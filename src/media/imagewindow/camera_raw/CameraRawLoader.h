// SPDX-License-Identifier: MIT
//
// CameraRawLoader - P3.5 (2026-09-23) libraw-based RAW decoder interface
//
//   Layer between ImageIOController and libraw (or stub).
//   CMakeLists.txt conditionally defines MULTIDOC_HAVE_LIBRAW when
//   find_package(LibRaw) succeeds; otherwise stub returns empty image
//   + descriptive error. The full interface is here so wiring (menu,
//   extension registry, dialog flow) is complete; only the actual decode
//   step swaps between real libraw and the no-op stub.
//
//   libraw: https://www.libraw.org/  (LGPL-2.1)
//   Wrap cv::Mat output from libraw_unpack / libraw_dcraw_process +
//   libraw_to_bayer_4n_to_8n or libraw_raw2image / libraw_imgbuf_extract.
//   For PS-style Camera Raw dialog exposure/wb/sharpness/noise params,
//   libraw supports open_file + adjust_params_info_only + dcraw_process
//   with dcraw_process_params. We follow that route.
//
#pragma once

#include <QString>
#include <opencv2/core.hpp>

namespace camera_raw {

struct CameraRawSettings {
    // P3.5 default: PS-style Camera Raw dialog fields.
    // All values are 0-100 normalized (50 = no-op).
    int exposure    = 50;   // -2 EV .. +2 EV, 50 = 0 EV (no-op)
    int wbTemp      = 50;   // 2000K..10000K, 50 = 6500K (default)
    int wbTint      = 50;   // -150..+150, 50 = 0 (no-op)
    int sharpness   = 50;   // 0..100, 50 = default
    int noiseRedux  = 50;   // 0..100, 50 = default
    // Output bit depth (libraw can produce 8 or 16 bit per channel)
    int bitsPerChannel = 8; // 8 or 16
};

struct CameraRawResult {
    cv::Mat         image;      // BGR cv::Mat (RGB after libraw conversion)
    QString         errorMsg;   // empty = success
    bool            ok() const { return errorMsg.isEmpty() && !image.empty(); }
};

// decodeRaw: 加载 RAW file + 应用 settings, 返回 decoded image 或 error.
//   真实 libraw 实现 (CMake HAVE_LIBRAW): 调 libraw_open_file +
//   libraw_unpack + libraw_dcraw_process + libraw_to_bayer_4n_to_8n +
//   手动 cv::cvtColor (Bayer BGGR -> BGR).
//   Stub (没 libraw): 返 errorMsg = "libraw not configured; vcpkg install libraw:x64-windows"。
CameraRawResult decodeRaw(const QString& path, const CameraRawSettings& settings);

// hasLibRawSupport: CMake 编译时是否启用了 libraw. 用来给 UI 隐藏/禁用 RAW 菜单.
bool hasLibRawSupport();

// 列出支持的 RAW 扩展名 (用于 FileExtensionRegistry + 文件对话框 filter).
QStringList supportedRawExtensions();

}  // namespace camera_raw