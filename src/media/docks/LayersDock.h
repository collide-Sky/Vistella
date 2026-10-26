// SPDX-License-Identifier: MIT
//
// LayersDock - F-G (2026-09-09) [F-G.2 升级 2026-09-09]
//
// 右侧 panel dock 3: 图层 (PS 风格)
//   F-G.2 阶段: QTabWidget 2 tab (图层 + 通道/路径 placeholder)
//   setContentWidget(tabIdx, w) 替换 tab 内容
//   F-G.3 阶段: imagewindow 集成时 setContentWidget(0, m_layerPanel) 装 LayerPanel
//   F-M 阶段: setContentWidget(1, m_channelPathPanel) 装通道/路径面板
//   P8.3.2 (2026-10-26): 通道/路径 早已在 F-G.3 拆成右侧面板自己的一个页
//     (VChannelPathPage), 内层的第 2 页只剩一个 placeholder. 本 dock 作为
//     "图层组" 的内容被装进 VDraggableTabWidget 后, 外层已经提供
//     Layers / Channels-Paths / History 的切换, 内层再画一排 tab 就是重复.
//     setEmbeddedInPanelGroup(true) 收起内层 tab 栏并丢掉那个死页.
#pragma once

#include <QWidget>

#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace docks {

class LayersDock : public QWidget
{
    Q_OBJECT
public:
    explicit LayersDock(QWidget* parent = nullptr);
    ~LayersDock() override = default;

    // P8.3.2 (2026-10-26): 装进右侧面板的"图层组"时收起内层 tab 栏,
    //   并移除早已被外层取代的通道/路径死页. 单独使用 (老 RightPanelStack)
    //   时保持原样, 2 个 tab.
    void setEmbeddedInPanelGroup(bool embedded);
    bool isEmbeddedInPanelGroup() const { return m_embedded; }

    // 替换 tab 内容 (清空旧 widget, 装新 widget 到 tab 的 QVBoxLayout)
    void setContentWidget(int tabIndex, QWidget* w);
    QTabWidget* tabs() { return m_tabs; }

private:
    QVBoxLayout* m_layout = nullptr;
    QTabWidget*  m_tabs   = nullptr;
    QLabel*      m_layerPlaceholder   = nullptr;
    QLabel*      m_channelPlaceholder = nullptr;
    bool         m_embedded = false;
};

} // namespace docks
