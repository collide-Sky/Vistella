// SPDX-License-Identifier: MIT
//
// BackendCapabilities - P8.2 (2026-10-16) GPU capability detection
//   Probed once at startup. The result is cached for the session.
//   P8.2 MVP: capabilityProvider is a function pointer; production
//   wires a real QOpenGLContext probe in P8.2.x. The interface is
//   stable: callers ask isGpuAvailable() and get a bool.
//
//   Per-stroke re-probe (user拍板 2026-10-16): not done in P8.2
//   because the real probe is too heavy; the cached-at-startup
//   value is used. P8.2.x may add a lightweight re-probe path on
//   user-toggle from P8.3's settings panel.
//
//   GPU fail (driver crash / shader compile fail) handling: P8.2.x
//   will report failures via setGpuFailed() which flips the cached
//   state to false for the rest of the session (until restart).

#pragma once

#include <QString>
#include <QStringList>

namespace gpu {

class BackendCapabilities {
public:
    // Returns the global instance. Lazy-init on first call.
    static BackendCapabilities& instance();

    // True if a GPU backend is currently usable. Static so the
    // address is a plain function pointer (matches the GpuBackend
    // capabilityProvider signature).
    static bool isGpuAvailable() { return instance().m_gpuAvailable; }

    // Reason string for logs / status bar.
    QString reason() const { return m_reason; }

    // Capability list (for status bar / "About" dialog).
    QStringList summary() const;

    // For tests: inject a GPU-available flag without the real probe.
    // setForced(true) makes isGpuAvailable() return true; setForced(false)
    // makes it return false; setForced(boost::none) restores the
    // default (no provider / unavailable).
    void setForced(bool gpuAvailable, const QString& reason = QStringLiteral("forced"));

    // Reset to default (call once at startup to allow re-probe).
    void reset();

    // Mark GPU as permanently failed (e.g. shader compile crash). The
    // session will use CPU from now on. P8.2.x wires this in.
    void setGpuFailed(const QString& reason);

    // Read-only access to the active provider (so the registry can
    // wire the same provider into its GpuBackend instance).
    using ProviderFn = bool (*)();
    ProviderFn providerFn() const { return m_provider; }

    // Register the actual probe. P8.2.x replaces the stub.
    void setProvider(ProviderFn fn);

    // P8.3 (2026-10-16): real OpenGL probe via QOpenGLContext +
    // QOffscreenSurface. Returns true if OpenGL 2.0+ is available
    // and a context can be made current. Tested in tst_P8_3; the
    // result is environment-dependent (test runners may have no GPU
    // driver, in which case the probe returns false).
    static bool probeOpenGL();

private:
    BackendCapabilities() = default;

    bool        m_gpuAvailable = false;
    QString     m_reason       = QStringLiteral("no provider registered");
    ProviderFn  m_provider     = nullptr;
};

} // namespace gpu
