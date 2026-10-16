// SPDX-License-Identifier: MIT
//
// CpuBackend impl - P8.2 (2026-10-16)

#include "CpuBackend.h"

namespace gpu {

cv::Mat CpuBackend::processTile(const compositor::TileKey& /*key*/,
                               const cv::Mat& input,
                               RenderBackendContext& /*ctx*/)
{
    // Pass-through: clone so the caller can free the input scratch.
    return input.clone();
}

QStringList CpuBackend::capabilities() const
{
    return {
        QStringLiteral("per-tile pass-through"),
        QStringLiteral("always available"),
        QStringLiteral("no GPU dependency"),
    };
}

} // namespace gpu
