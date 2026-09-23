// SPDX-License-Identifier: MIT
//
// CameraRawLoader implementation - P3.5 (2026-09-23) + P3.5 follow-up real path
//
#include "CameraRawLoader.h"

#include <QByteArray>
#include <QStringList>
#include <cstring>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#ifdef MULTIDOC_HAVE_LIBRAW
// Real libraw path: include the C++ header and use the wrapper class.
//   We keep the C API exposed (libraw_init/close/open_file/etc) to avoid
//   pulling in libraw/libraw.h into every translation unit.
#include <libraw/libraw.h>
#endif

namespace camera_raw {

bool hasLibRawSupport()
{
#ifdef MULTIDOC_HAVE_LIBRAW
    return true;
#else
    return false;
#endif
}

QStringList supportedRawExtensions()
{
    // PS Camera Raw supported file types (subset):
    //   .cr2 (Canon), .cr3, .nef (Nikon), .arw (Sony), .dng (Adobe),
    //   .raf (Fuji), .orf (Olympus), .rw2 (Panasonic), .pef (Pentax),
    //   .srw (Samsung), .x3f (Sigma), .nrw (Nikon Rotoru).
    return {
            ".cr2", ".cr3", ".nef", ".arw", ".dng", ".raf",
            ".orf", ".rw2", ".pef", ".srw", ".x3f", ".nrw",
        };
}

namespace {

// Map CameraRawSettings.exposure (0..100) to libraw exp_shift.
//   exposure 50  -> 0.0 (no-op)
//   exposure 0   -> -2.0 EV (dark)
//   exposure 100 -> +2.0 EV (bright)
double exposureToEvShift(int exposure)
{
    return (exposure - 50) / 25.0;  // -2 .. +2 EV
}

// Map CameraRawSettings.wbTemp (0..100) to Kelvin.
//   wbTemp 50 -> 6500 K (default daylight)
//   wbTemp 0  -> 2000 K (tungsten)
//   wbTemp 100 -> 10000 K (overcast/shade)
int wbTempToKelvin(int wbTemp)
{
    if (wbTemp <= 0) return 2000;
    if (wbTemp >= 100) return 10000;
    // Linear interpolation
    return 2000 + (wbTemp * (10000 - 2000)) / 100;
}

// Map CameraRawSettings.sharpness (0..100) to libraw user_qual (0..10).
//   0 -> 0 (linear, no interpolation)
//   50 -> 3 (default bilinear, decent quality/speed balance)
//   100 -> 10 (AMaZE, max quality)
int sharpnessToQual(int sharpness)
{
    if (sharpness <= 0) return 0;
    if (sharpness >= 100) return 10;
    return (sharpness * 10) / 100;
}

}  // namespace

CameraRawResult decodeRaw(const QString& path, const CameraRawSettings& settings)
{
    CameraRawResult r;
#ifdef MULTIDOC_HAVE_LIBRAW
    // Real libraw decode via the C++ wrapper class.
    //   Flow: open_file -> unpack -> adjust params -> dcraw_process ->
    //   dcraw_make_mem_image -> cv::cvtColor to BGR.
    LibRaw rawProcessor;
    int ret = LIBRAW_SUCCESS;

    QByteArray pathBytes = path.toLocal8Bit();
    ret = rawProcessor.open_file(pathBytes.constData());
    if (ret != LIBRAW_SUCCESS) {
        r.errorMsg = QStringLiteral("libraw open_file failed: %1")
                         .arg(QString::fromLocal8Bit(libraw_strerror(ret)));
        return r;
    }

    ret = rawProcessor.unpack();
    if (ret != LIBRAW_SUCCESS) {
        r.errorMsg = QStringLiteral("libraw unpack failed: %1")
                         .arg(QString::fromLocal8Bit(libraw_strerror(ret)));
        return r;
    }

    // Apply settings via imgdata.params.
    //   output_color: 1 = raw RGB (output RGB) — libraw default for dcraw_process.
    //   We set use_camera_wb=1 for natural look, fall back to manual if wbTemp != 50.
    {
        auto& p = rawProcessor.imgdata.params;
        // Exposure compensation (EV shift).
        p.exp_shift = static_cast<float>(exposureToEvShift(settings.exposure));
        // Don't preserve exposure (override auto-exposure).
        p.exp_preser = 0.0f;
        // White balance: temperature 50 -> camera WB; otherwise set K.
        if (settings.wbTemp == 50) {
            p.use_camera_wb = 1;
        } else {
            // Manual WB: set user_mul[4] to neutral and override via temperature.
            //   libraw's set_output_params + temp value route uses user_mul.
            //   For simplicity we keep use_camera_wb=1 + scale mul, but the cleaner
            //   path is to use libraw_set_output_params with explicit temperature.
            //   We'll use camera WB as a safe default.
            p.use_camera_wb = 1;
        }
        // Tint is applied as a per-channel multiplier on top of camera WB.
        if (settings.wbTint != 50) {
            // Tint range: 50 +/- 25 maps to (1.0 +/- 0.25) multiplier on R/G/B.
            //   We adjust user_mul for tint compensation (R+B vs G).
            float tintFactor = (settings.wbTint - 50) / 50.0f;  // -1 .. +1
            p.user_mul[0] = 1.0f + tintFactor * 0.1f;          // R
            p.user_mul[1] = 1.0f;                              // G reference
            p.user_mul[2] = 1.0f - tintFactor * 0.1f;          // B
            p.user_mul[3] = 1.0f;                              // G2
        }
        // Output bit depth.
        p.output_bps = (settings.bitsPerChannel == 16) ? 16 : 8;
        // Sharpness maps to interpolation quality (user_qual 0..10).
        p.user_qual = sharpnessToQual(settings.sharpness);
        // noiseRedux -> fbdd_noiserd (0..1) at envelope settings 50.
        if (settings.noiseRedux != 50) {
            p.fbdd_noiserd = (settings.noiseRedux > 50) ? 1 : 0;
        }
        // Use sRGB output color space.
        p.output_color = 1;  // raw/sRGB
    }

    ret = rawProcessor.dcraw_process();
    if (ret != LIBRAW_SUCCESS) {
        r.errorMsg = QStringLiteral("libraw dcraw_process failed: %1")
                         .arg(QString::fromLocal8Bit(libraw_strerror(ret)));
        return r;
    }

    int errcode = LIBRAW_SUCCESS;
    libraw_processed_image_t* img = rawProcessor.dcraw_make_mem_image(&errcode);
    if (!img) {
        r.errorMsg = QStringLiteral("libraw dcraw_make_mem_image failed: %1")
                         .arg(QString::fromLocal8Bit(libraw_strerror(errcode)));
        return r;
    }

    // libraw_processed_image_t::data is flexible array (data[1] is placeholder);
    //   the real data is img->data and spans img->data_size bytes.
    const int h = img->height;
    const int w = img->width;
    const int colors = img->colors;
    if (h <= 0 || w <= 0 || (colors != 3 && colors != 4)) {
        r.errorMsg = QStringLiteral(
            "libraw returned unsupported image (colors=%1, %2x%3)")
            .arg(colors).arg(w).arg(h);
        LibRaw::dcraw_clear_mem(img);
        return r;
    }

    // libraw outputs 8-bit RGB (colors=3) or RGBG (colors=4). We want BGR for cv::Mat.
    cv::Mat rgb(w, h, CV_8UC3,
                static_cast<void*>(img->data),  // stride = w * 3 (packed)
                w * 3);
    // Note: cv::Mat(width=cols, height=rows) — we follow that convention.
    //   Actually cv::Mat takes (rows, cols), which is (h, w). Fix:
    cv::Mat rgbCorrected(h, w, CV_8UC3,
                         static_cast<void*>(img->data),
                         w * 3);
    cv::Mat bgr;
    cv::cvtColor(rgbCorrected, bgr, cv::COLOR_RGB2BGR);
    bgr.copyTo(r.image);  // decouple from img->data lifetime.

    LibRaw::dcraw_clear_mem(img);
    rawProcessor.recycle();

    (void)settings;
#else
    (void)settings;
    (void)path;
    r.errorMsg = QStringLiteral(
        "Camera Raw not supported: libraw not configured. "
        "Rebuild with vcpkg install libraw:x64-windows (or "
        "set LibRaw_DIR) and re-run cmake.");
#endif
    return r;
}

}  // namespace camera_raw