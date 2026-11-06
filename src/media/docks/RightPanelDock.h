// SPDX-License-Identifier: MIT
//
// RightPanelDock - the PS-style right hand panel.
//
// M2-2: the self written drag layer is gone.
//
//   Before: 1 QSplitter holding 3 VDraggableTabWidget, each holding a set
//   of V*Page adapters (VPages.h) that wrapped the real panels
//   (ColorTab, SwatchTab, ...) in a zero margin box.
//
//   The self written layer was 0% usable: the drag code passed a LOCAL
//   widget position to mapFromGlobal(), so a drop never moved a page;
//   the floating wrapper was created with a parent, so it could never
//   float; and the page list and the tab bar kept two separate indices,
//   so switching pages could address the wrong one.
//
//   Now: 3 native QTabWidget groups inside one QSplitter, each holding
//   the real panel widget directly. There is no adapter layer any more:
//   RightPanelDock owns the panels itself and hands out the raw widget.
//
//   What Qt gives us for free that the old layer had to fake:
//     - the tab bar, the page switcher and the keyboard navigation,
//     - a page that sizes itself to the CURRENT page (QStackedWidget
//       does this, the old custom widget had to reimplement it),
//     - the drag to a floating window, the re-dock and the tab reordering
//       are the ones that DID work: QTabWidget::setMovable /
//       QTabWidget::setTabsClosable / QMainWindow::addDockWidget.
//
// The three groups:
//   - colour group:   colour / swatch / gradient / pattern
//   - property group: properties / adjustments
//   - layer group:    layers / channels+paths / history
//
// The colour group and the property group size to their content; only
// the layer group absorbs the leftover height (the layer list is the one
// panel that is meant to grow with the window).
#pragma once

#include <QWidget>
#include <QShowEvent>

#include "PropertiesDock.h"   // PropertiesDock::ImageInfo is a nested type

class QTabWidget;
class QSplitter;
class QUndoStack;

// AdjustmentPanel lives in the GLOBAL namespace (src/media/imagewindow/
// AdjustmentPanel.h), not in docks:: -- so its forward declaration has to
// stay outside the namespace block below.
class AdjustmentPanel;

#include "../mediators/WorkspaceMediator.h"

namespace docks {

// The panels this dock hosts. All of them live in this namespace, so the
// forward declarations have to be inside it too -- a forward declaration
// in the global namespace would name a DIFFERENT type and every pointer
// assignment would fail to compile.
class ColorTab;
class SwatchTab;
class GradientTab;
class PatternTab;
class LayersDock;
class ChannelPathPanel;
class HistoryDock;

class RightPanelDock : public QWidget
{
    Q_OBJECT
public:
    explicit RightPanelDock(QWidget* parent = nullptr);
    ~RightPanelDock();

    // attach WorkspaceMediator
    void attach(mediators::WorkspaceMediator* wsMed);

    // -----------------------------------------------------------------
    // the panels themselves -- the right way to talk to a right panel
    // page now. Each returns the real widget, not an adapter.
    // -----------------------------------------------------------------

    // Colour group.
    ColorTab*       colorTab()       const { return m_colorTab; }
    SwatchTab*      swatchTab()      const { return m_swatchTab; }
    GradientTab*    gradientTab()    const { return m_gradientTab; }
    PatternTab*     patternTab()     const { return m_patternTab; }
    QTabWidget*     colorGroup()     const { return m_colorGroup; }

    // Property group.
    PropertiesDock*  propertiesDock()  const { return m_propertiesDock; }
    AdjustmentPanel* adjustmentPanel() const { return m_adjustmentPanel; }
    QTabWidget*      propertyGroup()   const { return m_propertyGroup; }

    // Layer group.
    LayersDock*       layerDock()       const { return m_layerDock; }
    ChannelPathPanel* channelPathPanel() const { return m_channelPathPanel; }
    HistoryDock*      historyDock()      const { return m_historyDock; }
    QTabWidget*       layerGroup()      const { return m_layerGroup; }

    // QUndoStack binding forwarded to the history panel.
    void setUndoStack(QUndoStack* stack);

    // properties file info setter. Uses PropertiesDock's own ImageInfo
    // struct (defined in PropertiesDock.h) so callers can reuse the same
    // struct that loadFile already builds.
    void setPropertyImageInfo(const PropertiesDock::ImageInfo& info);

    // Layer panel (still lives in imagewindow; we reparent it into the
    // LayersDock content widget to keep the existing layout logic).
    void setLayerPanel(QWidget* panel);

    // Channel/path panel convenience: forward setQuickMask.
    void setChannelPathQuickMask(bool on);

    // incremental cache invalidation fan-out
    //   Walks the 3 groups and calls invalidateCache() on every page.
    //   The expensive pages (properties / channel path) drop their
    //   derived state and schedule a repaint.
    //
    //   Callers:
    //     - ImageWindow::onActiveLayerChanged (active layer)
    //     - ImageWindow::onActiveChannelChanged (active channel)
    //     - any future "model side changed" event
    void invalidateAllPageCaches();

    // The number of times the cache invalidation fan-out reached each
    // page. Test hook (the pages themselves do not count).
    int propertyInvalidateCount() const { return m_propertyInvalidateCount; }
    int channelPathInvalidateCount() const { return m_channelPathInvalidateCount; }
    int historyInvalidateCount() const { return m_historyInvalidateCount; }

    // 浮动位置 (imagewindow 右上角, 跟 m_rightPanel 同样模式)
    QSize sizeHint() const;

    // 有界的最小尺寸 —— dock 不能决定主窗口的高度
    QSize minimumSizeHint() const override;

    // give the colour group and the property group
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
    // 订阅 WorkspaceMediator 信号
    void onWorkspaceChanged(mediators::WorkspaceId id);
    void onDockVisibilityChanged(int dockIndex, bool visible);

private:
    void buildColorGroup();
    void buildPropertyGroup();
    void buildLayerGroup();

    QSplitter* m_splitter = nullptr;

    // Group 1: colour / swatch / gradient / pattern.
    QTabWidget* m_colorGroup = nullptr;
    ColorTab*   m_colorTab = nullptr;
    SwatchTab*  m_swatchTab = nullptr;
    GradientTab* m_gradientTab = nullptr;
    PatternTab* m_patternTab = nullptr;

    // Group 2: properties / adjustments.
    QTabWidget* m_propertyGroup = nullptr;
    PropertiesDock* m_propertiesDock = nullptr;
    AdjustmentPanel* m_adjustmentPanel = nullptr;

    // Group 3: layers / channel+path / history.
    QTabWidget* m_layerGroup = nullptr;
    LayersDock* m_layerDock = nullptr;
    ChannelPathPanel* m_channelPathPanel = nullptr;
    HistoryDock* m_historyDock = nullptr;

    mediators::WorkspaceMediator* m_wsMed = nullptr;

    // P8.3.2: the content-sized split is applied once, after the dock has
    //   been shown and knows its real height.
    bool m_splitApplied = false;

    // Cache invalidation counters (test hook).
    int m_propertyInvalidateCount = 0;
    int m_channelPathInvalidateCount = 0;
    int m_historyInvalidateCount = 0;
};

} // namespace docks
