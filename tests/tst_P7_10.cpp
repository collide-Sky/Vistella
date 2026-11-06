// tst_P7_10.cpp
//
// Unit test for P7.10 (2026-10-15): the right-panel cache invalidation
// fan-out.
//
// M2-2 (2026-11-06): the fan-out used to walk the 3 sub-docks and call
// invalidateCache() on every V*Page adapter. The adapter layer is gone,
// so the dock now owns the panels and counts the invalidation itself.
//
// Coverage:
//   - RightPanelDock::invalidateAllPageCaches() bumps the counter of
//     every panel that participates.
//   - Repeated calls accumulate.
//   - The fan-out reaches all three panels, not just one.
//
//   P7.10 commit (2026-10-15); M2-2 rework (2026-11-06).

#include <QTest>
#include <QApplication>
#include <QWidget>

#include "../src/media/docks/RightPanelDock.h"

class tst_P7_10 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // RightPanelDock fan-out.
    void fanOut_invokesAll3Panels();
    void fanOut_invokesEveryTime();

    // Idempotence / repeated calls.
    void repeatedInvokesAccumulateCount();

    // A fresh dock starts at zero.
    void freshDock_startsAtZero();

private:
    docks::RightPanelDock* m_dock = nullptr;
};

void tst_P7_10::initTestCase()
{
    m_dock = new docks::RightPanelDock();
}

void tst_P7_10::cleanupTestCase()
{
    delete m_dock;
    m_dock = nullptr;
}

// ---------------------------------------------------------------------------
// RightPanelDock fan-out
// ---------------------------------------------------------------------------
void tst_P7_10::fanOut_invokesAll3Panels()
{
    const int preProp = m_dock->propertyInvalidateCount();
    const int preChan = m_dock->channelPathInvalidateCount();
    const int preHist = m_dock->historyInvalidateCount();

    m_dock->invalidateAllPageCaches();

    QCOMPARE(m_dock->propertyInvalidateCount(),    preProp + 1);
    QCOMPARE(m_dock->channelPathInvalidateCount(), preChan + 1);
    QCOMPARE(m_dock->historyInvalidateCount(),      preHist + 1);
}

void tst_P7_10::fanOut_invokesEveryTime()
{
    const int preProp = m_dock->propertyInvalidateCount();
    const int preChan = m_dock->channelPathInvalidateCount();
    const int preHist = m_dock->historyInvalidateCount();

    m_dock->invalidateAllPageCaches();
    m_dock->invalidateAllPageCaches();
    m_dock->invalidateAllPageCaches();

    QCOMPARE(m_dock->propertyInvalidateCount(),    preProp + 3);
    QCOMPARE(m_dock->channelPathInvalidateCount(), preChan + 3);
    QCOMPARE(m_dock->historyInvalidateCount(),      preHist + 3);
}

void tst_P7_10::repeatedInvokesAccumulateCount()
{
    const int preProp = m_dock->propertyInvalidateCount();
    for (int i = 1; i <= 5; ++i) {
        m_dock->invalidateAllPageCaches();
        QCOMPARE(m_dock->propertyInvalidateCount(), preProp + i);
    }
}

void tst_P7_10::freshDock_startsAtZero()
{
    docks::RightPanelDock fresh;
    QCOMPARE(fresh.propertyInvalidateCount(), 0);
    QCOMPARE(fresh.channelPathInvalidateCount(), 0);
    QCOMPARE(fresh.historyInvalidateCount(), 0);
}

QTEST_MAIN(tst_P7_10)
#include "tst_P7_10.moc"
