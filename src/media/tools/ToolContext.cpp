// SPDX-License-Identifier: MIT
//
// ToolContext implementation - F-C (2026-09-09)
//
#include "ToolContext.h"
#include "ToolState.h"
#include "logger.h"

#include "../imagewindow.h"

#include <QMouseEvent>
#include <QKeyEvent>

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
    // F-D stage: connect ToolMediator::toolSwitched -> setState
    //   (F-C doesn't wire this, LeftToolBar will call setState directly)
    (void)m_toolMed;
}

void ToolContext::detach()
{
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

} // namespace tools
