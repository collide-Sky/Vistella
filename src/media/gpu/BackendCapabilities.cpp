// SPDX-License-Identifier: MIT
//
// BackendCapabilities impl - P8.2 (2026-10-16) + P8.3 (2026-10-16)

#include "BackendCapabilities.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

namespace gpu {

BackendCapabilities& BackendCapabilities::instance()
{
    static BackendCapabilities s;
    return s;
}

QStringList BackendCapabilities::summary() const
{
    return {
        QStringLiteral("GPU available: ") + (m_gpuAvailable ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("Reason: ") + m_reason,
    };
}

void BackendCapabilities::setForced(bool gpuAvailable, const QString& reason)
{
    m_gpuAvailable = gpuAvailable;
    m_reason       = reason;
}

void BackendCapabilities::reset()
{
    m_gpuAvailable = false;
    m_reason       = QStringLiteral("no provider registered");
    m_provider     = nullptr;
}

void BackendCapabilities::setGpuFailed(const QString& reason)
{
    m_gpuAvailable = false;
    m_reason       = QStringLiteral("GPU failed: ") + reason;
}

void BackendCapabilities::setProvider(bool (*fn)())
{
    m_provider = fn;
    if (fn) {
        m_gpuAvailable = fn();
        m_reason = QStringLiteral("provider returned ") + (m_gpuAvailable ? "true" : "false");
    }
}

bool BackendCapabilities::probeOpenGL()
{
    // Headless probe: create a context, try to make it current on an
    // offscreen surface, verify the GL functions object is reachable.
    // If any step fails, the GPU backend is not usable.
    //
    // P8.3 MVP doesn't introspect the GL version; it just verifies a
    // context can be created + made current. Real version probing
    // (e.g. require 3.0+) and shader compile checks live in P8.2.x
    // when the QtGpuBackend shader pipeline lands.
    QOpenGLContext ctx;
    ctx.create();   // void; failure means ctx.isValid() returns false
    if (!ctx.isValid()) {
        return false;
    }
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();   // void
    if (!surface.isValid()) {
        return false;
    }
    if (!ctx.makeCurrent(&surface)) {
        return false;
    }
    QOpenGLFunctions* fn = ctx.functions();
    ctx.doneCurrent();
    return fn != nullptr;
}

} // namespace gpu
