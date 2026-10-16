// SPDX-License-Identifier: MIT
//
// GpuBackend impl - P8.2 (2026-10-16) stub
//
//   The stub delegates availability to an external provider (test
//   injects a flag; production wires a real OpenGL probe in P8.2.x).
//   processTile() returns the input unchanged so the registry can
//   route to it and downstream code sees the same output as CPU.

#include "GpuBackend.h"

namespace gpu {

GpuBackend::GpuBackend(bool capabilityProvider())
    : m_capabilityProvider(capabilityProvider)
{
}

bool GpuBackend::isAvailable() const
{
    if (m_capabilityProvider) return m_capabilityProvider();
    // No provider registered -> conservative: not available. P8.2.x
    // replaces this with a real QOpenGLContext probe.
    return false;
}

cv::Mat GpuBackend::processTile(const compositor::TileKey& /*key*/,
                               const cv::Mat& input,
                               RenderBackendContext& /*ctx*/)
{
    // P8.2 stub: pass-through. P8.2.x will upload to a texture, run a
    // shader, and read back. The interface is stable.
    return input.clone();
}

QStringList GpuBackend::capabilities() const
{
    return {
        QStringLiteral("per-tile (stub)"),
        QStringLiteral("Qt OpenGL (planned)"),
        QStringLiteral("driver TBD"),
    };
}

} // namespace gpu
