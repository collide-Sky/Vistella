// SPDX-License-Identifier: MIT
//
// LayersDock - F-G (2026-09-09) [F-G.2 升级 2026-09-09]
//
// 右侧 panel dock 3: 图层 (PS 风格)
//   F-G.2 阶段: QTabWidget 2 tab (图层 + 通道/路径 placeholder)
//   setContentWidget(tabIdx, w) 替换 tab 内容
//   F-G.3 阶段: imagewindow 集成时 setContentWidget(0, m_layerPanel) 装 LayerPanel
//   F-M 阶段: setContentWidget(1, m_channelPathPanel) 装通道/路径面板
//
#pragma once

#include <QWidget>

class QLabel;
class QVBoxLayout;
class QTabWidget;

namespace docks {

class LayersDock : public QWidget
{
    Q_OBJECT
public:
    explicit LayersDock(QWidget* parent = nullptr);
    ~LayersDock() override = default;

    // 替换 tab 内容 (清空旧 widget, 装新 widget 到 tab 的 QVBoxLayout)
    void setContentWidget(int tabIndex, QWidget* w);
    QTabWidget* tabs() { return m_tabs; }

private:
    QVBoxLayout* m_layout = nullptr;
    QTabWidget*  m_tabs   = nullptr;
    QLabel*      m_layerPlaceholder   = nullptr;
    QLabel*      m_channelPlaceholder = nullptr;
};

} // namespace docks
