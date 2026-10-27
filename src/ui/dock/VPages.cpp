// VPages.cpp
//   + commit 4 +VHistoryPage
//   every page publishes its title as the widget's
//     window title.
//
//     VDraggableTabWidget::addPage() takes the tab label from
//     QWidget::windowTitle(), and the event filter keeps the two in sync.
//     The nine adapters below never set a window title, so all three panel
//     groups rendered three empty tabs and the user could not tell which
//     group was the colour group, the property group or the layer group.
//     setWindowTitle(pageTitle()) in every constructor fixes that at the
//     source: the page knows its own identity, so it publishes it.
#include "VPages.h"

#include <QUndoStack>

namespace vistella::ui {

// Helper: build the inner-widget layout for an adapter page. The page
// owns the inner widget; the VBoxLayout fills the adapter body with zero
// margin so the inner widget paints edge-to-edge (matches PS tab style).
namespace {
template <class T>
T* embed(QWidget* outer, T* inner)
{
    auto* layout = new QVBoxLayout(outer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(inner);
    return inner;
}
} // namespace

VColorPage::VColorPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::ColorTab(this)))
{
    setWindowTitle(pageTitle());
}

VSwatchPage::VSwatchPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::SwatchTab(this)))
{
    setWindowTitle(pageTitle());
}

VGradientPage::VGradientPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::GradientTab(this)))
{
    setWindowTitle(pageTitle());
}

VPatternPage::VPatternPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::PatternTab(this)))
{
    setWindowTitle(pageTitle());
}

VLayerPage::VLayerPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::LayersDock(this)))
{
    setWindowTitle(pageTitle());
    // the panel group provides the page switcher, so
    //   the inner tab bar of the LayersDock would be a duplicate row of
    //   tabs (and its channel/path page was only ever a placeholder).
    m_inner->setEmbeddedInPanelGroup(true);
}

VAdjustmentPage::VAdjustmentPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new AdjustmentPanel(this)))
{
    setWindowTitle(pageTitle());
}

VPropertyPage::VPropertyPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::PropertiesDock(this)))
{
    setWindowTitle(pageTitle());
}

void VPropertyPage::invalidateCache()
{
    ++m_invCount;
    // PropertiesDock has no internal cache to drop (its data is pushed
    // in from outside via setImageInfo / setSelectionBbox / etc), so
    // invalidate is effectively a repaint trigger. Future P8.1
    // incremental compositor can hook here to recompute the file-info
    // snapshot lazily on the next paint.
    update();
}

VChannelPathPage::VChannelPathPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::ChannelPathPanel(this)))
{
    setWindowTitle(pageTitle());
}

void VChannelPathPage::invalidateCache()
{
    ++m_invCount;
    if (m_inner) m_inner->update();
}

VHistoryPage::VHistoryPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::HistoryDock(this)))
{
    setWindowTitle(pageTitle());
}

void VHistoryPage::setUndoStack(QUndoStack* stack)
{
    if (m_inner) m_inner->setUndoStack(stack);
}

void VHistoryPage::invalidateCache()
{
    // HistoryDock listens to QUndoStack::indexChanged via its own
    // setUndoStack(); the only path that bypasses it is direct mutation
    // of m_undo, which we don't do. Keep the hook as a no-op counter
    // so RightPanelDock can still fan out invalidateCache() uniformly.
    ++m_invCount;
}

} // namespace vistella::ui
