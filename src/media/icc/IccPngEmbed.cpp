// SPDX-License-Identifier: MIT
//
// IccPngEmbed implementation - P0-8.3 (2026-09-15)
//
// PNG iCCP chunk 嵌入
//   不依赖 libpng, 直接用 Qt + LCMS2 (zlib 压缩)
//   路径:
//     1. 打开 PNG 文件, 跳 PNG signature (8 bytes)
//     2. 遍历 chunks (length + type + data + CRC)
//     3. 在 IHDR 之后插入 iCCP chunk
//     4. 写回新文件
//
#include "IccPngEmbed.h"
#include "logger.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>     // qToBigEndian / qFromBigEndian
#include <QtZlib/zlib.h>

namespace media {
namespace icc {

namespace {

// CRC32 for PNG chunk (RFC 2083)
quint32 crc32_png(const QByteArray& data)
{
    // use zlib's crc32 (same algorithm as PNG)
    return crc32(0L, reinterpret_cast<const Bytef*>(data.constData()),
                 static_cast<uInt>(data.size()));
}

// 构造 PNG iCCP chunk (length + "iCCP" + name\0 + 0 + compressed data + CRC)
QByteArray buildIccpChunk(const QByteArray& iccData, const QString& iccName)
{
    // profile name (1-79 bytes, null-terminated, ASCII)
    QByteArray name = iccName.toLatin1();
    if (name.isEmpty()) name = "ICC Profile";
    if (name.size() > 79) name = name.left(79);

    // zlib compress (compression method = 0)
    uLongf compressedSize = compressBound(static_cast<uLong>(iccData.size()));
    QByteArray compressed(static_cast<int>(compressedSize), '\0');
    int rc = compress2(reinterpret_cast<Bytef*>(compressed.data()),
                       &compressedSize,
                       reinterpret_cast<const Bytef*>(iccData.constData()),
                       static_cast<uLong>(iccData.size()),
                       Z_DEFAULT_COMPRESSION);
    if (rc != Z_OK) {
        LOG_WARN("[IccPng] zlib compress failed: rc={}", rc);
        return QByteArray();
    }
    compressed.resize(static_cast<int>(compressedSize));

    // chunk data: name + 0x00 + 0x00 + compressed
    QByteArray chunkData;
    chunkData.append(name);
    chunkData.append('\0');
    chunkData.append(static_cast<char>(0));  // compression method = 0 (zlib)
    chunkData.append(compressed);

    // chunk type + data for CRC
    QByteArray typeAndData;
    typeAndData.append("iCCP");
    typeAndData.append(chunkData);
    const quint32 crc = crc32_png(typeAndData);

    // chunk header: length + type + data + CRC (all big-endian)
    QByteArray chunk;
    {
        char buf[4];
        qToBigEndian<quint32>(static_cast<quint32>(chunkData.size()),
                              reinterpret_cast<uchar*>(buf));
        chunk.append(buf, 4);
    }
    chunk.append("iCCP");
    chunk.append(chunkData);
    {
        char buf[4];
        qToBigEndian<quint32>(crc, reinterpret_cast<uchar*>(buf));
        chunk.append(buf, 4);
    }
    return chunk;
}

} // anonymous namespace

bool embedIccToPng(const QString& filePath,
                    const QByteArray& iccData,
                    const QString& iccName,
                    QString* err)
{
    if (iccData.isEmpty()) {
        if (err) *err = QStringLiteral("ICC data is empty");
        return false;
    }
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Failed to open PNG: %1").arg(in.errorString());
        return false;
    }
    const QByteArray src = in.readAll();
    in.close();

    // PNG signature: 8 bytes (0x89 'P' 'N' 'G' '\r' '\n' 0x1A '\n')
    static const char kPngSig[8] = {
        static_cast<char>(0x89), 'P', 'N', 'G', '\r', '\n',
        static_cast<char>(0x1A), '\n'
    };
    if (src.size() < 8 || QByteArray(src.left(8)) != QByteArray(kPngSig, 8)) {
        if (err) *err = QStringLiteral("Not a valid PNG file");
        return false;
    }

    QByteArray dst;
    dst.reserve(src.size() + 4096);
    dst.append(src.left(8));   // PNG signature

    // 遍历 chunks: 4 bytes length + 4 bytes type + data + 4 bytes CRC
    int pos = 8;
    bool inserted = false;
    while (pos + 8 <= src.size()) {
        quint32 length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(src.constData() + pos));
        const char* typePtr = src.constData() + pos + 4;
        if (pos + 8 + length + 4 > src.size()) {
            // malformed, abort embed
            if (err) *err = QStringLiteral("Malformed PNG chunk at pos %1").arg(pos);
            return false;
        }

        QByteArray chunkHeader(src.mid(pos, 8));   // length + type
        QByteArray chunkData(src.mid(pos + 8, static_cast<int>(length)));
        QByteArray chunkCrc(src.mid(pos + 8 + length, 4));

        // IHDR 后立即插入 iCCP
        if (!inserted && QByteArray(typePtr, 4) == QByteArray("IHDR", 4)) {
            QByteArray iccp = buildIccpChunk(iccData, iccName);
            if (iccp.isEmpty()) {
                if (err) *err = QStringLiteral("buildIccpChunk failed");
                return false;
            }
            dst.append(chunkHeader);
            dst.append(chunkData);
            dst.append(chunkCrc);
            dst.append(iccp);
            inserted = true;
        } else {
            dst.append(chunkHeader);
            dst.append(chunkData);
            dst.append(chunkCrc);
        }

        pos += 8 + length + 4;

        // IEND 后停止
        if (QByteArray(typePtr, 4) == QByteArray("IEND", 4)) break;
    }

    if (!inserted) {
        if (err) *err = QStringLiteral("PNG has no IHDR chunk");
        return false;
    }

    // 写新文件 (overwrite original)
    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("Failed to write PNG: %1").arg(out.errorString());
        return false;
    }
    if (out.write(dst) != dst.size()) {
        if (err) *err = QStringLiteral("PNG write short");
        return false;
    }
    if (!out.commit()) {
        if (err) *err = QStringLiteral("PNG commit failed: %1").arg(out.errorString());
        return false;
    }
    LOG_INFO("[IccPng] embedded ICC profile ({} bytes) into {}",
             iccData.size(), filePath.toStdString());
    return true;
}

} // namespace icc
} // namespace media