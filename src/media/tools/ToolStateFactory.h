#pragma once

#include "mediators/ToolMediator.h"

namespace tools {

class ToolState;

// Factory: single source of truth for ToolId -> ToolState subclass.
//
//   LeftToolBar's onToolMediatorSwitched and ToolContext's
//   onToolMediatorSwitched both call this. Centralising avoids the
//   duplicate switch + accidental drift when adding new tools.
ToolState* createToolState(mediators::ToolId id);

} // namespace tools