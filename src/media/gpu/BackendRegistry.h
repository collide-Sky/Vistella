// SPDX-License-Identifier: MIT
//
// BackendRegistry - P8.2 (2026-10-16) per-tile backend selection
//   pick(brushKind, sharedState) consults kindNeedsGpu() (P7.9.2) +
//   the active BackendCapabilities + the optional user override and
//   returns a RenderBackend* (always non-null: the registry owns
//   CpuBackend and GpuBackend and returns one of them).
//
//   Routing rules (user拍板 2026-10-16):
//     - Wet 3 (Watercolor/Oil/Acrylic)         -> Gpu if available, else Cpu
//     - Special 2 (Smudge/Eraser)             -> Cpu (always)
//     - Basic 8                               -> defers to sharedState.needsGpu()
//                                                (which itself uses the per-state
//                                                smoothing/scatter/blendMode rules
//                                                from P7.9.1)
//     - User override (setForced)             -> Cpu
//   Future backends (D3D12, Metal, Vulkan) plug in by adding to
//   the registry constructor; pick() doesn't change.

#pragma once

#include <QString>

#include "../brushes/BrushKind.h"   // BrushKind, BrushCategory
#include "../tools/ToolContext.h"   // BrushSharedState

namespace gpu {

class RenderBackend;

class BackendRegistry {
public:
    BackendRegistry();
    ~BackendRegistry();

    // Per-tile selection. Always non-null.
    //   brushKind: P7.9.2 13-kinds taxonomy.
    //   sharedState: P7.9.1 9-field state. Used only when kind is Basic
    //                and we need the per-state needsGpu() check.
    //   Returns Cpu or Gpu (or the registered future backend).
    RenderBackend* pick(brushes::BrushKind brushKind,
                        const tools::BrushSharedState& sharedState) const;

    // Test/override hook: force CPU for the rest of the session.
    // P8.3 will wire this to the user setting.
    void setForceCpu(bool force);

    // Test/override hook: per-kind override. e.g. "all Wet kinds must
    // use CPU even if GPU is available". Default: empty (no override).
    void setKindOverride(brushes::BrushKind kind, bool useGpu);

    QString describeDecision(brushes::BrushKind brushKind,
                             const tools::BrushSharedState& sharedState) const;

    // Accessors (for tests + status bar).
    RenderBackend* cpuBackend() const { return m_cpu; }
    RenderBackend* gpuBackend() const { return m_gpu; }

private:
    // Owned. Always non-null after construction.
    RenderBackend* m_cpu = nullptr;
    RenderBackend* m_gpu = nullptr;
    bool           m_forceCpu = false;
    // Per-kind override: 0 = no override (registry decides), 1 = prefer
    // GPU (falls back to CPU if unavailable), 2 = force CPU.
    // Indexed by static_cast<int>(BrushKind).
    int            m_kindOverride[13] = {};
};

} // namespace gpu
