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
    // WorkspaceMediator 走 RightPanelStack::attach 模式, 转发到 3 个 dock
    // (颜色/属性/图层 是 workspace 切换的 dock)
    if (m_colorDock)  m_colorDock->setVisible(wsMed ? wsMed->isDockVisible(0) : true);
    if (m_propsDock)  m_propsDock->setVisible(wsMed ? wsMed->isDockVisible(1) : true);
    if (m_layersDock) m_layersDock->setVisible(wsMed ? wsMed->isDockVisible(2) : true);
    // ChannelPathPanel 不跟 workspace 切换 (永远显示)
    LOG_DEBUG("[RightPanelDock] attach wsMed={}", wsMed ? "yes" : "null");
}

void RightPanelDock::setAdjustmentPanel(AdjustmentPanel* adj)
{
    if (!adj) return;
    // 调整 tab 在 4 个 dock 之后 (5th tab)
    m_tabs->addTab(adj, tr("调整"));
    LOG_INFO("[RightPanelDock] adjustment panel attached as 5th tab");
}

} // namespace docks
