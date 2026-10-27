// SPDX-License-Identifier: MIT
//
// ToolContext implementation
//
#include "ToolContext.h"
#include "ToolState.h"
#include "ToolStateFactory.h"
#include "logger.h"

#include "../imagewindow.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <memory>

namespace tools {

ToolContext::ToolContext(QObject* parent) : QObject(parent)
{
    installDefaultState();
}

ToolContext::~ToolContext()
{
    if (m_current && m_host) {
        m_current->onExit(m_host.data());
    }
}

void ToolContext::installDefaultState()
{
    // F-C stage: default None state (no-op)
    //   F-L will add explicit NoneTool
    m_current = nullptr;
}

void ToolContext::attach(ImageWindow* host, mediators::ToolMediator* toolMed)
{
    m_host = host;
    m_toolMed = toolMed;
    // ToolContext listens to Mediator::toolSwitched so
    //   menu / shortcut paths can switch tools without LeftToolBar being
    //   present in the UI. Both LeftToolBar and ToolContext now call
    //   setState; LeftToolBar keeps its button highlight sync.
    if (m_toolMed) {
        connect(m_toolMed, &mediators::ToolMediator::toolSwitched,
                this, [this](mediators::ToolId id) {
            auto* st = createToolState(id);
            setState(std::unique_ptr<ToolState>(st));
        });
    }
}

void ToolContext::detach()
{
    if (m_toolMed) {
        disconnect(m_toolMed, nullptr, this, nullptr);
    }
    m_host = nullptr;
    m_toolMed = nullptr;
}

mediators::ToolId ToolContext::currentToolId() const
{
    return m_current ? m_current->id() : mediators::ToolId::None;
}

void ToolContext::setState(std::unique_ptr<ToolState> state)
{
    // 1. old tool exits (always, even without host)
    if (m_current) {
        m_current->onExit(m_host.data());
    }
    // 2. replace
    m_current = std::move(state);
    // 3. new tool enters
    //   F-C design: always call onEnter (passing nullptr if no host)
    //   This lets state-only tools work without host, and host-dependent
    //   tools can check for nullptr inside onEnter
    if (m_current) {
        m_current->onEnter(m_host.data());
        emit cursorChanged(m_current->cursor());
    } else {
        emit cursorChanged(QCursor(Qt::ArrowCursor));
    }
    LOG_INFO("[ToolCtx] setState: id={} host={}", static_cast<int>(currentToolId()), m_host ? "yes" : "null");
    // 4. notify subscribers
    emit toolChanged(currentToolId());
}

void ToolContext::onMousePress(QMouseEvent* e, const QPointF& scenePos)
{
    if (m_current) m_current->onMousePress(e, m_host.data(), scenePos);
}

void ToolContext::onMouseMove(QMouseEvent* e, const QPointF& scenePos)
{
    if (m_current) m_current->onMouseMove(e, m_host.data(), scenePos);
}

void ToolContext::onMouseRelease(QMouseEvent* e, const QPointF& scenePos)
{
    if (m_current) m_current->onMouseRelease(e, m_host.data(), scenePos);
}

// TODO(P8.5): the tool keyboard handlers have no caller, so polygon closure, pen commit and Esc cancel do not work -- see docs/DEBT.md#A-KEYBOARD
void ToolContext::onKeyPress(QKeyEvent* e)
{
    if (m_current) m_current->onKeyPress(e, m_host.data());
}

void ToolContext::onKeyRelease(QKeyEvent* e)
{
    if (m_current) m_current->onKeyRelease(e, m_host.data());
}

// ===== brushSharedState setters =====
//   Single setter writes + emits brushSettingsChanged. UI
//     BrushOptionsPanel (ImageOptionBar idx=7) value change goes through these 6
//     setters via ImageOptionBar::onBrushSharedStateChangedFromPanel. Reverse
//     direction: ToolContext set (load file / preset switch / future tools) ->
//     brushSettingsChanged -> ImageOptionBar::onToolContextBrushStateChanged ->
//     BrushOptionsPanel::applySharedState.
//   P7.9 stage: current active brush tool listens to brushSettingsChanged for sync.
//
//   P7.6 stage: tool read path goes through setBrushSize -> Brush::setBrushSize + sync
//   in Brush::onEnter (subsequent commit also connects 4 brush tools). 5 brush tools
//   done at P7.6 commit end. opacity/flow/smoothing/angle/blendMode temporarily only
//   touch ToolContext.m_brushShared (signal emitted), tool apply() path P7.9.
void ToolContext::setBlendMode(int mode)
{
    if (m_brushShared.blendMode == mode) return;
    m_brushShared.blendMode = mode;
    emit brushSettingsChanged();
}

void ToolContext::setOpacity(double pct)
{
    if (qFuzzyCompare(m_brushShared.opacity, pct)) return;
    m_brushShared.opacity = pct;
    emit brushSettingsChanged();
}

void ToolContext::setFlow(double pct)
{
    if (qFuzzyCompare(m_brushShared.flow, pct)) return;
    m_brushShared.flow = pct;
    emit brushSettingsChanged();
}

void ToolContext::setSmoothing(double pct)
{
    if (qFuzzyCompare(m_brushShared.smoothing, pct)) return;
    m_brushShared.smoothing = pct;
    emit brushSettingsChanged();
}

void ToolContext::setAngle(int deg)
{
    if (m_brushShared.angle == deg) return;
    m_brushShared.angle = deg;
    emit brushSettingsChanged();
}

void ToolContext::setBrushSize(int px)
{
    if (m_brushShared.brushSize == px) return;
    m_brushShared.brushSize = px;
    emit brushSettingsChanged();
}

// 3 new shared brush state setters
//   (hardness / spacing / scatter). Same fuzzy-compare + signal pattern.
void ToolContext::setHardness(double pct)
{
    if (qFuzzyCompare(m_brushShared.hardness, pct)) return;
    m_brushShared.hardness = pct;
    emit brushSettingsChanged();
}

void ToolContext::setSpacing(double pct)
{
    if (qFuzzyCompare(m_brushShared.spacing, pct)) return;
    m_brushShared.spacing = pct;
    emit brushSettingsChanged();
}

void ToolContext::setScatter(double pct)
{
    if (qFuzzyCompare(m_brushShared.scatter, pct)) return;
    m_brushShared.scatter = pct;
    emit brushSettingsChanged();
}

// P7.9.1: bulk update from BrushOptionsPanel (1 signal instead of 9).
void ToolContext::setBrushSharedState(const BrushSharedState& s)
{
    bool changed = false;
    if (m_brushShared.blendMode != s.blendMode)  { m_brushShared.blendMode = s.blendMode;  changed = true; }
    if (!qFuzzyCompare(m_brushShared.opacity,   s.opacity))   { m_brushShared.opacity   = s.opacity;   changed = true; }
    if (!qFuzzyCompare(m_brushShared.flow,      s.flow))      { m_brushShared.flow      = s.flow;      changed = true; }
    if (!qFuzzyCompare(m_brushShared.smoothing, s.smoothing)) { m_brushShared.smoothing = s.smoothing; changed = true; }
    if (m_brushShared.angle != s.angle)            { m_brushShared.angle     = s.angle;     changed = true; }
    if (m_brushShared.brushSize != s.brushSize)    { m_brushShared.brushSize = s.brushSize; changed = true; }
    if (!qFuzzyCompare(m_brushShared.hardness,  s.hardness))  { m_brushShared.hardness  = s.hardness;  changed = true; }
    if (!qFuzzyCompare(m_brushShared.spacing,   s.spacing))   { m_brushShared.spacing   = s.spacing;   changed = true; }
    if (!qFuzzyCompare(m_brushShared.scatter,   s.scatter))   { m_brushShared.scatter   = s.scatter;   changed = true; }
    if (changed) emit brushSettingsChanged();
}

} // namespace tools