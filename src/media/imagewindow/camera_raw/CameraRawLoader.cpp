// SPDX-License-Identifier: MIT
//
// CameraRawLoader implementation - P3.5 (2026-09-23)
//
#include "CameraRawLoader.h"

#include <QStringList>

#ifdef MULTIDOC_HAVE_LIBRAW
// Real libraw path is wired in CMake; declare prototypes we use here.
// Forward decls of libraw API to keep header light (only CameraRawLoader.cpp
// knows about libraw, the rest of the project talks to CameraRawLoader).
extern "C" {
struct libraw_data_t;
struct libraw_processed_image_t;
struct libraw_iparams_t;
typedef struct libraw_data_t *libraw_data_t_ptr;

libraw_data_t_ptr libraw_init(unsigned int flags);
void             libraw_close(libraw_data_t_ptr);
int              libraw_open_file(libraw_data_t_ptr, const char *fname);
int              libraw_unpack(libraw_data_t_ptr);
int              libraw_dcraw_process(libraw_data_t_ptr);
int              libraw_raw2image(libraw_data_t_ptr);  // older API
// libraw exposes output via libraw_dcraw_make_mem_image(&p, &sz, ...) but
// we use the in-memory image returned by libraw_unpack + dcraw_process.
//
// In C++ form via libraw/libraw.h:
#define LIBRAW_STRUCT_H
// libraw returns processed data via:
//   libraw_dcraw_make_mem_image(libraw_data_t*, int *, int)
//   libraw_dcraw_clear_mem(...)
// plus:
int  libraw_adjust_params_info_only(libraw_data_t_ptr, ...);
void libraw_set_output_params(libraw_data_t_ptr, void *);
}
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

CameraRawResult decodeRaw(const QString& path, const CameraRawSettings& settings)
{
    CameraRawResult r;
#ifdef MULTIDOC_HAVE_LIBRAW
    // Real libraw decode. Stub here; full implementation requires CMake
    // wiring find_package(LibRaw REQUIRED) + linked target. When project
    // dev sets up vcpkg libraw, this branch becomes live without
    // touching any caller code (interface is identical to the stub).
    //
    // Sketch:
    //   libraw_data_t *raw = libraw_init(0);
    //   if (libraw_open_file(raw, path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
    //       { r.errorMsg = "open failed"; return r; }
    //   if (libraw_unpack(raw) != LIBRAW_SUCCESS)
    //       { r.errorMsg = "unpack failed"; libraw_close(raw); return r; }
    //   libraw_set_output_params(raw, paramsFromSettings(settings));
    //   if (libraw_dcraw_process(raw) != LIBRAW_SUCCESS)
    //       { r.errorMsg = "dcraw_process failed"; libraw_close(raw); return r; }
    //   int sz = 0;
    //   libraw_processed_image_t *p = libraw_dcraw_make_mem_image(&raw, &sz, 1);
    //   if (!p) { r.errorMsg = "make_mem_image failed"; libraw_close(raw); return r; }
    //   // Convert p->data (RGB or BGR) -> cv::Mat BGR
    //   cv::Mat img(p->height, p->width, CV_MAKETYPE(CV_8U, p->colors),
    //               p->data, p->stride);
    //   cv::cvtColor(img, r.image, (p->colors == 3 ? cv::COLOR_RGB2BGR
    //                                            : cv::COLOR_GRAY2BGR));
    //   img.copyTo(r.image);  // decouple from raw's lifetime
    //   libraw_dcraw_clear_mem(p);
    //   libraw_close(raw);
    (void)settings; (void)path;
    r.errorMsg = QStringLiteral("P3.5 stub: libraw link OK but decode not wired yet");
#else
    (void)settings;
    r.errorMsg = QStringLiteral(
        "Camera Raw not supported: libraw not configured. "
        "Rebuild with vcpkg install libraw:x64-windows (or "
        "set LibRaw_DIR) and re-run cmake.");
#endif
    return r;
}

}  // namespace camera_raw