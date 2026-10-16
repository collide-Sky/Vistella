// SPDX-License-Identifier: MIT
//
// GpuBackend - P8.2 (2026-10-16) GPU backend STUB
//   This is intentionally a stub. It declares the interface +
//   reports availability based on a capability flag (real OpenGL
//   probe comes in P8.2.x). processTile() returns the input
//   unchanged, so it behaves identically to CpuBackend — but the
//   registry can route to it for Wet brushes and the status bar can
//   report "GPU: <name>".
//
//   The real shader pipeline (upload tile to texture, run shader,
//   read back to cv::Mat) arrives in P8.2.x. The interface is
//   stable: per-tile, no global state in the per-tile call.
//
//   P8.2 commit. P8.2.x incrementally fills in:
//     - QOpenGLContext probe
//     - Texture upload (cv::Mat -> QOpenGLTexture)
//     - QOpenGLShaderProgram (pass-through, blend, blur, ...)
//     - FBO readback (QOpenGLFramebufferObject -> cv::Mat)

#pragma once

#include "RenderBackend.h"

namespace gpu {

class GpuBackend : public RenderBackend {
public:
    // capabilityProvider returns true when GPU is usable right now.
    // For tests, the test sets a flag and the provider reads it.
    explicit GpuBackend(bool capabilityProvider() = nullptr);

    QString name() const override { return QStringLiteral("qt-opengl-stub"); }
    bool    isAvailable() const override;

    cv::Mat processTile(const compositor::TileKey& /*key*/,
                        const cv::Mat& input,
                        RenderBackendContext& /*ctx*/) override;

    QStringList capabilities() const override;

private:
    bool (*m_capabilityProvider)() = nullptr;
};

} // namespace gpu
