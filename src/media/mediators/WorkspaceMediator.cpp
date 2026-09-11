// SPDX-License-Identifier: MIT
//
// WorkspaceMediator 实现 - F-B2 (2026-09-08)
//
#include "WorkspaceMediator.h"
#include "logger.h"

namespace mediators {

WorkspaceMediator::WorkspaceMediator(QObject* parent) : QObject(parent)
{
    buildDefaultWorkspaces();
    // F-B2 (2026-09-08) 修正: 从当前工作区 DockConfig.visible 初始化
    //   之前全部填 true → Photo/Paint 的 dock 2 (visible=false) 被强制覆盖为 true
    //   现在按 DockConfig 显隐配置填, 切工作区时也按这个重置
    const auto docks = m_workspaces.value(m_current);
    m_dockVisible.resize(docks.size());
    for (int i = 0; i < docks.size(); ++i) {
        m_dockVisible[i] = docks[i].visible;
    }
}

void WorkspaceMediator::buildDefaultWorkspaces()
{
    // ---- Basic (基本功能) — 3 dock 全开 ----
    m_workspaces[WorkspaceId::Basic] = {
        // Dock 1: 颜色
        {QStringLiteral("颜色"), {QStringLiteral("颜色"), QStringLiteral("色板"),
                                  QStringLiteral("渐变"),  QStringLiteral("图案")}, true},
        // Dock 2: 属性
        {QStringLiteral("属性"), {QStringLiteral("属性"), QStringLiteral("调整")}, true},
        // Dock 3: 图层
        {QStringLiteral("图层"), {QStringLiteral("图层"), QStringLiteral("通道"),
                                  QStringLiteral("路径")}, true},
    };
    // ---- Photo (摄影) — 2 dock, 隐藏 dock 3 ----
    m_workspaces[WorkspaceId::Photo] = {
        {QStringLiteral("颜色"), {QStringLiteral("颜色"), QStringLiteral("色板")}, true},
        {QStringLiteral("属性"), {QStringLiteral("属性"), QStringLiteral("直方图")}, true},
        // dock 3 隐藏 (title 空, tabs 空, visible=false)
        {QString(), {}, false},
    };
    // ---- Paint (绘画) — 2 dock ----
    m_workspaces[WorkspaceId::Paint] = {
        {QStringLiteral("颜色"), {QStringLiteral("颜色"), QStringLiteral("色板"),
                                  QStringLiteral("画笔")}, true},
        {QStringLiteral("属性"), {QStringLiteral("属性"), QStringLiteral("调整")}, true},
        {QString(), {}, false},
    };
    // ---- Web (图形和 Web) — 3 dock ----
    m_workspaces[WorkspaceId::Web] = {
        {QStringLiteral("颜色"), {QStringLiteral("颜色"), QStringLiteral("色板")}, true},
        {QStringLiteral("属性"), {QStringLiteral("属性"), QStringLiteral("调整")}, true},
        {QStringLiteral("图层"), {QStringLiteral("图层")}, true},
    };
}

void WorkspaceMediator::setCurrentWorkspace(WorkspaceId id)
{
    // F-B2 简单版: 不验 id 范围 (4 个枚举值, 调用方控制)
    m_current = id;
}

int WorkspaceMediator::dockCount() const
{
    return m_workspaces.value(m_current).size();
}

QString WorkspaceMediator::dockTitle(int dockIndex) const
{
    const auto docks = m_workspaces.value(m_current);
    if (dockIndex < 0 || dockIndex >= docks.size()) return QString();
    return docks[dockIndex].title;
}

QStringList WorkspaceMediator::dockTabs(int dockIndex) const
{
    const auto docks = m_workspaces.value(m_current);
    if (dockIndex < 0 || dockIndex >= docks.size()) return {};
    return docks[dockIndex].tabs;
}

bool WorkspaceMediator::isDockVisible(int dockIndex) const
{
    if (dockIndex < 0 || dockIndex >= m_dockVisible.size()) return false;
    return m_dockVisible[dockIndex];
}

void WorkspaceMediator::switchWorkspace(WorkspaceId id)
{
    if (m_current == id) return;
    LOG_INFO("[WorkspaceMed] switchWorkspace: {} -> {}", static_cast<int>(m_current), static_cast<int>(id));
    m_current = id;
    // F-B2 (2026-09-08) 修正: 从新工作区 DockConfig.visible 重置
    //   之前全填 true → Photo dock 2 visible=false 被覆盖
    const auto docks = m_workspaces.value(m_current);
    m_dockVisible.resize(docks.size());
    for (int i = 0; i < docks.size(); ++i) {
        m_dockVisible[i] = docks[i].visible;
    }
    emit workspaceChanged(id);
}

void WorkspaceMediator::setDockVisible(int dockIndex, bool visible)
{
    if (dockIndex < 0 || dockIndex >= m_dockVisible.size()) return;
    if (m_dockVisible[dockIndex] == visible) return;
    LOG_DEBUG("[WorkspaceMed] setDockVisible: dock={} -> {}", dockIndex, visible);
    m_dockVisible[dockIndex] = visible;
    emit dockVisibilityChanged(dockIndex, visible);
}

} // namespace mediators
