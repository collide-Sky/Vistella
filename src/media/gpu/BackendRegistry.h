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

// P8.3 (2026-10-16): 3-state mode for user control. Replaces P8.2's
// binary setForceCpu(bool). P8.3 wires this to the status bar menu
// and the settings dialog GPU page.
enum class RenderMode {
    Auto      = 0,   // registry decides per kind
    ForceGpu  = 1,   // user override: prefer GPU everywhere
    ForceCpu  = 2,   // user override: force CPU everywhere
};

class BackendRegistry {
public:
    // P8.3 (2026-10-16): global registry instance + static accessors.
    // The main status bar / settings dialog use the global instance
    // because there's only ever one process-wide backend choice.
    static BackendRegistry& instance();
    static void setModeStatic(RenderMode m) { instance().setMode(m); }
    static RenderMode modeStatic() { return instance().mode(); }

    BackendRegistry();
    ~BackendRegistry();

    // Per-tile selection. Always non-null.
    //   brushKind: P7.9.2 13-kinds taxonomy.
    //   sharedState: P7.9.1 9-field state. Used only when kind is Basic
    //                and we need the per-state needsGpu() check.
    //   Returns Cpu or Gpu (or the registered future backend).
    RenderBackend* pick(brushes::BrushKind brushKind,
                        const tools::BrushSharedState& sharedState) const;

    // P8.3 (2026-10-16): 3-state mode. Auto = registry decides.
    // ForceGpu = override kind decisions, always prefer GPU.
    // ForceCpu = override kind decisions, always CPU.
    void setMode(RenderMode m);
    RenderMode mode() const { return m_mode; }

    // P8.2 legacy: binary force CPU. Kept for backward compat; maps to
    // ForceCpu (true) or Auto (false).
    void setForceCpu(bool force);
    bool forceCpu() const { return m_mode == RenderMode::ForceCpu; }

    // Test/override hook: per-kind override. e.g. "all Wet kinds must
    // use CPU even if GPU is available". Default: empty (no override).
    void setKindOverride(brushes::BrushKind kind, bool useGpu);

    QString describeDecision(brushes::BrushKind brushKind,
                             const tools::BrushSharedState& sharedState) const;

    // P8.3 helper for status bar / status text. Picks the backend name
    // for a sample (kind, sharedState) under the current mode +
    // capability state. Useful for the status bar GPU button.
    QString currentBackendName(brushes::BrushKind brushKind,
                               const tools::BrushSharedState& sharedState) const;

    // Mode label for status bar (e.g. "AUTO" / "FORCE GPU" / "FORCE CPU").
    static QString modeLabel(RenderMode m);

    // Accessors (for tests + status bar).
    RenderBackend* cpuBackend() const { return m_cpu; }
    RenderBackend* gpuBackend() const { return m_gpu; }

private:
    // Owned. Always non-null after construction.
    RenderBackend* m_cpu = nullptr;
    RenderBackend* m_gpu = nullptr;
    RenderMode     m_mode = RenderMode::Auto;
    // Per-kind override: 0 = no override (registry decides), 1 = prefer
    // GPU (falls back to CPU if unavailable), 2 = force CPU.
    // Indexed by static_cast<int>(BrushKind).
    int            m_kindOverride[13] = {};
};

} // namespace gpu
