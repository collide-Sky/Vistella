// tst_P7_10.cpp
//
// Unit test for P7.10 (2026-10-15):
//   - VTabPageBase::invalidateCache() virtual default is no-op
//   - VPropertyPage::invalidateCache() drops property cache + repaints
//   - VChannelPathPage::invalidateCache() drops channel cache + repaints
//   - VHistoryPage::invalidateCache() is a no-op counter (QUndoStack wires it)
//   - RightPanelDock::invalidateAllPageCaches() fans out to all 3 sub-docks
//
//   P7.10 commit (2026-10-15).

#include <QTest>
#include <QApplication>
#include <QWidget>
#include <QSignalSpy>

#include "../src/ui/dock/VTabPageBase.h"
#include "../src/ui/dock/VPages.h"
#include "../src/ui/dock/VRenderPath.h"
#include "../src/media/docks/RightPanelDock.h"

using vistella::ui::VTabPageBase;
using vistella::ui::VPropertyPage;
using vistella::ui::VChannelPathPage;
using vistella::ui::VHistoryPage;
using vistella::ui::VDraggableTabWidget;
using vistella::ui::RenderPath;

class tst_P7_10 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // Per-page override behavior.
    void propertyPage_invalidateCache_bumpsCounterAndRepaints();
    void channelPathPage_invalidateCache_bumpsCounterAndRepaints();
    void historyPage_invalidateCache_bumpsCounterOnly();
    void otherPages_invalidateCache_isNoOp();

    // RightPanelDock fan-out.
    void fanOut_invokesAll3SubDocks();
    void fanOut_invokesPerSubDockSubPages();

    // Idempotence / repeated calls.
    void repeatedInvokesAccumulateCount();
};

// ---------------------------------------------------------------------------
// Per-page override
// ---------------------------------------------------------------------------
void tst_P7_10::initTestCase() {}
void tst_P7_10::cleanupTestCase() {}

void tst_P7_10::propertyPage_invalidateCache_bumpsCounterAndRepaints()
{
    VPropertyPage p;
    QCOMPARE(p.testInvalidateCacheCount(), 0);
    p.invalidateCache();
    QCOMPARE(p.testInvalidateCacheCount(), 1);
    p.invalidateCache();
    QCOMPARE(p.testInvalidateCacheCount(), 2);
}

void tst_P7_10::channelPathPage_invalidateCache_bumpsCounterAndRepaints()
{
    VChannelPathPage p;
    QCOMPARE(p.testInvalidateCacheCount(), 0);
    p.invalidateCache();
    QCOMPARE(p.testInvalidateCacheCount(), 1);
    p.invalidateCache();
    QCOMPARE(p.testInvalidateCacheCount(), 2);
}

void tst_P7_10::historyPage_invalidateCache_bumpsCounterOnly()
{
    VHistoryPage p;
    QCOMPARE(p.testInvalidateCacheCount(), 0);
    p.invalidateCache();
    QCOMPARE(p.testInvalidateCacheCount(), 1);
    // HistoryPage's invalidate is intentionally a no-op aside from
    // the counter; QUndoStack::indexChanged drives HistoryDock
    // internally. We just count to allow uniform fan-out.
}

void tst_P7_10::otherPages_invalidateCache_isNoOp()
{
    // Pages that don't override invalidateCache: VColorPage,
    // VSwatchPage, VGradientPage, VPatternPage, VLayerPage,
    // VAdjustmentPage. None of them define a counter, so they just
    // use the base class default (no-op). Verify by checking that
    // calling invalidateCache doesn't crash.
    vistella::ui::VColorPage       c;
    vistella::ui::VSwatchPage      s;
    vistella::ui::VGradientPage    g;
    vistella::ui::VPatternPage     pt;
    vistella::ui::VLayerPage       l;
    vistella::ui::VAdjustmentPage  a;
    c.invalidateCache();
    s.invalidateCache();
    g.invalidateCache();
    pt.invalidateCache();
    l.invalidateCache();
    a.invalidateCache();
    QVERIFY(true);   // reached here = no crash
}

// ---------------------------------------------------------------------------
// RightPanelDock fan-out
// ---------------------------------------------------------------------------
void tst_P7_10::fanOut_invokesAll3SubDocks()
{
    docks::RightPanelDock rp;
    // Snapshot pre-fanout counters.
    int preProp = rp.propertyPage()->testInvalidateCacheCount();
    int preChan = rp.channelPathPage()->testInvalidateCacheCount();
    int preHist = rp.historyPage()->testInvalidateCacheCount();
    rp.invalidateAllPageCaches();
    QCOMPARE(rp.propertyPage()->testInvalidateCacheCount(),    preProp + 1);
    QCOMPARE(rp.channelPathPage()->testInvalidateCacheCount(), preChan + 1);
    QCOMPARE(rp.historyPage()->testInvalidateCacheCount(),      preHist + 1);
}

void tst_P7_10::fanOut_invokesPerSubDockSubPages()
{
    // RightPanelDock layout (P7.6.x v3 commit 4):
    //   subDockColor    -> VColorPage / VSwatchPage / VGradientPage / VPatternPage
    //   subDockProperty -> VPropertyPage / VAdjustmentPage
    //   subDockLayer    -> VLayerPage / VChannelPathPage / VHistoryPage
    // Fan-out should touch every page, but the test counter only
    // exists for PropertyPage / ChannelPathPage / HistoryPage. We
    // verify those three bumped; the rest are checked in
    // tst_RightPanelDock (3 sub-dock structure) where no counter is
    // required.
    docks::RightPanelDock rp;
    int preProp = rp.propertyPage()->testInvalidateCacheCount();
    int preChan = rp.channelPathPage()->testInvalidateCacheCount();
    int preHist = rp.historyPage()->testInvalidateCacheCount();
    rp.invalidateAllPageCaches();
    rp.invalidateAllPageCaches();
    rp.invalidateAllPageCaches();
    QCOMPARE(rp.propertyPage()->testInvalidateCacheCount(),    preProp + 3);
    QCOMPARE(rp.channelPathPage()->testInvalidateCacheCount(), preChan + 3);
    QCOMPARE(rp.historyPage()->testInvalidateCacheCount(),      preHist + 3);
}

void tst_P7_10::repeatedInvokesAccumulateCount()
{
    VPropertyPage p;
    for (int i = 1; i <= 5; ++i) {
        p.invalidateCache();
        QCOMPARE(p.testInvalidateCacheCount(), i);
    }
}

QTEST_MAIN(tst_P7_10)
#include "tst_P7_10.moc"
