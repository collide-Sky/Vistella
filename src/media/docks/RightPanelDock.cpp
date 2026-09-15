// SPDX-License-Identifier: MIT
//
// RightPanelDock implementation - F-G.3 Fix (2026-09-10)
//
#include "RightPanelDock.h"
#include "ColorDock.h"
#include "PropertiesDock.h"
#include "LayersDock.h"
#include "ChannelPathPanel.h"
#include "HistoryDock.h"
#include "../imagewindow/AdjustmentPanel.h"
#include "../mediators/WorkspaceMediator.h"
#include "logger.h"

#include <QTabWidget>
#include <QVBoxLayout>

namespace docks {

RightPanelDock::RightPanelDock(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::North);
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs);

    // 颜色 (F-H)
    m_colorDock = new ColorDock(this);
    m_tabs->addTab(m_colorDock, tr("颜色"));

    // 属性 (F-I + P0-4.9 选区 bbox)
    m_propsDock = new PropertiesDock(this);
    m_tabs->addTab(m_propsDock, tr("属性"));

    // 图层 (F-G.2 + F-M.3)
    m_layersDock = new LayersDock(this);
    m_tabs->addTab(m_layersDock, tr("图层"));

    // 选区 - 通道/路径 (F-M.3 + P0-4 单独抽出)
    m_chanPathPanel = new ChannelPathPanel(this);
    m_tabs->addTab(m_chanPathPanel, tr("选区"));

    // P0-8.1 (2026-09-15): 历史 dock (PS 同款 history panel)
    m_historyDock = new HistoryDock(this);
    m_tabs->addTab(m_historyDock, tr("历史"));

    LOG_INFO("[RightPanelDock] created with 5 dock tabs (color/props/layers/sel/history)");
}

RightPanelDock::~RightPanelDock() = default;

void RightPanelDock::attach(mediators::WorkspaceMediator* wsMed)
{
    m_wsMed = wsMed;
    if (m_wsMed) {
        // Stage D (2026-09-15): 订阅信号实时同步, 之前只 setVisible 一次
        connect(m_wsMed, &mediators::WorkspaceMediator::workspaceChanged,
                this, &RightPanelDock::onWorkspaceChanged);
        connect(m_wsMed, &mediators::WorkspaceMediator::dockVisibilityChanged,
                this, &RightPanelDock::onDockVisibilityChanged);
        // 同步初始状态
        onWorkspaceChanged(m_wsMed->currentWorkspace());
    }
    LOG_DEBUG("[RightPanelDock] attach wsMed={}", wsMed ? "yes" : "null");
}

void RightPanelDock::onWorkspaceChanged(mediators::WorkspaceId id)
{
    if (!m_wsMed) return;
    LOG_INFO("[RightPanelDock] workspaceChanged: id={}", static_cast<int>(id));
    // WorkspaceMediator dockCount=3 (颜色/属性/图层), RightPanelDock 5 tab
    //   dockIndex 0 -> 颜色 tab
    //   dockIndex 1 -> 属性 tab
    //   dockIndex 2 -> 图层 tab
    //   选区/调整 永远显示 (跟 workspace 无关)
    int idxColor  = m_tabs->indexOf(m_colorDock);
    int idxProps  = m_tabs->indexOf(m_propsDock);
    int idxLayers = m_tabs->indexOf(m_layersDock);
    if (idxColor  >= 0) m_tabs->setTabVisible(idxColor,  m_wsMed->isDockVisible(0));
    if (idxProps  >= 0) m_tabs->setTabVisible(idxProps,  m_wsMed->isDockVisible(1));
    if (idxLayers >= 0) m_tabs->setTabVisible(idxLayers, m_wsMed->isDockVisible(2));
}

void RightPanelDock::onDockVisibilityChanged(int dockIndex, bool visible)
{
    if (!m_wsMed) return;
    QWidget* target = nullptr;
    if      (dockIndex == 0) target = m_colorDock;
    else if (dockIndex == 1) target = m_propsDock;
    else if (dockIndex == 2) target = m_layersDock;
    if (!target) return;
    int idx = m_tabs->indexOf(target);
    if (idx >= 0) m_tabs->setTabVisible(idx, visible);
}

void RightPanelDock::setAdjustmentPanel(AdjustmentPanel* adj)
{
    if (!adj) return;
    // 调整 tab 在 4 个 dock 之后 (5th tab)
    m_adjTabIndex = m_tabs->addTab(adj, tr("调整"));
    LOG_INFO("[RightPanelDock] adjustment panel attached as 5th tab (index={})", m_adjTabIndex);
}

} // namespace docks
