// SPDX-License-Identifier: MIT
//
// RightPanelStack - F-G (2026-09-09)
//
// imagewindow 右侧 panel: 3 dock 上下排 (颜色/属性/图层)
//   跟 WorkspaceMediator 联动: 切换工作区时, 重建 dock tab 列表 + 显隐
//   强约束: 不动 setMode/toggleMode/m_normalSize (主窗口状态机)
//
// 设计:
//   - QWidget 容器, QVBoxLayout 装 3 dock
//   - attach(wsMed): connect wsMed->workspaceChanged
//   - onWorkspaceChanged(id): 重读 wsMed->dockTabs(i) 更新每个 dock 内部 tab, 跟 wsMed->isDockVisible(i) 显隐
//   - F-G.1 阶段: dock 内 tab 暂用 placeholder (后续 F-G.2 升级为 QTabWidget)
//   - F-G.2 阶段: LayersDock 内接 LayerPanel
//   - F-H/I 阶段: ColorDock/PropertiesDock 实装
//
#pragma once

#include <QWidget>
#include "../mediators/WorkspaceMediator.h"

class QVBoxLayout;

namespace docks {

class ColorDock;
class PropertiesDock;
class LayersDock;

class RightPanelStack : public QWidget
{
    Q_OBJECT
public:
    explicit RightPanelStack(QWidget* parent = nullptr);
    ~RightPanelStack() override;

    void attach(mediators::WorkspaceMediator* wsMed);
    void detach();

private slots:
    void onWorkspaceChanged(mediators::WorkspaceId id);

private:
    QVBoxLayout*    m_layout     = nullptr;
    ColorDock*      m_colorDock  = nullptr;
    PropertiesDock* m_propsDock  = nullptr;
    LayersDock*     m_layersDock = nullptr;
    mediators::WorkspaceMediator* m_wsMed = nullptr;  // weak ref
};

} // namespace docks
