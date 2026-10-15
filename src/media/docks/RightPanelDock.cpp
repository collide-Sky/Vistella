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

    // Equal-height initial split: 1/3 each.
    QList<int> sizes;
    sizes << 200 << 200 << 200;
    m_splitter->setSizes(sizes);

    LOG_INFO("[RightPanelDock] v3 commit 4 created: 3 sub-docks "
             "(color-group/property-group/layer-group)");
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

QSize RightPanelDock::sizeHint() const
{
    return QSize(340, 800);
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
