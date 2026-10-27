// SPDX-License-Identifier: MIT
//
// RightPanelDock implementation - P7.6.x v3 commit 4 (2026-10-15)
//
#include "RightPanelDock.h"

#include "PropertiesDock.h"
#include "../../ui/dock/VTabPageBase.h"
#include "../mediators/WorkspaceMediator.h"
#include "logger.h"

#include <QSplitter>
#include <QUndoStack>
#include <QSize>
#include <QStackedWidget>

namespace docks {

namespace {

// the smallest height a panel group may take. Below
//   this the tab bar is all that is left and the group is useless.
constexpr int kMinGroupHeight = 80;

// VColorPage wraps docks::ColorTab internally. Callers that need the
// underlying ColorTab can use VColorPage::inner() to wire signals.
vistella::ui::VColorPage* makeColorPage(QWidget* parent)
{
    return new vistella::ui::VColorPage(parent);
}

} // namespace

RightPanelDock::RightPanelDock(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(4);
    outer->addWidget(m_splitter);

    buildSubDockColor();
    buildSubDockProperty();
    buildSubDockLayer();

    // the colour group and the property group size to
    //   their content; only the layer group absorbs the leftover height.
    //
    //   The old code asked for a fixed 1/3 split (200/200/200). Qt's
    //   QSplitter turns that into three equally tall groups - with no
    //   stretch factor set, every widget is "expansive" and the leftover
    //   space is shared out proportionally. The colour group therefore
    //   carried hundreds of pixels of empty space and the property text
    //   floated in the middle of a blank panel.
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 1);
    applyContentSizedSplit();

    LOG_INFO("[RightPanelDock] v3 commit 4 created: 3 sub-docks "
             "(color-group/property-group/layer-group)");
}

void RightPanelDock::applyContentSizedSplit()
{
    if (!m_splitter || !m_subDockColor || !m_subDockProperty || !m_subDockLayer)
        return;

    // A group is never smaller than its header plus a usable strip of
    // content, otherwise the tab bar would be the only thing left.
    const int hColor    = qMax(kMinGroupHeight, m_subDockColor->sizeHint().height());
    const int hProperty = qMax(kMinGroupHeight, m_subDockProperty->sizeHint().height());

    // The layer group takes whatever is left: it is the one panel whose
    // content (the layer list) is meant to grow with the window.
    m_splitter->setSizes({ hColor, hProperty, 0 });
}

void RightPanelDock::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // The first show is the first moment the dock knows its real height,
    // which is when the content-sized split becomes meaningful.
    if (!m_splitApplied) {
        m_splitApplied = true;
        applyContentSizedSplit();
    }
}

RightPanelDock::~RightPanelDock() = default;

void RightPanelDock::buildSubDockColor()
{
    m_subDockColor = new vistella::ui::VDraggableTabWidget(this);
    m_colorPage    = makeColorPage(m_subDockColor);
    m_swatchPage   = new vistella::ui::VSwatchPage(m_subDockColor);
    m_gradientPage = new vistella::ui::VGradientPage(m_subDockColor);
    m_patternPage  = new vistella::ui::VPatternPage(m_subDockColor);

    m_subDockColor->addPage(m_colorPage);
    m_subDockColor->addPage(m_swatchPage);
    m_subDockColor->addPage(m_gradientPage);
    m_subDockColor->addPage(m_patternPage);
    m_subDockColor->setCompositeStrategy(
        vistella::ui::CompositeStrategy::Sequential);

    m_splitter->addWidget(m_subDockColor);
}

void RightPanelDock::buildSubDockProperty()
{
    m_subDockProperty = new vistella::ui::VDraggableTabWidget(this);
    m_propertyPage    = new vistella::ui::VPropertyPage(m_subDockProperty);
    m_adjustmentPage  = new vistella::ui::VAdjustmentPage(m_subDockProperty);

    m_subDockProperty->addPage(m_propertyPage);
    m_subDockProperty->addPage(m_adjustmentPage);
    m_subDockProperty->setCompositeStrategy(
        vistella::ui::CompositeStrategy::Sequential);

    m_splitter->addWidget(m_subDockProperty);
}

void RightPanelDock::buildSubDockLayer()
{
    m_subDockLayer     = new vistella::ui::VDraggableTabWidget(this);
    m_layerPage        = new vistella::ui::VLayerPage(m_subDockLayer);
    m_channelPathPage  = new vistella::ui::VChannelPathPage(m_subDockLayer);
    m_historyPage      = new vistella::ui::VHistoryPage(m_subDockLayer);

    m_subDockLayer->addPage(m_layerPage);
    m_subDockLayer->addPage(m_channelPathPage);
    m_subDockLayer->addPage(m_historyPage);
    m_subDockLayer->setCompositeStrategy(
        vistella::ui::CompositeStrategy::Sequential);

    m_splitter->addWidget(m_subDockLayer);
}

void RightPanelDock::attach(mediators::WorkspaceMediator* wsMed)
{
    m_wsMed = wsMed;
    if (m_wsMed) {
        connect(m_wsMed, &mediators::WorkspaceMediator::workspaceChanged,
                this, &RightPanelDock::onWorkspaceChanged);
        connect(m_wsMed, &mediators::WorkspaceMediator::dockVisibilityChanged,
                this, &RightPanelDock::onDockVisibilityChanged);
        onWorkspaceChanged(m_wsMed->currentWorkspace());
    }
    LOG_DEBUG("[RightPanelDock] attach wsMed={}", wsMed ? "yes" : "null");
}

void RightPanelDock::onWorkspaceChanged(mediators::WorkspaceId id)
{
    if (!m_wsMed) return;
    LOG_INFO("[RightPanelDock] workspaceChanged: id={}", static_cast<int>(id));
    // P7.6.x v3: workspace visibility now controls sub-dock 1 (color-group)
    // and sub-dock 2 (property-group). Sub-dock 3 (layer-group) is always
    // visible (core editing area).
    bool visColor   = m_wsMed->isDockVisible(0);
    bool visProperty = m_wsMed->isDockVisible(1);
    if (m_subDockColor)    m_subDockColor->setVisible(visColor);
    if (m_subDockProperty) m_subDockProperty->setVisible(visProperty);
    Q_UNUSED(id);
}

void RightPanelDock::onDockVisibilityChanged(int dockIndex, bool visible)
{
    if (!m_wsMed) return;
    if (dockIndex == 0 && m_subDockColor)    m_subDockColor->setVisible(visible);
    if (dockIndex == 1 && m_subDockProperty) m_subDockProperty->setVisible(visible);
}

void RightPanelDock::setUndoStack(QUndoStack* stack)
{
    if (m_historyPage) m_historyPage->setUndoStack(stack);
}

void RightPanelDock::setPropertyImageInfo(const PropertiesDock::ImageInfo& info)
{
    if (!m_propertyPage) return;
    if (auto* inner = m_propertyPage->inner()) {
        inner->setImageInfo(info);
    }
}

void RightPanelDock::setLayerPanel(QWidget* panel)
{
    // VLayerPage currently hosts a bare LayersDock. Callers that need to
    // install a LayerPanel into the first layer tab can do so via
    // LayersDock::setContentWidget(0, panel). For now we look up the
    // inner LayersDock through the VLayerPage and call setContentWidget.
    if (!m_layerPage) return;
    if (auto* inner = m_layerPage->inner()) {
        inner->setContentWidget(0, panel);
    }
}

void RightPanelDock::setChannelPathQuickMask(bool on)
{
    if (!m_channelPathPage) return;
    if (auto* inner = m_channelPathPage->inner()) {
        inner->setQuickMask(on);
    }
}

void RightPanelDock::invalidateAllPageCaches()
{
    auto fanOut = [](vistella::ui::VDraggableTabWidget* sd) {
        if (!sd) return;
        for (int i = 0; i < sd->pageCount(); ++i) {
            // pageAt() returns QWidget* (VDraggableTabWidget's API);
            // the VTabPageBase API lives on the actual subclass. qobject_cast
            // is safe because every page was inserted via addPage(VTabPageBase*).
            auto* pageBase = qobject_cast<vistella::ui::VTabPageBase*>(sd->pageAt(i));
            if (pageBase) pageBase->invalidateCache();
        }
    };
    fanOut(m_subDockColor);
    fanOut(m_subDockProperty);
    fanOut(m_subDockLayer);
}

QSize RightPanelDock::sizeHint() const
{
    return QSize(340, 800);
}

QSize RightPanelDock::minimumSizeHint() const
{
    // 右侧面板是 dock, 它不能决定主窗口的高度
    //
    //   Qt 会把 dock 的 minimumSizeHint 累加进 QMainWindow 的布局最小值:
    //   三个组的最小高度加起来 (颜色 ~110 + 属性 ~390 + 图层 ~240) 超过
    //   窗口高度时, 窗口就被强行撑大 —— 打开曲线调整页时窗口长到 1186px,
    //   直接超出 1080 的屏幕.
    //
    //   正确做法是 dock 给一个"有界下限": 窗口给多少高度, 三个组就怎么分,
    //   需要更多空间的用户自己拖分隔条 (splitter 的拖动不受这个下限限制).
    return QSize(280, 240);
}

// ---------------------------------------------------------------------------
// DEPRECATED shims. Return nullptr + log. Kept so callers that haven't
// been migrated yet still compile (the deref will then fail).
// ---------------------------------------------------------------------------

ColorDock*        RightPanelDock::colorDock()        const
{
    LOG_WARN("[RightPanelDock] DEPRECATED colorDock() called; "
             "use colorPage() instead (returns VColorPage*).");
    return nullptr;
}

PropertiesDock*   RightPanelDock::propertiesDock()   const
{
    LOG_WARN("[RightPanelDock] DEPRECATED propertiesDock() called; "
             "use propertyPage() instead (returns VPropertyPage*).");
    return nullptr;
}

LayersDock*       RightPanelDock::layersDock()       const
{
    LOG_WARN("[RightPanelDock] DEPRECATED layersDock() called; "
             "use layerPage() instead (returns VLayerPage*).");
    return nullptr;
}

ChannelPathPanel* RightPanelDock::channelPathPanel() const
{
    LOG_WARN("[RightPanelDock] DEPRECATED channelPathPanel() called; "
             "use channelPathPage() instead (returns VChannelPathPage*).");
    return nullptr;
}

HistoryDock*      RightPanelDock::historyDock()      const
{
    LOG_WARN("[RightPanelDock] DEPRECATED historyDock() called; "
             "use historyPage() instead (returns VHistoryPage*).");
    return nullptr;
}

void RightPanelDock::setAdjustmentPanel(AdjustmentPanel* adj)
{
    Q_UNUSED(adj);
    LOG_WARN("[RightPanelDock] DEPRECATED setAdjustmentPanel() called; "
             "adjustment page is now built-in (VAdjustmentPage).");
}

} // namespace docks
