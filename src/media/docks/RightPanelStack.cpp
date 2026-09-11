// SPDX-License-Identifier: MIT
//
// RightPanelStack implementation - F-G (2026-09-09)
//
#include "RightPanelStack.h"
#include "ColorDock.h"
#include "PropertiesDock.h"
#include "LayersDock.h"
#include "logger.h"

#include <QVBoxLayout>

namespace docks {

RightPanelStack::RightPanelStack(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(4);

    m_colorDock  = new ColorDock(this);
    m_propsDock  = new PropertiesDock(this);
    m_layersDock = new LayersDock(this);

    m_layout->addWidget(m_colorDock);
    m_layout->addWidget(m_propsDock);
    m_layout->addWidget(m_layersDock);
    m_layout->addStretch(1);
}

RightPanelStack::~RightPanelStack() = default;

void RightPanelStack::attach(mediators::WorkspaceMediator* wsMed)
{
    m_wsMed = wsMed;
    if (m_wsMed) {
        connect(m_wsMed, &mediators::WorkspaceMediator::workspaceChanged,
                this, &RightPanelStack::onWorkspaceChanged);
        // 同步初始状态
        onWorkspaceChanged(m_wsMed->currentWorkspace());
    }
}

void RightPanelStack::detach()
{
    if (m_wsMed) {
        disconnect(m_wsMed, nullptr, this, nullptr);
    }
    m_wsMed = nullptr;
}

void RightPanelStack::onWorkspaceChanged(mediators::WorkspaceId id)
{
    if (!m_wsMed) return;
    LOG_INFO("[RightPanel] workspaceChanged: id={}", static_cast<int>(id));
    // F-G.1 阶段: 仅根据 dockVisible 显隐 dock widget
    //   F-G.2 阶段: 同时重建 dock 内部 tab 列表
    if (m_colorDock)  m_colorDock->setVisible(m_wsMed->isDockVisible(0));
    if (m_propsDock)  m_propsDock->setVisible(m_wsMed->isDockVisible(1));
    if (m_layersDock) m_layersDock->setVisible(m_wsMed->isDockVisible(2));
}

} // namespace docks
