// SPDX-License-Identifier: MIT
//
// BackendCapabilities impl - P8.2 (2026-10-16)

#include "BackendCapabilities.h"

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

} // namespace gpu
