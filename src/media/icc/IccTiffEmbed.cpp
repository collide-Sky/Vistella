// SPDX-License-Identifier: MIT
//
// IccTiffEmbed implementation - P0-8.4 (2026-09-15)
//
// 详见 IccTiffEmbed.h 头注释
//
#include "IccTiffEmbed.h"
#include "logger.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

namespace media {
namespace icc {

namespace {

// TIFF type -> element size in bytes
quint32 tiffTypeSize(quint16 type)
{
    switch (type) {
    case 1:  return 1;   // BYTE
    case 2:  return 1;   // ASCII
    case 3:  return 2;   // SHORT
    case 4:  return 4;   // LONG
    case 5:  return 8;   // RATIONAL (num/den)
    case 6:  return 1;   // SBYTE
    case 7:  return 1;   // UNDEFINED
    case 8:  return 2;   // SSHORT
    case 9:  return 4;   // SLONG
    case 10: return 8;   // SRATIONAL
    case 11: return 4;   // FLOAT
    case 12: return 8;   // DOUBLE
    default: return 1;
    }
}

struct TiffEntry {
    quint16 tag;
    quint16 type;
    quint32 count;
    quint32 value;       // value if in entry (count*typeSize <= 4), else offset
    quint32 dataOffset;  // 0 if in entry
    quint32 dataSize;    // total bytes if in extras (count * typeSize)
};

static int findIccIndex(const QList<TiffEntry>& entries)
{
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].tag == 34675) return i;
    }
    return -1;
}

} // anonymous namespace

bool embedIccToTiff(const QString& filePath,
                     const QByteArray& iccData,
                     QString* err)
{
    if (iccData.isEmpty()) {
        if (err) *err = QStringLiteral("ICC data is empty");
        return false;
    }

    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Failed to open TIFF: %1").arg(in.errorString());
        return false;
    }
    const QByteArray src = in.readAll();
    in.close();

    // 1. Header
    if (src.size() < 8) {
        if (err) *err = QStringLiteral("TIFF too small");
        return false;
    }
    const uchar b0 = static_cast<uchar>(src[0]);
    const uchar b1 = static_cast<uchar>(src[1]);
    bool littleEndian;
    if (b0 == 'I' && b1 == 'I')      littleEndian = true;
    else if (b0 == 'M' && b1 == 'M') littleEndian = false;
    else {
        if (err) *err = QStringLiteral("Not a TIFF file (bad byte order)");
        return false;
    }
    auto read16 = [&](const uchar* p) -> quint16 {
        return littleEndian ? qFromLittleEndian<quint16>(p)
                              : qFromBigEndian<quint16>(p);
    };
    auto read32 = [&](const uchar* p) -> quint32 {
        return littleEndian ? qFromLittleEndian<quint32>(p)
                              : qFromBigEndian<quint32>(p);
    };
    auto write16 = [&](quint16 v, uchar* p) {
        if (littleEndian) qToLittleEndian<quint16>(v, p);
        else              qToBigEndian<quint16>(v, p);
    };
    auto write32 = [&](quint32 v, uchar* p) {
        if (littleEndian) qToLittleEndian<quint32>(v, p);
        else              qToBigEndian<quint32>(v, p);
    };

    if (read16(reinterpret_cast<const uchar*>(src.constData() + 2)) != 42) {
        if (err) *err = QStringLiteral("Bad TIFF magic");
        return false;
    }
    const quint32 ifdOffset = read32(reinterpret_cast<const uchar*>(src.constData() + 4));
    if (ifdOffset + 2 > static_cast<quint32>(src.size())) {
        if (err) *err = QStringLiteral("IFD offset out of bounds");
        return false;
    }

    // 2. Parse IFD
    const quint16 numEntries = read16(reinterpret_cast<const uchar*>(src.constData() + ifdOffset));
    const quint32 entriesStart = ifdOffset + 2;
    const quint32 entriesEnd   = entriesStart + numEntries * 12;
    if (entriesEnd + 4 > static_cast<quint32>(src.size())) {
        if (err) *err = QStringLiteral("IFD entries out of bounds");
        return false;
    }
    QList<TiffEntry> entries;
    bool hasICC = false;
    for (quint16 i = 0; i < numEntries; ++i) {
        const uchar* p = reinterpret_cast<const uchar*>(src.constData() + entriesStart + i * 12);
        TiffEntry e;
        e.tag   = read16(p);
        e.type  = read16(p + 2);
        e.count = read32(p + 4);
        e.value = read32(p + 8);
        const quint32 elemSize = tiffTypeSize(e.type);
        const quint32 totalSize = e.count * elemSize;
        if (totalSize <= 4) {
            e.dataOffset = 0;
            e.dataSize = 0;
        } else {
            e.dataOffset = e.value;
            e.dataSize = totalSize;
        }
        if (e.tag == 34675) hasICC = true;
        entries.append(e);
    }
    const quint32 nextIfdOffset = read32(reinterpret_cast<const uchar*>(src.constData() + entriesEnd));
    const quint32 ifdEnd = entriesEnd + 4;   // IFD ends after next_ifd_offset

    // 3. Compute extras region (offset >= ifdEnd) - this is the "secondary data area"
    //    Includes: extras (XResolution 8 bytes etc.) + image data (strip)
    //    P0-8.4 简化: 假设 image data 在 extras 之后, extras 在 image data 之前
    //                (Qt TIFF 输出典型格式)
    //    实际上无法精确分离, 我们把 "secondary data" 整块后移 (处理 extras offsets)
    //    对 StripOffsets 这种 large offset 也按 ifdEnd 之后统一处理
    //
    // 简化策略:
    //   - extras + image data 在 ifdEnd..file_end 范围
    //   - 加 ICCProfile entry 后, ifdEnd 增加 12 (一个新 entry)
    //   - 所有 entries 的 value offset >= 旧 ifdEnd 的, 加 12 (因为 IFD 变大)
    //   - ICCProfile value 指向 file_end (image data + extras 之后)

    const bool addEntry = !hasICC;
    const quint32 newIfdEnd = ifdEnd + (addEntry ? 12 : 0);
    // secondary data 的字节数 (原 IFD 之后的所有内容)
    const quint32 secDataStart = ifdEnd;
    const quint32 secDataSize  = static_cast<quint32>(src.size()) - secDataStart;

    QByteArray newFile;
    newFile.reserve(static_cast<int>(src.size() + 12 + iccData.size()));

    // Header (8 bytes)
    newFile.append(src.left(8));

    // New IFD: num_entries + entries + next_ifd_offset
    {
        char buf[4];
        const quint16 newNumEntries = static_cast<quint16>(entries.size() + (addEntry ? 1 : 0));
        write16(newNumEntries, reinterpret_cast<uchar*>(buf));
        newFile.append(buf, 2);

        // Existing entries, fix value offsets
        for (const auto& e : entries) {
            char entryBuf[12];
            write16(e.tag, reinterpret_cast<uchar*>(entryBuf));
            write16(e.type, reinterpret_cast<uchar*>(entryBuf + 2));
            write32(e.count, reinterpret_cast<uchar*>(entryBuf + 4));
            quint32 newValue = e.value;
            if (e.dataOffset != 0 && e.dataOffset >= secDataStart) {
                // offset 指向 secondary data, 加 12 (IFD 变大)
                newValue = e.dataOffset + 12;
            } else if (e.tag == 34675) {
                // ICCProfile value 指向新 ICC data (放在 file 末尾)
                // ICC data offset = newFileSize (header 8 + new IFD + secondary data)
                // 等 secondary data 写完后计算
            }
            write32(newValue, reinterpret_cast<uchar*>(entryBuf + 8));
            newFile.append(entryBuf, 12);
        }

        // Add ICCProfile entry if not exists
        quint32 iccValuePlaceholder = 0;  // 后面回填
        if (addEntry) {
            char entryBuf[12];
            write16(static_cast<quint16>(34675), reinterpret_cast<uchar*>(entryBuf));
            write16(static_cast<quint16>(7), reinterpret_cast<uchar*>(entryBuf + 2));   // UNDEFINED
            write32(static_cast<quint32>(iccData.size()), reinterpret_cast<uchar*>(entryBuf + 4));
            write32(iccValuePlaceholder, reinterpret_cast<uchar*>(entryBuf + 8));   // 占位, 后面填
            newFile.append(entryBuf, 12);
        }

        // next IFD offset
        write32(nextIfdOffset, reinterpret_cast<uchar*>(buf));
        newFile.append(buf, 4);
    }

    // Secondary data (extras + image data), copy as-is
    newFile.append(src.mid(static_cast<int>(secDataStart), static_cast<int>(secDataSize)));

    // ICC data at end
    const quint32 iccDataOffset = static_cast<quint32>(newFile.size());
    newFile.append(iccData);

    // Backfill ICCProfile value offset (entry 位置: 8 + 2 + hasICC ? (numEntries * 12) : numEntries * 12)
    //   hasICC (replace):  value 在 entry index = hasICC ? hasICC_index : (n-1) 末尾
    //   not hasICC (add):  value 在 entry index = numEntries (新加的)
    // 算 entry 字节位置:
    //   header (8) + num_entries (2) + entryIndex * 12 + 8 (value field 在 entry 内偏移 8)
    const quint32 valueFieldOffset = 8 + 2
        + static_cast<quint32>(hasICC ? (findIccIndex(entries) * 12) : (entries.size() * 12))
        + 8;
    if (valueFieldOffset + 4 <= static_cast<quint32>(newFile.size())) {
        write32(iccDataOffset, reinterpret_cast<uchar*>(newFile.data() + valueFieldOffset));
    } else {
        if (err) *err = QStringLiteral("Backfill offset out of bounds");
        return false;
    }

    // Write to file
    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("Failed to write TIFF: %1").arg(out.errorString());
        return false;
    }
    if (out.write(newFile) != newFile.size()) {
        if (err) *err = QStringLiteral("TIFF write short");
        return false;
    }
    if (!out.commit()) {
        if (err) *err = QStringLiteral("TIFF commit failed");
        return false;
    }
    LOG_INFO("[IccTiff] embedded ICC profile ({} bytes) into {}",
             iccData.size(), filePath.toStdString());
    return true;
}

// IccTiffEmbed.cpp 内嵌 helper (lambdas 不能跨函数)
//   static findIccIndex 已移到 anonymous namespace 之前

} // namespace icc
} // namespace media