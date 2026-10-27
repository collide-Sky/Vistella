// SPDX-License-Identifier: MIT
//
// GpuBackend - real Qt OpenGL shader pipeline
//
//   Pass-through fragment shader MVP. The backend owns a single
//   QOpenGLContext + QOffscreenSurface + a QOpenGLShaderProgram that
//   just samples the input texture and writes the same color out.
//   processTile() uploads the cv::Mat to a texture, renders to an
//   FBO, reads back the result.
//
//   Thread-safety: QOpenGLContext can only be current on one thread
//   at a time, so all GL work runs under m_lock. Multiple workers
//   calling processTile() concurrently serialize through this lock.
//   Context + shader + FBO are kept alive across calls (init once,
//   reuse forever).
//
//   Pixel format pipeline:
//     cv::Mat (CV_8UC3 BGR) -> QImage (Format_BGR888) -> GL_BGR upload
//     GL pass-through shader
//     glReadPixels into QImage (Format_BGR888) -> cv::Mat (BGR)
//
//   For formats that don't map cleanly to GL_BGR (e.g. CV_8UC4
//   BGRA), the backend will fall back to input.clone() to avoid
//   silent corruption. Future P8.2.x shader work (blend, blur)
//   will route through this same path.
//
//   Capability probe: isAvailable() lazy-initializes the GL context
//   once on first call and caches the result. The capabilityProvider
//   callback (P8.2 stub API) is honored as an override for tests.

#pragma once

#include "RenderBackend.h"

#include <QMutex>
#include <QStringList>

class QOpenGLContext;
class QOpenGLFunctions;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOffscreenSurface;

namespace gpu {

class GpuBackend : public RenderBackend {
public:
    explicit GpuBackend(bool (*capabilityProvider)() = nullptr);
    ~GpuBackend() override;

    QString name() const override { return QStringLiteral("qt-opengl"); }
    bool    isAvailable() const override;

    cv::Mat processTile(const compositor::TileKey& key,
                        const cv::Mat& input,
                        RenderBackendContext& ctx) override;

    QStringList capabilities() const override;

private:
    // Lazy GL init. Returns true on success. Idempotent.
    bool ensureGL() const;

    // Release all GL resources. Called from dtor.
    void releaseGL();

    // External capability flag (test override). If non-null, takes
    // precedence over the real probe.
    bool (*m_capabilityProvider)() = nullptr;

    // Cached availability result. -1 = not probed, 0 = no, 1 = yes.
    mutable int  m_probed       = -1;

    // GL state (all owned via raw pointers; Qt objects, no parent
    // because they may be created on a different thread than the
    // dtor's thread).
    mutable QOpenGLContext*          m_context  = nullptr;
    mutable QOffscreenSurface*       m_surface  = nullptr;
    mutable QOpenGLFunctions*        m_functions = nullptr;
    mutable QOpenGLShaderProgram*    m_program  = nullptr;

    // Per-tile FBO. Created on first processTile to match input size;
    // resized if the next tile is a different size.
    mutable QOpenGLFramebufferObject* m_fbo      = nullptr;
    mutable int                      m_fboW     = 0;
    mutable int                      m_fboH     = 0;

    // Serializes all GL access across threads.
    mutable QMutex  m_lock;
};

} // namespace gpu