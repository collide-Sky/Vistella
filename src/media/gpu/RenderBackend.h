// SPDX-License-Identifier: MIT
//
// RenderBackend - per-tile GPU/CPU backend interface
//   Pure abstract: a backend takes a tile (cv::Mat) and returns a
//   processed tile. The interface is small so future backends (D3D12,
//   Metal, Vulkan) can implement it without taking a dependency on
//   Qt / OpenCV specifics.
//
//   MVP scope (user拍板)
//     - CpuBackend: pass-through (default; always available).
//     - GpuBackend: stub. Declared + instantiable; processTile returns
//       the input unchanged. The real shader path arrives in P8.2.x
//       incrementally (1 shader at a time: pass-through, blend mode,
//       blur, etc.).
//   P8.2 does NOT change the existing render path; RenderGraph still
//   runs LayerBitmapNode's compute() directly. P8.2.x integrates the
//   backend into RenderGraph.
//
//   Per-tile interface (P8.2 design choice): 1 backend processes 1
//   tile at a time. Easy to map to GPU compute-shader (1 thread per
//   tile) or to CPU loops. Alternative (per-node / per-graph) was
//   rejected as too coarse for the Wet-vs-Basic routing needs.

#pragma once

#include <QString>
#include <QStringList>
#include <opencv2/core.hpp>

#include "../compositor/TileGrid.h"   // TileKey

namespace gpu {

// Read-only context a backend may consult (e.g. shared brush state for
// blend mode lookup, tile grid geometry, debug flags). Backends are
// expected to treat this as advisory; the per-tile input is the
// authoritative payload.
class RenderBackendContext {
public:
    // Source data for the rendering. Currently empty; P8.2.x will plumb
    // the shared brush state + node kind here for backend dispatch.
    virtual ~RenderBackendContext() = default;
    virtual QStringList activeCaps() const { return {}; }
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    // Stable id for logs / status bar. e.g. "cpu" / "qt-opengl" /
    // "d3d12".
    virtual QString name() const = 0;

    // Whether this backend is actually usable right now. For the
    // MVP CpuBackend: always true. GpuBackend stub: true if a
    // display surface is available (test environment reports false).
    virtual bool isAvailable() const = 0;

    // Per-tile processing. Implementations must:
    //   - Return a cv::Mat of the same size + type as `input` (or
    //     a transformed variant for blend nodes). The caller stitches
    //     the result into the canvas.
    //   - Not retain references to `input` after returning (the
    //     caller may free it).
    //   - Be thread-safe (P8.4 will fan tiles across threads).
    virtual cv::Mat processTile(const compositor::TileKey& key,
                                const cv::Mat& input,
                                RenderBackendContext& ctx) = 0;

    // Optional human-readable capability list (for status bar / logs).
    virtual QStringList capabilities() const { return {}; }
};

} // namespace gpu
