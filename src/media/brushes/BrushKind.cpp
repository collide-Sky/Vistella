// SPDX-License-Identifier: MIT
//
// BrushKind impl - 13 brush dynamic types
//   kindNeedsGpu() needs the full tools::BrushSharedState definition to
//   call its needsGpu() member. BrushKind.h only forward-decls it to avoid
//   pulling the whole tools/ subdir into brushes/. This .cpp does the
//   single cross-namespace work.
//

#include "BrushKind.h"

#include "../tools/ToolContext.h"   // full definition of tools::BrushSharedState

namespace brushes {

bool kindNeedsGpu(BrushKind k, const tools::BrushSharedState& ss)
{
    const BrushCategory cat = categoryOf(k);
    if (cat == BrushCategory::Wet)     return true;
    if (cat == BrushCategory::Special) return false;
    // Basic 8: defer to sharedState (Basic brushes are cheap unless the
    // user cranks smoothing/scatter/blendMode beyond the per-state threshold).
    return ss.needsGpu();
}

} // namespace brushes
