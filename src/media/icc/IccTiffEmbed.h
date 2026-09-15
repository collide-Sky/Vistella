// SPDX-License-Identifier: MIT
//
// IccTiffEmbed - P0-8.4 (2026-09-15)
//
// TIFF ICCProfile tag embedder (PS 同款 ICC profile 嵌入)
//
// 流程:
//   1. 打开 TIFF 文件, 解析 byte order (II/MM) + magic (42) + IFD0 offset
//   2. 解析 IFD entries (num_entries + entries + next_ifd_offset)
//   3. 检查是否已有 ICCProfile tag (34675)
//      - 没有: 加新 entry
//      - 有: replace value 指向新的 ICC data
//   4. 重建文件: header + new IFD + extras (offset 修正) + image data + ICC data
//
// 假设 (Qt 默认 TIFF 输出格式):
//   - Little-endian (II)
//   - Classic TIFF (magic 42), 不是 BigTIFF
//   - Single IFD, single strip
//   - Extras (XResolution/YResolution 8 bytes) 在 IFD 之后, image data 之前
//
#pragma once

#include <QString>
#include <QByteArray>

namespace media {
namespace icc {

// 在已有的 TIFF 文件 (filePath) 里嵌入 ICC profile (iccData)
//   成功 -> true, filePath 被覆盖
//   失败 -> false, err 写错误
bool embedIccToTiff(const QString& filePath,
                     const QByteArray& iccData,
                     QString* err = nullptr);

} // namespace icc
} // namespace media