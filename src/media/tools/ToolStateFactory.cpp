#include "ToolStateFactory.h"

#include "ToolState.h"
#include "MoveTool.h"
#include "RectSelect.h"
#include "Lasso.h"
#include "MagicWand.h"
#include "Crop.h"
#include "Text.h"
#include "Brush.h"
#include "EyedropperTool.h"
#include "ShapeTool.h"
#include "PenTool.h"
#include "CloneTool.h"
#include "HealTool.h"
#include "PatchTool.h"
#include "RedEyeTool.h"
#include "MaskBrushTool.h"

namespace tools {

ToolState* createToolState(mediators::ToolId id)
{
    switch (id) {
    case mediators::ToolId::Move:       return new MoveTool();
    case mediators::ToolId::RectSelect: return new RectSelect();
    case mediators::ToolId::Lasso:      return new Lasso();
    case mediators::ToolId::MagicWand:  return new MagicWand();
    case mediators::ToolId::Crop:       return new Crop();
    case mediators::ToolId::Text:       return new Text();
    case mediators::ToolId::Brush:      return new Brush();
    case mediators::ToolId::Eyedropper: return new EyedropperTool();
    case mediators::ToolId::Shape:      return new ShapeTool();
    case mediators::ToolId::Pen:        return new PenTool();
    case mediators::ToolId::Clone:      return new CloneTool();
    case mediators::ToolId::Heal:       return new HealTool();
    case mediators::ToolId::Patch:      return new PatchTool();
    case mediators::ToolId::RedEye:     return new RedEyeTool();
    case mediators::ToolId::MaskBrush:  return new MaskBrushTool();
    case mediators::ToolId::None:
    case mediators::ToolId::Transform:
    default:                            return nullptr;
    }
}

} // namespace tools