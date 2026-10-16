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

BackendRegistry::~BackendRegistry()
{
    delete m_cpu;
    delete m_gpu;
}

RenderBackend* BackendRegistry::pick(brushes::BrushKind brushKind,
                                     const tools::BrushSharedState& sharedState) const
{
    // Forced CPU wins.
    if (m_forceCpu) return m_cpu;

    // Per-kind override: a set value is authoritative.
    const int idx = static_cast<int>(brushKind);
    if (idx >= 0 && idx < 13) {
        if (m_kindOverride[idx] == 1) {
            // Prefer GPU; if not available, fall back to CPU.
            return m_gpu->isAvailable() ? m_gpu : m_cpu;
        }
        if (m_kindOverride[idx] == 2) {
            return m_cpu;     // force CPU
        }
    }

    // Otherwise consult kindNeedsGpu() (P7.9.2). Wet 3 always say
    // true; Special 2 always say false; Basic 8 defer to sharedState.
    const bool wantsGpu = brushes::kindNeedsGpu(brushKind, sharedState);
    if (wantsGpu) {
        return m_gpu->isAvailable() ? m_gpu : m_cpu;
    }
    return m_cpu;
}

void BackendRegistry::setForceCpu(bool force)
{
    m_forceCpu = force;
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

} // namespace gpu
