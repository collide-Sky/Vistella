// SPDX-License-Identifier: MIT
//
// LeftToolBar
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

    // P7.1: PS-style fg/bg double-click swap (event filter).
    bool eventFilter(QObject* watched, QEvent* event);

    // PS-style foreground / background color accessors
    //   fg/bg swatches (50x50 fg + 10x50 bg) live in the bottom strip of
    //   this dock and stay in lockstep with ColorDock. Click fg / bg to
    //   open QColorDialog; double-click triggers swap. Click the small
    //   "swapBtn" arrow to swap manually.
    QColor foregroundColor() const { return m_fgColor; }
    QColor backgroundColor() const { return m_bgColor; }
    void   setForegroundColor(const QColor& c);
    void   setBackgroundColor(const QColor& c);
    void   swapColors();
    void   defaultColors();

signals:
    void foregroundColorChanged(const QColor& c);
    void backgroundColorChanged(const QColor& c);

private slots:
    // ToolMediator 信号
    void onToolMediatorSwitched(mediators::ToolId id);
    // P7.1: fg/bg swatch interaction.
    void onFgSwatchClicked();
    void onBgSwatchClicked();
    void onSwapBtnClicked();
    void onFgSwatchDoubleClicked();
    void onBgSwatchDoubleClicked();

private:
    // 8 工具 factory (加 RectSelect/Lasso/MagicWand/Crop/Text/Brush)
    static ToolState* createMoveTool();
    static ToolState* createRectSelectTool();
    static ToolState* createLassoTool();
    static ToolState* createMagicWandTool();
    static ToolState* createCropTool();
    static ToolState* createTextTool();
    static ToolState* createBrushTool();
    static ToolState* createEyedropperTool();
    // 形状/矢量 + 修复工具 6 个 factory
    static ToolState* createShapeTool();
    static ToolState* createPenTool();
    static ToolState* createCloneTool();
    static ToolState* createHealTool();
    static ToolState* createPatchTool();
    static ToolState* createRedEyeTool();
    // MaskBrush factory (像素蒙版画笔)
    static ToolState* createMaskBrushTool();

    // P7.1: paint fg/bg swatch widget.
    void paintColorSwatch(QWidget* w, const QColor& c);

    QHash<mediators::ToolId, QToolButton*> m_buttons;

    mediators::ToolMediator* m_toolMed = nullptr;  // weak ref
    ToolContext*             m_ctx     = nullptr;  // weak ref

    // P7.1: fg/bg state.
    QColor m_fgColor = Qt::black;
    QColor m_bgColor = Qt::white;

    Ui::LeftToolBar* ui = nullptr;  // ui auto-generated
};

} // namespace tools
