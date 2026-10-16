// VPages.cpp
//   P7.6.x v3 commit 3 (2026-10-14) + commit 4 (2026-10-15): +VHistoryPage
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
}

VSwatchPage::VSwatchPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::SwatchTab(this)))
{
}

VGradientPage::VGradientPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::GradientTab(this)))
{
}

VPatternPage::VPatternPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::PatternTab(this)))
{
}

VLayerPage::VLayerPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::LayersDock(this)))
{
}

VAdjustmentPage::VAdjustmentPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new AdjustmentPanel(this)))
{
}

VPropertyPage::VPropertyPage(QWidget* parent)
    : VTabPageBase(parent)
    , m_inner(embed(this, new docks::PropertiesDock(this)))
{
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
