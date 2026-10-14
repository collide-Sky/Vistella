// SPDX-License-Identifier: MIT
//
// ToolContext implementation - F-C (2026-09-09)
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
    // P1.3.4 (2026-09-17): ToolContext listens to Mediator::toolSwitched so
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

void ToolContext::onKeyPress(QKeyEvent* e)
{
    if (m_current) m_current->onKeyPress(e, m_host.data());
}

void ToolContext::onKeyRelease(QKeyEvent* e)
{
    if (m_current) m_current->onKeyRelease(e, m_host.data());
}

// ===== P7.6 (2026-10-13) brushSharedState setters =====
//   P7.6.x (2026-10-14): Single setter writes + emits brushSettingsChanged. UI:
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

} // namespace tools