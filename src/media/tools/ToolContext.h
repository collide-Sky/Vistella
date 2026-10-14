// SPDX-License-Identifier: MIT
//
// ToolContext - F-C (2026-09-09)
//
// F-C stage only: holds current ToolState + forwards events to it.
// Mediator integration (signal-driven setState) is F-D stage.
//
#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

class QMouseEvent;
class QKeyEvent;
class QPointF;
class QWidget;
class ImageWindow;

#include "../mediators/ToolMediator.h"

namespace tools {

// P7.6 (2026-10-13): shared brush state. P7.6.x (2026-10-14) UI: BrushOptionsPanel
//   (ImageOptionBar idx=7) writes via ImageOptionBar::onBrushSharedStateChangedFromPanel,
//   Brush/Clone/Heal/Patch/MaskBrush read via brushSettingsChanged.
//   Not nested (moc does not like nested struct). Top-level namespace.
//   P7.9 commit extends (12 dynamic panel slots) and tools apply() sync.
struct BrushSharedState {
    // PS brush mode starter 3 items; P7.9 commit extends to 9
    int     blendMode = 0;     // 0=Normal
    double  opacity   = 100.0; // 0..100 %
    double  flow      = 100.0; // 0..100 %
    double  smoothing = 10.0;  // 0..100 %; P7.9 commit changes to 0
    int     angle     = 0;     // 0..359 deg
    int     brushSize = 13;    // 1..2000 px; PS default 13
};

class ToolState;

class ToolContext : public QObject
{
    Q_OBJECT
public:
    explicit ToolContext(QObject* parent = nullptr);
    ~ToolContext() override;

    // Wiring (called after ImageWindow is constructed)
    void attach(ImageWindow* host, mediators::ToolMediator* toolMed);
    void detach();

    // Current tool
    mediators::ToolId currentToolId() const;
    ToolState* currentState() const { return m_current.get(); }

    // Switch tool (main entry, takes ownership via std::unique_ptr)
    void setState(std::unique_ptr<ToolState> state);

    // Event forwarding (ImageWindow eventFilter calls these)
    //   host is forwarded to ToolState, lets tool access image/layer/undo
    void onMousePress(QMouseEvent* e, const QPointF& scenePos);
    void onMouseMove(QMouseEvent* e, const QPointF& scenePos);
    void onMouseRelease(QMouseEvent* e, const QPointF& scenePos);
    void onKeyPress(QKeyEvent* e);
    void onKeyRelease(QKeyEvent* e);

    // P7.6 (2026-10-13): shared brush state accessor (BrushOptionsPanel via
    //   ImageOptionBar reads/writes). P7.6.x UI.
    BrushSharedState& brushSharedState() { return m_brushShared; }
    const BrushSharedState& brushSharedState() const { return m_brushShared; }
    // Single setter path with refresh signal — BrushOptionsPanel / tools can set shared.
    void setBlendMode(int mode);
    void setOpacity(double pct);
    void setFlow(double pct);
    void setSmoothing(double pct);
    void setAngle(int deg);
    void setBrushSize(int px);

signals:
    // Subscribers listen:
    //   ImageCanvas: change cursor
    //   ImageOptionBar: switch stack page
    void toolChanged(mediators::ToolId newId);
    // Current tool's cursor changed (e.g. internal state change)
    void cursorChanged(const QCursor& cursor);

    // P7.6 (2026-10-13): brushSettingsChanged — any of opacity/flow/angle/size/blendMode change emits
    //   signal. P7.6.x (2026-10-14) UI: subscribers are ImageOptionBar (forwards to
    //   BrushOptionsPanel::applySharedState) + future brush tools (P7.9 commit).
    void brushSettingsChanged();

private:
    // Do NOT own host / toolMed (weak ref only)
    QPointer<ImageWindow>           m_host;
    mediators::ToolMediator*         m_toolMed = nullptr;  // weak ref (F-D wires Mediator listener)

    // Own current tool
    std::unique_ptr<ToolState>       m_current;

    // P7.6 (2026-10-13): shared brush state (mode/opacity/flow/smoothing/angle/brushSize).
    //   P7.6.x (2026-10-14) UI writes via BrushOptionsPanel (ImageOptionBar).
    BrushSharedState                 m_brushShared;

    // Initial default tool (None state, no event response)
    void installDefaultState();
};

} // namespace tools