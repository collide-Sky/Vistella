// SPDX-License-Identifier: MIT
//
// CpuBackend - CPU pass-through backend
//   processTile returns the input unchanged. The actual pixel work
//   (paint, blend, etc.) is done by the CompositorNode's compute()
//   method which already runs on CPU. CpuBackend is a no-op shim
//   used to satisfy the RenderBackend interface so the registry can
//   always fall back to "CPU" without special-casing.
//
//   P8.2 commit. P8.2.x may extend this with SIMD intrinsics or
//   OpenCV's parallel_for_ tile loop, but for now it's a thin wrapper.

#pragma once

#include "RenderBackend.h"

namespace gpu {

class CpuBackend : public RenderBackend {
public:
    CpuBackend() = default;

    QString name() const override { return QStringLiteral("cpu"); }
    bool    isAvailable() const override { return true; }

    cv::Mat processTile(const compositor::TileKey& /*key*/,
                        const cv::Mat& input,
                        RenderBackendContext& /*ctx*/) override;

    QStringList capabilities() const override;
};

} // namespace gpu
