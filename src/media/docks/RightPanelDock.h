// SPDX-License-Identifier: MIT
//
// RightPanelDock - F-G.3 Fix (2026-09-10)
//
// 右侧 panel (1 个 widget, 4+1 tab):
//   - 颜色 (ColorDock)
//   - 属性 (PropertiesDock) - 修过 F-I + P0-4.9 (含选区 bbox)
//   - 图层 (LayersDock) - 修过 F-M.3
//   - 选区 (ChannelPathPanel) - 修过 P0-4
//   - 调整 (AdjustmentPanel 5 tab: 曲线/色阶/HSL/黑白/通道混合器) - P0-3.2 (可选, 5th tab)
//
// 集成 (替代之前的 3 个分离 dock 重叠):
//   - 撤销: adjDock (addDockWidget + splitDockWidget) + infoDock + m_rightPanel
//   - 1 个 panel 装 tab, setGeometry 浮动在 imagewindow 右侧
//
// 设计原则:
//   - 浮动 panel, 无 title bar
//   - QTabWidget 装 tab
//   - 浮动位置: imagewindow 右上角 (跟原 m_rightPanel 一样 setGeometry)
//
#pragma once

#include <QWidget>

class QTabWidget;
class AdjustmentPanel;

namespace mediators { class WorkspaceMediator; }

namespace docks {

class ColorDock;
class PropertiesDock;
class LayersDock;
class ChannelPathPanel;
class HistoryDock;

class RightPanelDock : public QWidget
{
    Q_OBJECT
public:
    explicit RightPanelDock(QWidget* parent = nullptr);
    ~RightPanelDock() override;

    // F-G.3 (2026-09-09) attach WorkspaceMediator
    void attach(mediators::WorkspaceMediator* wsMed);

    // P0-3.2: 调整 panel 集成 (外部已创建, 装到 tab)
    void setAdjustmentPanel(AdjustmentPanel* adj);

    // P0-8.1 (2026-09-15): 历史 dock 暴露 (ImageWindow::ctor setUndoStack 注入)
    HistoryDock* historyDock() const { return m_historyDock; }

    // 浮动位置 (imagewindow 右上角, 跟 m_rightPanel 同样模式)
    QSize sizeHint() const override { return QSize(340, 600); }

private:
    QTabWidget      *m_tabs        = nullptr;
    ColorDock       *m_colorDock   = nullptr;
    PropertiesDock  *m_propsDock   = nullptr;
    LayersDock      *m_layersDock  = nullptr;
    ChannelPathPanel*m_chanPathPanel = nullptr;
    HistoryDock     *m_historyDock = nullptr;
};

} // namespace docks
