// SPDX-License-Identifier: MIT
//
// IccPngEmbed - P0-8.3 (2026-09-15)
//
// PNG iCCP chunk embedder (PS 同款 ICC profile 嵌入)
//
// 流程:
//   1. 用 QImageWriter 生成 base PNG bytes (without ICC)
//   2. parse PNG chunks, 在 IHDR 之后插入 iCCP chunk (profile name + compressed data)
//   3. 修正 IHDR 之后的 chunk CRC
//
// PNG iCCP chunk 结构 (RFC 2083 + extensions):
//   length (4 bytes, big-endian)
//   chunk type "iCCP" (4 bytes)
//   profile name (1-79 bytes, null-terminated)
//   compression method (1 byte, 0 = zlib)
//   compressed profile data (zlib stream)
//   CRC32 of type+data (4 bytes)
//
#pragma once

#include <QString>
#include <QByteArray>

namespace media {
namespace icc {

// 在已有的 PNG 文件 (filePath) 里嵌入 ICC profile (iccData)
//   成功 -> true, outputPath 写入新文件 (覆盖原 filePath)
//   失败 -> false, err 写错误
//   iccName: profile 短名, 1-79 chars ASCII (e.g.g "sRGB IEC61966-2.1")
bool embedIccToPng(const QString& filePath,
                    const QByteArray& iccData,
                    const QString& iccName,
                    QString* err = nullptr);

} // namespace icc
} // namespace media