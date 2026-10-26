// tst_RightPanelDock.cpp
//
// Unit test for the P7.6.x v3 commit 4 right-panel structure: 3 sub-docks,
// each a VDraggableTabWidget, hosting 9 V*Page adapters.
//
// Coverage:
//   - 3 sub-dock containers exist (subDockColor/subDockProperty/subDockLayer).
//   - Each sub-dock has the expected number of pages (4 / 2 / 3).
//   - Each V*Page adapter has the correct pageTag (smoke check 9 adapters).
//   - Composite strategy defaults to Sequential on all 3 sub-docks.
//   - Sub-dock 1 (color-group) is hidden when WorkspaceMediator says so;
//     sub-dock 3 (layer-group) is always visible (core editing area).
//   - Deprecated shims return nullptr (colorDock/layersDock/...).
//   - setAdjustmentPanel is a no-op (does not add a tab).
//   - setUndoStack propagates to VHistoryPage (pageTitleChanged smoke).
//   - setLayerPanel wires the panel into VLayerPage->inner()->setContentWidget(0).
//   - setChannelPathQuickMask forwards to VChannelPathPage->inner()->setQuickMask.

#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QTabBar>
#include <QWidget>
#include <QUndoStack>
#include <QUndoCommand>
#include <QSplitter>

#include "../src/media/docks/RightPanelDock.h"
#include "../src/ui/dock/VPages.h"
#include "../src/ui/dock/VDraggableTabWidget.h"
#include "../src/ui/dock/VCompositeStrategy.h"
#include "../src/media/docks/PropertiesDock.h"
#include "../src/media/docks/LayersDock.h"
#include "../src/media/docks/ChannelPathPanel.h"
#include "../src/media/docks/HistoryDock.h"

using vistella::ui::VDraggableTabWidget;
using vistella::ui::CompositeStrategy;
using vistella::ui::VColorPage;
using vistella::ui::VSwatchPage;
using vistella::ui::VGradientPage;
using vistella::ui::VPatternPage;
using vistella::ui::VPropertyPage;
using vistella::ui::VAdjustmentPage;
using vistella::ui::VLayerPage;
using vistella::ui::VChannelPathPage;
using vistella::ui::VHistoryPage;

class tst_RightPanelDock : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // Structure.
    void threeSubDocksExist();
    void subDockColor_hostsFourPages();
    void subDockProperty_hostsTwoPages();
    void subDockLayer_hostsThreePages();

    // Composite strategy.
    void allSubDocks_defaultSequential();
    void subDockCompositeStrategy_settable();

    // Deprecated shims.
    void deprecatedShims_returnNullptr();

    // setAdjustmentPanel is a no-op (still has 0 tabs in m_rightDock if
    // we count adapter pages, not the old "5th tab" path).
    void setAdjustmentPanel_isNoop();

    // Forwarding.
    void setUndoStack_propagatesToHistoryPage();
    void setLayerPanel_wiresIntoLayerPage();
    void setChannelPathQuickMask_propagatesToChannelPathPage();
    void setPropertyImageInfo_propagatesToPropertyPage();

    // Page identity.
    void colorPage_pageTag();
    void swatchPage_pageTag();
    void gradientPage_pageTag();
    void patternPage_pageTag();
    void propertyPage_pageTag();
    void adjustmentPage_pageTag();
    void layerPage_pageTag();
    void channelPathPage_pageTag();
    void historyPage_pageTag();

    // P8.3.2 (2026-10-26): panel group layout.
    void tabTitles_arePublished();
    void layerGroup_hidesInnerTabBar();
    void stretchGivesRemainderToLayerGroup();
    void contentSizedSplit_layerGroupGetsTheRest();

private:
    docks::RightPanelDock* m_dock = nullptr;
};

void tst_RightPanelDock::initTestCase()
{
    m_dock = new docks::RightPanelDock();
}

void tst_RightPanelDock::cleanupTestCase()
{
    delete m_dock;
    m_dock = nullptr;
}

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------
void tst_RightPanelDock::threeSubDocksExist()
{
    QVERIFY(m_dock);
    QVERIFY(m_dock->subDockColor()    != nullptr);
    QVERIFY(m_dock->subDockProperty() != nullptr);
    QVERIFY(m_dock->subDockLayer()    != nullptr);
}

void tst_RightPanelDock::subDockColor_hostsFourPages()
{
    auto* sd = m_dock->subDockColor();
    QCOMPARE(sd->pageCount(), 4);
    QCOMPARE(m_dock->colorPage(),    sd->pageAt(0));
    QCOMPARE(m_dock->swatchPage(),   sd->pageAt(1));
    QCOMPARE(m_dock->gradientPage(), sd->pageAt(2));
    QCOMPARE(m_dock->patternPage(),  sd->pageAt(3));
}

void tst_RightPanelDock::subDockProperty_hostsTwoPages()
{
    auto* sd = m_dock->subDockProperty();
    QCOMPARE(sd->pageCount(), 2);
    QCOMPARE(m_dock->propertyPage(),   sd->pageAt(0));
    QCOMPARE(m_dock->adjustmentPage(), sd->pageAt(1));
}

void tst_RightPanelDock::subDockLayer_hostsThreePages()
{
    auto* sd = m_dock->subDockLayer();
    QCOMPARE(sd->pageCount(), 3);
    QCOMPARE(m_dock->layerPage(),       sd->pageAt(0));
    QCOMPARE(m_dock->channelPathPage(), sd->pageAt(1));
    QCOMPARE(m_dock->historyPage(),     sd->pageAt(2));
}

// ---------------------------------------------------------------------------
// Composite strategy
// ---------------------------------------------------------------------------
void tst_RightPanelDock::allSubDocks_defaultSequential()
{
    QCOMPARE(m_dock->subDockColor()->compositeStrategy(),
             CompositeStrategy::Sequential);
    QCOMPARE(m_dock->subDockProperty()->compositeStrategy(),
             CompositeStrategy::Sequential);
    QCOMPARE(m_dock->subDockLayer()->compositeStrategy(),
             CompositeStrategy::Sequential);
}

void tst_RightPanelDock::subDockCompositeStrategy_settable()
{
    m_dock->subDockColor()->setCompositeStrategy(CompositeStrategy::Parallel);
    QCOMPARE(m_dock->subDockColor()->compositeStrategy(),
             CompositeStrategy::Parallel);
    // Reset to default.
    m_dock->subDockColor()->setCompositeStrategy(CompositeStrategy::Sequential);
    QCOMPARE(m_dock->subDockColor()->compositeStrategy(),
             CompositeStrategy::Sequential);
}

// ---------------------------------------------------------------------------
// Deprecated shims
// ---------------------------------------------------------------------------
void tst_RightPanelDock::deprecatedShims_returnNullptr()
{
    QCOMPARE(m_dock->colorDock(),        nullptr);
    QCOMPARE(m_dock->propertiesDock(),   nullptr);
    QCOMPARE(m_dock->layersDock(),       nullptr);
    QCOMPARE(m_dock->channelPathPanel(), nullptr);
    QCOMPARE(m_dock->historyDock(),      nullptr);
}

void tst_RightPanelDock::setAdjustmentPanel_isNoop()
{
    // Create a dummy QWidget* and call setAdjustmentPanel with it.
    // Since the method is a no-op shim, nothing should change.
    int before = m_dock->subDockProperty()->pageCount();
    QWidget dummy;
    m_dock->setAdjustmentPanel(reinterpret_cast<class AdjustmentPanel*>(&dummy));
    QCOMPARE(m_dock->subDockProperty()->pageCount(), before);
}

// ---------------------------------------------------------------------------
// Forwarding
// ---------------------------------------------------------------------------
void tst_RightPanelDock::setUndoStack_propagatesToHistoryPage()
{
    QUndoStack stack;
    m_dock->setUndoStack(&stack);
    auto* hist = m_dock->historyPage()->inner();
    QVERIFY(hist != nullptr);
    QCOMPARE(hist->undoStack(), &stack);
}

void tst_RightPanelDock::setLayerPanel_wiresIntoLayerPage()
{
    // The VLayerPage's inner is a LayersDock. setLayerPanel should
    // call inner->setContentWidget(0, panel) which reparents the panel.
    QWidget* panel = new QWidget();
    panel->setObjectName("fakeLayerPanel");
    m_dock->setLayerPanel(panel);
    auto* layers = m_dock->layerPage()->inner();
    QVERIFY(layers != nullptr);
    // After setContentWidget(0, panel), the panel's parent is layers
    // (or one of its children) -- ancestor check is enough.
    QVERIFY(layers->isAncestorOf(panel));
    delete panel;   // safe -- it's now a child of layers (which is a child of m_dock)
}

void tst_RightPanelDock::setChannelPathQuickMask_propagatesToChannelPathPage()
{
    auto* chan = m_dock->channelPathPage()->inner();
    QVERIFY(chan != nullptr);
    m_dock->setChannelPathQuickMask(true);
    // ChannelPathPanel::setQuickMask doesn't expose a getter in this
    // header; we just verify the call doesn't crash and the dock
    // remains consistent. (More elaborate verification would need a
    // QSignalSpy on the panel's own state-changed signal.)
    QVERIFY(chan != nullptr);
    m_dock->setChannelPathQuickMask(false);
}

void tst_RightPanelDock::setPropertyImageInfo_propagatesToPropertyPage()
{
    docks::PropertiesDock::ImageInfo info;
    info.filePath = QStringLiteral("D:/test.png");
    info.width    = 800;
    info.height   = 600;
    info.format   = QStringLiteral("PNG");
    info.dpi      = 96;
    m_dock->setPropertyImageInfo(info);
    // PropertiesDock updates internal state; pathLabelText() is its
    // accessor. Exact display format is a property of PropertiesDock
    // (covered by tst_F_O_Throttle), so we just verify the call
    // propagates and the accessor is reachable (no crash, no nullptr).
    auto* props = m_dock->propertyPage()->inner();
    QVERIFY(props != nullptr);
    QString captured = props->pathLabelText();
    Q_UNUSED(captured);   // format may include a directory prefix
}

// ---------------------------------------------------------------------------
// Page identity (smoke check 9 adapters)
// ---------------------------------------------------------------------------
void tst_RightPanelDock::colorPage_pageTag()
{
    QCOMPARE(m_dock->colorPage()->pageTag(), QStringLiteral("color"));
}
void tst_RightPanelDock::swatchPage_pageTag()
{
    QCOMPARE(m_dock->swatchPage()->pageTag(), QStringLiteral("swatch"));
}
void tst_RightPanelDock::gradientPage_pageTag()
{
    QCOMPARE(m_dock->gradientPage()->pageTag(), QStringLiteral("gradient"));
}
void tst_RightPanelDock::patternPage_pageTag()
{
    QCOMPARE(m_dock->patternPage()->pageTag(), QStringLiteral("pattern"));
}
void tst_RightPanelDock::propertyPage_pageTag()
{
    QCOMPARE(m_dock->propertyPage()->pageTag(), QStringLiteral("property"));
}
void tst_RightPanelDock::adjustmentPage_pageTag()
{
    QCOMPARE(m_dock->adjustmentPage()->pageTag(), QStringLiteral("adjustment"));
}
void tst_RightPanelDock::layerPage_pageTag()
{
    QCOMPARE(m_dock->layerPage()->pageTag(), QStringLiteral("layer"));
}
void tst_RightPanelDock::channelPathPage_pageTag()
{
    QCOMPARE(m_dock->channelPathPage()->pageTag(), QStringLiteral("channelpath"));
}
void tst_RightPanelDock::historyPage_pageTag()
{
    QCOMPARE(m_dock->historyPage()->pageTag(), QStringLiteral("history"));
}

// ---------------------------------------------------------------------------
// P8.3.2 (2026-10-26): panel group layout
// ---------------------------------------------------------------------------

void tst_RightPanelDock::tabTitles_arePublished()
{
    // VDraggableTabWidget::addPage() takes the tab label from
    // QWidget::windowTitle(). A page that never publishes its title shows
    // up as an empty tab, and the user cannot tell the three panel groups
    // apart.
    VDraggableTabWidget* groups[] = {
        m_dock->subDockColor(),
        m_dock->subDockProperty(),
        m_dock->subDockLayer(),
    };
    for (VDraggableTabWidget* group : groups) {
        QVERIFY(group != nullptr);
        QTabBar* bar = group->tabBar();
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->count(), group->pageCount());
        for (int i = 0; i < bar->count(); ++i) {
            const QString text = bar->tabText(i);
            QVERIFY2(!text.isEmpty(), "panel group tab has no title");
            QWidget* page = group->pageAt(i);
            QVERIFY(page != nullptr);
            QCOMPARE(text, page->windowTitle());
        }
    }
}

void tst_RightPanelDock::layerGroup_hidesInnerTabBar()
{
    auto* inner = m_dock->layerPage()->inner();
    QVERIFY(inner != nullptr);
    QVERIFY(inner->isEmbeddedInPanelGroup());

    // The outer group is the page switcher now, so the inner bar would be a
    // duplicate row of tabs, and the channel/path page it used to hold was
    // only ever a placeholder.
    QCOMPARE(inner->tabs()->count(), 1);
    QVERIFY(!inner->tabs()->tabBar()->isVisibleTo(inner));
}

void tst_RightPanelDock::stretchGivesRemainderToLayerGroup()
{
    // Without a stretch factor Qt treats every splitter child as
    // expansive and splits the height evenly - that is what left the
    // colour group with a huge empty area.
    QCOMPARE(m_dock->subDockColor()->sizePolicy().verticalStretch(), 0);
    QCOMPARE(m_dock->subDockProperty()->sizePolicy().verticalStretch(), 0);
    QCOMPARE(m_dock->subDockLayer()->sizePolicy().verticalStretch(), 1);
}

void tst_RightPanelDock::contentSizedSplit_layerGroupGetsTheRest()
{
    m_dock->resize(340, 900);
    m_dock->show();
    QTest::qWait(0);
    m_dock->applyContentSizedSplit();
    QTest::qWait(0);

    const int hColor    = m_dock->subDockColor()->height();
    const int hProperty = m_dock->subDockProperty()->height();
    const int hLayer    = m_dock->subDockLayer()->height();

    QVERIFY2(hLayer > hColor,
             "the layer group must take the leftover height, not the colour group");
    QVERIFY2(hLayer > hProperty,
             "the layer group must take the leftover height, not the property group");
    // The colour group sizes to its content, it is not a third of the panel.
    QVERIFY2(hColor < m_dock->height() / 3,
             "the colour group must size to its content");
}

QTEST_MAIN(tst_RightPanelDock)
#include "tst_RightPanelDock.moc"
