// SPDX-License-Identifier: MIT
//
// BackendRegistry impl - P8.2 (2026-10-16)

#include "BackendRegistry.h"

#include "BackendCapabilities.h"
#include "CpuBackend.h"
#include "GpuBackend.h"
#include "RenderBackend.h"

namespace gpu {

BackendRegistry::BackendRegistry()
    : m_cpu(new CpuBackend())
    , m_gpu(new GpuBackend(&BackendCapabilities::isGpuAvailable))
{
}

BackendRegistry& BackendRegistry::instance()
{
    static BackendRegistry s;
    return s;
}

BackendRegistry::~BackendRegistry()
{
    delete m_cpu;
    delete m_gpu;
}

RenderBackend* BackendRegistry::pick(brushes::BrushKind brushKind,
                                     const tools::BrushSharedState& sharedState) const
{
    // P8.3 mode (replaces the P8.2 binary forceCpu):
    //   ForceCpu -> always CPU
    //   ForceGpu -> always prefer GPU (fall back to CPU if unavailable)
    //   Auto     -> per-kind override / kindNeedsGpu()
    if (m_mode == RenderMode::ForceCpu) return m_cpu;

    if (m_mode == RenderMode::ForceGpu) {
        return m_gpu->isAvailable() ? m_gpu : m_cpu;
    }

    // Auto: per-kind override takes precedence over kindNeedsGpu.
    const int idx = static_cast<int>(brushKind);
    if (idx >= 0 && idx < 13) {
        if (m_kindOverride[idx] == 1) {
            return m_gpu->isAvailable() ? m_gpu : m_cpu;
        }
        if (m_kindOverride[idx] == 2) {
            return m_cpu;
        }
    }

    const bool wantsGpu = brushes::kindNeedsGpu(brushKind, sharedState);
    if (wantsGpu) {
        return m_gpu->isAvailable() ? m_gpu : m_cpu;
    }
    return m_cpu;
}

void BackendRegistry::setMode(RenderMode m)
{
    m_mode = m;
}

void BackendRegistry::setForceCpu(bool force)
{
    m_mode = force ? RenderMode::ForceCpu : RenderMode::Auto;
}

void BackendRegistry::setKindOverride(brushes::BrushKind kind, bool preferGpu)
{
    // preferGpu=true  -> m_kindOverride[idx] = 1 (prefer GPU, fall back to CPU)
    // preferGpu=false -> m_kindOverride[idx] = 2 (force CPU)
    const int idx = static_cast<int>(kind);
    if (idx >= 0 && idx < 13) {
        m_kindOverride[idx] = preferGpu ? 1 : 2;
    }
}

QString BackendRegistry::describeDecision(brushes::BrushKind brushKind,
                                          const tools::BrushSharedState& ss) const
{
    RenderBackend* b = pick(brushKind, ss);
    return QString::fromLatin1(brushes::categoryName(brushes::categoryOf(brushKind)))
         + QStringLiteral(" - ")
         + QString::fromLatin1(brushes::kindName(brushKind).toUtf8().constData())
         + QStringLiteral(" -> ")
         + b->name();
}

QString BackendRegistry::currentBackendName(brushes::BrushKind brushKind,
                                            const tools::BrushSharedState& ss) const
{
    return pick(brushKind, ss)->name();
}

QString BackendRegistry::modeLabel(RenderMode m)
{
    switch (m) {
        case RenderMode::Auto:     return QStringLiteral("AUTO");
        case RenderMode::ForceGpu: return QStringLiteral("FORCE GPU");
        case RenderMode::ForceCpu: return QStringLiteral("FORCE CPU");
    }
    return QStringLiteral("AUTO");
}

} // namespace gpu
