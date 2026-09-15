// SPDX-License-Identifier: MIT
//
// IccJpegEmbed - P0-8.3 (2026-09-15)
//
// JPEG APP2 ICC_PROFILE marker embedder (PS 同款 ICC profile 嵌入)
//
// 流程:
//   1. QImageWriter 生成 base JPEG bytes
//   2. 跳 SOI (0xFFD8), 立即插入 APP2 marker
//   3. APP2 data: "ICC_PROFILE\0" (12 bytes) + sequence number (1) + total (1) + chunk data
//
// JPEG ICC_PROFILE 规范 (ICC v4.4 + JPEG APP2):
//   marker: 0xFF 0xE2
//   length: 2 bytes (含 length 自己, 不含 marker)
//   data: "ICC_PROFILE\0" + seq_no (1 byte) + total (1 byte) + ICC chunk (max 65519 bytes)
//
#pragma once

#include <QString>
#include <QByteArray>

namespace media {
namespace icc {

// 在已有的 JPEG 文件 (filePath) 里嵌入 ICC profile (iccData)
//   成功 -> true, filePath 被覆盖
//   失败 -> false, err 写错误
bool embedIccToJpeg(const QString& filePath,
                     const QByteArray& iccData,
                     QString* err = nullptr);

} // namespace icc
} // namespace media