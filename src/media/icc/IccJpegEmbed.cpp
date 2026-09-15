// SPDX-License-Identifier: MIT
//
// IccJpegEmbed implementation - P0-8.3 (2026-09-15)
//
#include "IccJpegEmbed.h"
#include "logger.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>     // qToBigEndian

namespace media {
namespace icc {

namespace {

// 构造 APP2 ICC_PROFILE marker (单 chunk, seq_no=1, total=1)
QByteArray buildApp2IccMarker(const QByteArray& iccData)
{
    // "ICC_PROFILE\0" = 12 bytes
    QByteArray marker;
    marker.append("ICC_PROFILE");
    marker.append('\0');
    marker.append(static_cast<char>(1));   // sequence number (first chunk)
    marker.append(static_cast<char>(1));   // total chunks (only one)
    marker.append(iccData);

    // marker data length (含 length field 自己, 不含 marker bytes)
    const quint16 length = static_cast<quint16>(marker.size() + 2);

    QByteArray app2;
    app2.append(static_cast<char>(0xFF));
    app2.append(static_cast<char>(0xE2));   // APP2
    {
        char buf[2];
        qToBigEndian<quint16>(length, reinterpret_cast<uchar*>(buf));
        app2.append(buf, 2);
    }
    app2.append(marker);
    return app2;
}

} // anonymous namespace

bool embedIccToJpeg(const QString& filePath,
                     const QByteArray& iccData,
                     QString* err)
{
    if (iccData.isEmpty()) {
        if (err) *err = QStringLiteral("ICC data is empty");
        return false;
    }
    if (iccData.size() > 65519) {
        // JPEG APP2 single marker max is 65535 - 16 ("ICC_PROFILE\0\1\1" + length + marker) = 65519
        // 实际 ICC 很少超过这个, sRGB profile ~3KB, Adobe RGB ~3KB
        if (err) *err = QStringLiteral("ICC profile too large for single APP2 marker");
        return false;
    }

    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Failed to open JPEG: %1").arg(in.errorString());
        return false;
    }
    const QByteArray src = in.readAll();
    in.close();

    // JPEG 必须以 SOI 开头: 0xFF 0xD8
    if (src.size() < 4
        || static_cast<uchar>(src[0]) != 0xFF
        || static_cast<uchar>(src[1]) != 0xD8) {
        if (err) *err = QStringLiteral("Not a valid JPEG file");
        return false;
    }

    QByteArray dst;
    dst.reserve(src.size() + iccData.size() + 64);
    // SOI + APP2 marker (插在 SOI 之后, 在其他 marker 之前)
    dst.append(src.left(2));                          // SOI
    dst.append(buildApp2IccMarker(iccData));         // APP2 ICC_PROFILE
    dst.append(src.mid(2));                           // rest of JPEG (other markers + scan data)

    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("Failed to write JPEG: %1").arg(out.errorString());
        return false;
    }
    if (out.write(dst) != dst.size()) {
        if (err) *err = QStringLiteral("JPEG write short");
        return false;
    }
    if (!out.commit()) {
        if (err) *err = QStringLiteral("JPEG commit failed: %1").arg(out.errorString());
        return false;
    }
    LOG_INFO("[IccJpeg] embedded ICC profile ({} bytes) into {}",
             iccData.size(), filePath.toStdString());
    return true;
}

} // namespace icc
} // namespace media