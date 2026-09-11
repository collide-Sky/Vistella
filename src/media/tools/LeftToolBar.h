// SPDX-License-Identifier: MIT
//
// LeftToolBar - F-D (2026-09-09)
//
// PS-style vertical toolbar on the left of canvas.
//   F-D stage: 2 tools (Move + Eyedropper) in 2-column grid
//   F-L stage: extend to 8 tools
//
// Layout:
//   +---------+
//   | [M] [I] |  (M=Move, I=Eyedropper, 2 cols)
//   +---------+
//
// Wiring (F-D does NOT touch main window state machine):
//   attach(toolMed, ctx):
//     LeftToolBar button click -> toolMed->switchTool(id)
//     toolMed->toolSwitched -> button setChecked (highlight active)
//     ctx->setState(new ToolState subclass) (called via Mediator signal)
//
#pragma once

#include <QWidget>
#include <QHash>
#include "../mediators/ToolMediator.h"

class QToolButton;

namespace Ui { class LeftToolBar; }

namespace tools {

class ToolState;
class ToolContext;

class LeftToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit LeftToolBar(QWidget* parent = nullptr);
    ~LeftToolBar() override;

    // Wiring (called after ImageWindow ctor)
    void attach(mediators::ToolMediator* toolMed, ToolContext* ctx);
    void detach();

    // Disable a tool (e.g. when no image loaded)
    void setToolEnabled(mediators::ToolId id, bool enabled);

private slots:
    // ToolMediator 信号
    void onToolMediatorSwitched(mediators::ToolId id);

private:
    // 8 工具 factory (F-L 2026-09-10: 加 RectSelect/Lasso/MagicWand/Crop/Text/Brush)
    static ToolState* createMoveTool();
    static ToolState* createRectSelectTool();
    static ToolState* createLassoTool();
    static ToolState* createMagicWandTool();
    static ToolState* createCropTool();
    static ToolState* createTextTool();
    static ToolState* createBrushTool();
    static ToolState* createEyedropperTool();

    QHash<mediators::ToolId, QToolButton*> m_buttons;

    mediators::ToolMediator* m_toolMed = nullptr;  // weak ref
    ToolContext*             m_ctx     = nullptr;  // weak ref

    Ui::LeftToolBar* ui = nullptr;  // .ui auto-generated (F-P.3 2026-09-09)
};

} // namespace tools
