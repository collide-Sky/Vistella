// SPDX-License-Identifier: MIT
//
// RightPanelDock - P7.6.x v3 commit 4 (2026-10-15) rewrite
//
// 右侧 panel PS 风格 3 嵌套 sub-dock:
//   - Sub-dock 1 (颜色组):  颜色 / 色板 / 渐变 / 图案
//   - Sub-dock 2 (属性组):  属性 / 调整
//   - Sub-dock 3 (图层组):  图层 / 通道/路径 / 历史
//
// 之前版本 (F-G.3 2026-09-10): 1 个 QTabWidget 装 5-6 tab (颜色/属性/图层/
// 选区/历史/调整). 全部用各自子 widget (ColorDock 4-tab, LayersDock 2-tab,
// PropertiesDock, ChannelPathPanel, HistoryDock, AdjustmentPanel).
//
// 重构原因:
//   - 嵌套 sub-dock 是 PS 风格 (用户 10/15 spec). tab 可以拉出变 floating
//     dock, 跨 sub-dock 合并, sub-dock 高度可拖拽.
//   - VDraggableTabWidget (commit 2) + VTabPageBase + V*Page adapter (commit 3)
//     已经准备好了, 这里只组合.
//   - ColorDock / LayersDock 类还在, 但 RightPanelDock 不再用, 标
//     deprecated. 调用方迁移到 V*Page accessor 后删.
//
// 设计:
//   - 1 个 QSplitter (vertical) 装 3 个 VDraggableTabWidget
//   - 每个 VDraggableTabWidget 装一组 V*Page
//   - P8.3.2 (2026-10-26): 颜色组 / 属性组 按内容高度, 图层组吃掉剩余高度
//     (原来是 1/3 + 1/3 + 1/3 等分, 颜色组空一大片, 属性文字浮在空白中间)
//   - compositeStrategy 字段 (P0 placeholder, P8.4 多线程合成接)
//
// Backward compatibility:
//   - 旧 accessor (colorDock() 返 ColorDock* 等) 保留但返 nullptr + log 警告.
//   - 新 accessor (colorPage() 返 VColorPage* 等) 是调用方要用的.
//   - setAdjustmentPanel(AdjustmentPanel*) 保留为 no-op + log 警告;
//     调整 panel 现在通过 VAdjustmentPage (内置) 接入.

#pragma once

#include <QWidget>
#include <QShowEvent>

#include "../../ui/dock/VDraggableTabWidget.h"
#include "../../ui/dock/VPages.h"
#include "../../ui/dock/VCompositeStrategy.h"

class QStackedWidget;
class QSplitter;
class QUndoStack;

class AdjustmentPanel;

#include "../mediators/WorkspaceMediator.h"

namespace docks {

// Forward declared. The old widget classes are kept around as deprecated
// shims for callers that haven't migrated yet.
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
    ~RightPanelDock();

    // F-G.3 (2026-09-09) attach WorkspaceMediator
    void attach(mediators::WorkspaceMediator* wsMed);

    // -----------------------------------------------------------------
    // NEW: V*Page accessors (commit 4 onwards). The right way to talk
    // to a right-panel page now.
    // -----------------------------------------------------------------

    // Sub-dock 1 (color group).
    vistella::ui::VColorPage*    colorPage()    const { return m_colorPage; }
    vistella::ui::VSwatchPage*   swatchPage()   const { return m_swatchPage; }
    vistella::ui::VGradientPage* gradientPage() const { return m_gradientPage; }
    vistella::ui::VPatternPage*  patternPage()  const { return m_patternPage; }
    vistella::ui::VDraggableTabWidget* subDockColor() const { return m_subDockColor; }

    // Sub-dock 2 (property group).
    vistella::ui::VPropertyPage*    propertyPage()    const { return m_propertyPage; }
    vistella::ui::VAdjustmentPage* adjustmentPage()  const { return m_adjustmentPage; }
    vistella::ui::VDraggableTabWidget* subDockProperty() const { return m_subDockProperty; }

    // Sub-dock 3 (layer group).
    vistella::ui::VLayerPage*       layerPage()       const { return m_layerPage; }
    vistella::ui::VChannelPathPage* channelPathPage() const { return m_channelPathPage; }
    vistella::ui::VHistoryPage*     historyPage()     const { return m_historyPage; }
    vistella::ui::VDraggableTabWidget* subDockLayer() const { return m_subDockLayer; }

    // QUndoStack binding forwarded to VHistoryPage (was on HistoryDock).
    void setUndoStack(QUndoStack* stack);

    // Q4.3 (2026-10-08): properties file info setter. Uses PropertiesDock's
    // own ImageInfo struct (defined in PropertiesDock.h) so callers can
    // reuse the same struct that loadFile already builds.
    void setPropertyImageInfo(const PropertiesDock::ImageInfo& info);

    // Layer panel (still lives in imagewindow; we reparent it into the
    // VLayerPage's content widget to keep the existing layout logic).
    void setLayerPanel(class QWidget* panel);

    // Channel/path panel convenience: forward setQuickMask to VChannelPathPage.
    void setChannelPathQuickMask(bool on);

    // P7.10 (2026-10-15): incremental cache invalidation fan-out.
    //   Walks the 3 sub-docks and calls invalidateCache() on every page.
    //   Pages with default no-op override ignore the call. The
    //   expensive pages (VPropertyPage / VChannelPathPage) drop their
    //   derived state and schedule a repaint.
    //
    //   Callers:
    //     - ImageWindow::onActiveLayerChanged (active layer)
    //     - ImageWindow::onActiveChannelChanged (active channel)
    //     - any future "model side changed" event
    void invalidateAllPageCaches();

    // -----------------------------------------------------------------
    // DEPRECATED: old ColorDock / PropertiesDock / LayersDock / etc
    // accessors. Return nullptr and log a warning. Kept so callers that
    // haven't migrated yet still compile; the nullptr will then fail
    // loudly in the deref so we notice.
    //
    // Remove in a future commit once all callers have been migrated.
    // -----------------------------------------------------------------
    // TODO(P8.5): this deprecated accessor returns nullptr, so the toolbar foreground/background chain never connects -- see docs/DEBT.md#A-COLORDOCK
    ColorDock*        colorDock()        const;
    PropertiesDock*   propertiesDock()   const;
    LayersDock*       layersDock()       const;
    ChannelPathPanel* channelPathPanel() const;
    HistoryDock*      historyDock()      const;
    void              setAdjustmentPanel(AdjustmentPanel* adj);

    // P0-3.2 (2026-09-08): setAdjustmentPanel kept for the
    // ImageWindow::ctor signature; it now does nothing.

    // 浮动位置 (imagewindow 右上角, 跟 m_rightPanel 同样模式)
    QSize sizeHint() const;

    // P8.3.2 (2026-10-26): 有界的最小尺寸 —— dock 不能决定主窗口的高度.
    QSize minimumSizeHint() const override;

    // P8.3.2 (2026-10-26): give the colour group and the property group
    //   exactly the height their content needs and let the layer group
    //   absorb the rest.
    //
    //   The old code asked for a fixed 1/3 split (200/200/200). Qt turns
    //   that into three equally tall groups, so the colour group carried
    //   hundreds of pixels of empty space and the property text floated in
    //   the middle of a blank panel. Re-applied once the dock knows its
    //   real height, and whenever the user asks for it.
    void applyContentSizedSplit();

protected:
    void showEvent(QShowEvent* e) override;

private slots:
    // Stage D (2026-09-15): 订阅 WorkspaceMediator 信号.
    void onWorkspaceChanged(mediators::WorkspaceId id);
    void onDockVisibilityChanged(int dockIndex, bool visible);

private:
    void buildSubDockColor();
    void buildSubDockProperty();
    void buildSubDockLayer();

    QSplitter* m_splitter = nullptr;

    // Sub-dock 1: 颜色 / 色板 / 渐变 / 图章.
    vistella::ui::VDraggableTabWidget* m_subDockColor = nullptr;
    vistella::ui::VColorPage*          m_colorPage    = nullptr;
    vistella::ui::VSwatchPage*         m_swatchPage   = nullptr;
    vistella::ui::VGradientPage*       m_gradientPage = nullptr;
    vistella::ui::VPatternPage*        m_patternPage  = nullptr;

    // Sub-dock 2: 属性 / 调整.
    vistella::ui::VDraggableTabWidget* m_subDockProperty = nullptr;
    vistella::ui::VPropertyPage*       m_propertyPage    = nullptr;
    vistella::ui::VAdjustmentPage*     m_adjustmentPage  = nullptr;

    // Sub-dock 3: 图层 / 通道/路径 / 历史.
    vistella::ui::VDraggableTabWidget* m_subDockLayer = nullptr;
    vistella::ui::VLayerPage*          m_layerPage    = nullptr;
    vistella::ui::VChannelPathPage*    m_channelPathPage = nullptr;
    vistella::ui::VHistoryPage*        m_historyPage  = nullptr;

    mediators::WorkspaceMediator* m_wsMed = nullptr;

    // P8.3.2: the content-sized split is applied once, after the dock has
    //   been shown and knows its real height.
    bool m_splitApplied = false;
};

} // namespace docks
