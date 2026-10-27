// VRenderPath.h
//
// Render path preference for a VTabPageBase.
//   placeholders for GPU backend abstraction
//   Actual GPU/CPU switching lives in P8.2; here we only declare the enum so
//   downstream code (VTabPageBase, RightPanelDock, status bar) can already
//   report / query the preference without changing signatures later.
//
//   - Auto          : caller asks the capability layer; default behaviour.
//   - CpuOnly       : hard-require CPU path; page refuses to run if forced GPU.
//   - GpuPreferred  : prefer GPU; capability layer may still downgrade to CPU
//                     when the device reports no D3D12/Metal/Vulkan support.
#pragma once

namespace vistella::ui {

enum class RenderPath {
    Auto          = 0,
    CpuOnly       = 1,
    GpuPreferred  = 2,
};

} // namespace vistella::ui
