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

#include "../ColorState.h"

#include <QWidget>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <QPair>
#include "../mediators/ToolMediator.h"
#include "ToolContext.h"

class QToolButton;
namespace tools { class ToolGroupButton; }

namespace Ui { class LeftToolBar; }

namespace tools {

class ToolState;

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
    // M1-8: the colour lives in the document's ColorState.
    //   The toolbar is a VIEW: it renders what the state says and pushes
    //   user changes into it.
    QColor foregroundColor() const;
    QColor backgroundColor() const;

    // Bind this toolbar to a document's colour state (the active one).
    //   Binding again re-points the toolbar at the new state; the old
    //   connections are dropped.
    void setColorState(vistella::ColorState* state);
    vistella::ColorState* colorState() const { return m_colorState; }
    void   setForegroundColor(const QColor& c);
    void   setBackgroundColor(const QColor& c);
    void   swapColors();
    void   defaultColors();

signals:
    void foregroundColorChanged(const QColor& c);
    void backgroundColorChanged(const QColor& c);

private slots:
    // M1-8: update the swatch from the colour state WITHOUT pushing
    //   back into it (echo guard).
    void onForegroundColorChanged(const QColor& c);
    void onBackgroundColorChanged(const QColor& c);
    // ToolMediator 信号
    void onToolMediatorSwitched(mediators::ToolId id);
    // P7.1: fg/bg swatch interaction.
    void onFgSwatchClicked();
    void onBgSwatchClicked();
    void onSwapBtnClicked();
    void onFgSwatchDoubleClicked();
    void onBgSwatchDoubleClicked();

private:
    // The state the toolbar writes into. Always non-null: when no
    // document is bound, the toolbar falls back to its own state.
    vistella::ColorState* effectiveColorState();

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
    // M2-4: the factories for the tools that were missing.
    static ToolState* createEraserTool();
    static ToolState* createGradientTool();
    static ToolState* createPaintBucketTool();
    static ToolState* createDodgeBurnTool();
    static ToolState* createBlurSharpenSmudgeTool();
    static ToolState* createHandTool();
    static ToolState* createZoomTool();
    static ToolState* createRotateViewTool();

    // P7.1: paint fg/bg swatch widget.
    void paintColorSwatch(QWidget* w, const QColor& c);

    QHash<mediators::ToolId, QToolButton*> m_buttons;

    // M2-4: the tool groups.
    //
    //   Photoshop groups the tools: one button per group, the sibling
    //   tools come out of a long press. The audit
    //   (docs/CODE_AUDIT_2026-10-26.md 5.3 item 10) found that this
    //   interaction was missing entirely.
    //
    //   The group buttons are created in code (not in the .ui) because
    //   a group button has to know its siblings, and that changes
    //   whenever a tool is added.
    QVector<tools::ToolGroupButton*> m_groupButtons;
    // The group a ToolId belongs to (nullptr = the tool is not in a
    // group).
    QHash<int, tools::ToolGroupButton*> m_groupOfTool;

    void buildToolGroups();

public:
    // The group definition: the tools of each group, in the order the
    // popup shows them.
    //
    //   Public so that a test can check that every tool of a group is
    //   reachable through a group button.
    static QVector<QPair<QList<int>, QStringList>> toolGroupDefinitions();

private:

    // 文档级 weak ref: 换文档时 attach() 会重新绑定, 文档销毁后自动置 null,
    // 不会再拿着已析构 ImageWindow 的指针.
    QPointer<mediators::ToolMediator> m_toolMed;
    QPointer<ToolContext>              m_ctx;

    // P7.1: fg/bg state.
    // M1-8: the state this toolbar renders.
    //
    //   Normally this is the ImageWindow's ColorState (one per
    //   document). The toolbar is a VIEW: it renders the state and
    //   pushes user changes into it.
    //
    //   When no document is bound yet (the toolbar exists before the
    //   first image is opened, or in a unit test), the toolbar falls
    //   back to its OWN ColorState, owned by m_ownedColorState. That
    //   keeps the invariant "the toolbar always has exactly one
    //   colour state" -- without it the getters and the setters
    //   would disagree (the setter would emit, the getter would
    //   still report the hard-coded default).
    vistella::ColorState* m_colorState = nullptr;
    vistella::ColorState* m_ownedColorState = nullptr;

    Ui::LeftToolBar* ui = nullptr;  // ui auto-generated
};

} // namespace tools
