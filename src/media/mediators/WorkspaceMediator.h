// SPDX-License-Identifier: MIT
//
// WorkspaceMediator - F-B2 (2026-09-08)
//
// Mediator 模式, 1 个类只负责"工作区切换 + 3 dock 内容配置"一个方向
//   转发: 调用方 → WorkspaceMediator → 订阅方 (二级工具栏 工作区按钮 / RightPanelStack 3 dock)
//
// 设计原则 (跟 ToolMediator 一致, 一次性到位):
//   1) 单例 per ImageWindow
//   2) 4 个默认工作区 (Basic / Photo / Paint / Web) — 跟 PS 同款
//   3) 每个工作区定义 3 dock 的 tab 列表
//   4) 强约束: 不动 setMode/toggleMode/m_normalSize (主窗口状态机)
//
// 工作区示例 (PS 同款):
//   Basic:  颜色/色板/渐变/图案 + 属性/调整 + 图层/通道/路径  (3 dock 全开)
//   Photo:  颜色/色板 + 属性/直方图 + (无 dock 3)
//   Paint:  颜色/色板/画笔 + 属性/调整 + (无 dock 3)
//   Web:    颜色/色板 + 属性/调整 + 图层
//
// 用法:
//   m_workspaceMed->switchWorkspace(WorkspaceId::Basic);
//   m_workspaceMed->dockCount();        // 3
//   m_workspaceMed->dockTabs(0);        // {"颜色", "色板", "渐变", "图案"}
//   m_workspaceMed->setDockVisible(0, false);  // 关掉 dock 1
//
#pragma once

#include <QObject>
#include <QStringList>
#include <QHash>

namespace mediators {

// 工作区 ID (4 默认, 跟 PS 同款)
enum class WorkspaceId {
    Basic = 0,   // 基本功能: 颜色/属性/图层 三 dock 全开
    Photo,       // 摄影: 颜色 + 属性/直方图
    Paint,       // 绘画: 颜色/画笔 + 属性
    Web,         // 图形和 Web: 颜色 + 属性/调整
};

// 一个 dock 的配置
struct DockConfig {
    QString     title;       // dock 标题 ("颜色" / "属性" / "图层")
    QStringList tabs;        // tab 名列表 ({"颜色", "色板", "渐变", "图案"})
    bool        visible = true;  // F-G 阶段用户可隐藏
};

class WorkspaceMediator : public QObject
{
    Q_OBJECT
public:
    explicit WorkspaceMediator(QObject* parent = nullptr);
    ~WorkspaceMediator() override = default;

    // 当前工作区
    WorkspaceId currentWorkspace() const { return m_current; }
    void setCurrentWorkspace(WorkspaceId id);

    // dock 配置查询 (RightPanelStack 用)
    int          dockCount() const;
    QString      dockTitle(int dockIndex) const;
    QStringList  dockTabs(int dockIndex) const;
    bool         isDockVisible(int dockIndex) const;

public slots:
    // 切换工作区 (二级工具栏右侧的 "工作区 ▼" 按钮)
    void switchWorkspace(WorkspaceId id);
    // 单独控制某个 dock 的显示/隐藏 (二级工具栏或右键菜单)
    void setDockVisible(int dockIndex, bool visible);

signals:
    // 订阅方:
    //   二级工具栏 "工作区" 按钮: 同步按钮文字
    //   RightPanelStack 3 dock: 重建 tab + 隐藏/显示
    //   MediatorMediator: 通知其他 Mediator (e.g. DialogMediator 关掉无关 dialog)
    void workspaceChanged(WorkspaceId id);
    // 单独某个 dock 显示/隐藏
    void dockVisibilityChanged(int dockIndex, bool visible);

private:
    // 4 工作区 × 3 dock 配置
    //   工作区 0 (Basic):  3 dock 全开, 颜色/色板/渐变/图案, 属性/调整, 图层/通道/路径
    //   工作区 1 (Photo):  2 dock, 颜色/色板, 属性/直方图
    //   工作区 2 (Paint):  2 dock, 颜色/色板/画笔, 属性/调整
    //   工作区 3 (Web):    3 dock, 颜色/色板, 属性/调整, 图层
    void buildDefaultWorkspaces();

    // 4 × 3 dock 配置
    QHash<WorkspaceId, QList<DockConfig>> m_workspaces;
    WorkspaceId m_current = WorkspaceId::Basic;
    QList<bool> m_dockVisible;   // 当前工作区的 3 dock 显隐 (随 setDockVisible 改)
};

} // namespace mediators

Q_DECLARE_METATYPE(mediators::WorkspaceId)
Q_DECLARE_METATYPE(mediators::DockConfig)
