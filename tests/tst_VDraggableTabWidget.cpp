// tst_VDraggableTabWidget.cpp
//
// Unit test for the PS-style draggable tab container.
//   P7.6.x v3 commit 2 (2026-10-14).
//
// Coverage:
//   - addPage / insertPage / removePage / pageAt / indexOf
//   - currentIndex / setCurrentIndex / currentPage
//   - dockedCount / floatingCount / isPageFloating
//   - undockPage / redockPage (programmatic; bypasses QDrag event loop)
//   - movePageTo (cross-widget, programmatic)
//   - setPageTitle / pageTitle (writes windowTitle; tab bar updates)
//   - compositeStrategy getter/setter (P0 placeholder)
//   - VDragManager: activeDragSource/Page, beginDrag/endDrag
//
// We deliberately do NOT drive the full QDrag event loop in the test
// (that needs a real event loop + windowing system; left to manual smoke
// tests). Programmatic movePageTo / undockPage / redockPage exercise the
// same internal move/undock code paths as the drag-driven version.

#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QWidget>
#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>

#include "../src/ui/dock/VDraggableTabWidget.h"
#include "../src/ui/dock/VDragManager.h"
#include "../src/ui/dock/VRenderPath.h"
#include "../src/ui/dock/VCompositeStrategy.h"

using vistella::ui::VDraggableTabWidget;
using vistella::ui::VDragManager;
using vistella::ui::RenderPath;
using vistella::ui::CompositeStrategy;

namespace {

// Helper: build a labelled page with a unique objectName + windowTitle.
QWidget* makePage(const QString& name)
{
    auto* w = new QWidget();
    w->setObjectName(name);
    w->setWindowTitle(name);
    return w;
}

} // namespace

class tst_VDraggableTabWidget : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // Page list API.
    void addPage_appendsInOrder();
    void insertPage_atArbitraryIndex();
    void removePage_removesFromAllStructures();
    void pageAt_indexOf_roundtrip();

    // Current index.
    void currentIndex_initialIsZero();
    void setCurrentIndex_emitsSignal();
    void currentPage_returnsActivePage();

    // Title.
    void setPageTitle_updatesTabAndTitle();

    // Float / re-dock (programmatic).
    void undockPage_createsFloatingWrapper();
    void redockPage_restoresToStack();
    void floatingCount_tracksUndockedPages();

    // Cross-widget move.
    void movePageTo_movesToTargetAtIndex();

    // Composite strategy (P0 placeholder).
    void compositeStrategy_defaultIsSequential();
    void compositeStrategy_settable();

    // VDragManager: drag handshake bookkeeping.
    void dragManager_beginEndClearsActiveSource();

private:
    QMainWindow* m_host = nullptr;
};

void tst_VDraggableTabWidget::initTestCase()
{
    m_host = new QMainWindow();
    m_host->resize(800, 600);
}

void tst_VDraggableTabWidget::cleanupTestCase()
{
    delete m_host;
    m_host = nullptr;
}

// ---------------------------------------------------------------------------
// Page list
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::addPage_appendsInOrder()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    QWidget* c = makePage(QStringLiteral("C"));
    tw->addPage(a);
    tw->addPage(b);
    tw->addPage(c);
    QCOMPARE(tw->pageCount(), 3);
    QCOMPARE(tw->pageAt(0), a);
    QCOMPARE(tw->pageAt(1), b);
    QCOMPARE(tw->pageAt(2), c);
    QCOMPARE(tw->dockedCount(), 3);
    QCOMPARE(tw->floatingCount(), 0);
    delete tw;
}

void tst_VDraggableTabWidget::insertPage_atArbitraryIndex()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    QWidget* c = makePage(QStringLiteral("C"));
    tw->addPage(a);
    tw->addPage(c);                 // order: [A, C]
    tw->insertPage(1, b);           // order: [A, B, C]
    QCOMPARE(tw->pageAt(0), a);
    QCOMPARE(tw->pageAt(1), b);
    QCOMPARE(tw->pageAt(2), c);
    QCOMPARE(tw->indexOf(b), 1);
    delete tw;
}

void tst_VDraggableTabWidget::removePage_removesFromAllStructures()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    tw->addPage(a);
    tw->addPage(b);
    tw->removePage(a);
    QCOMPARE(tw->pageCount(), 1);
    QCOMPARE(tw->pageAt(0), b);
    QCOMPARE(tw->indexOf(a), -1);
    // Removing again is a no-op.
    tw->removePage(a);
    QCOMPARE(tw->pageCount(), 1);
    delete tw;
}

void tst_VDraggableTabWidget::pageAt_indexOf_roundtrip()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* x = makePage(QStringLiteral("X"));
    tw->addPage(x);
    QCOMPARE(tw->indexOf(x), 0);
    QCOMPARE(tw->pageAt(0), x);
    // Untracked widget returns -1 / nullptr.
    QWidget* ghost = makePage(QStringLiteral("ghost"));
    QCOMPARE(tw->indexOf(ghost), -1);
    QCOMPARE(tw->pageAt(5),  nullptr);
    delete ghost;
    delete tw;
}

// ---------------------------------------------------------------------------
// Current index
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::currentIndex_initialIsZero()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    tw->addPage(a);
    QCOMPARE(tw->currentIndex(), 0);
    QCOMPARE(tw->currentPage(), a);
    delete tw;
}

void tst_VDraggableTabWidget::setCurrentIndex_emitsSignal()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    tw->addPage(a);
    tw->addPage(b);
    QSignalSpy spy(tw, &VDraggableTabWidget::currentChanged);
    tw->setCurrentIndex(1);
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1);
    QCOMPARE(args.at(1).value<QWidget*>(), b);
    delete tw;
}

void tst_VDraggableTabWidget::currentPage_returnsActivePage()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    tw->addPage(a);
    tw->addPage(b);
    tw->setCurrentIndex(1);
    QCOMPARE(tw->currentPage(), b);
    delete tw;
}

// ---------------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::setPageTitle_updatesTabAndTitle()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("old"));
    tw->addPage(a);
    tw->setPageTitle(a, QStringLiteral("new"));
    QCOMPARE(tw->pageTitle(a), QStringLiteral("new"));
    QCOMPARE(a->windowTitle(), QStringLiteral("new"));
    delete tw;
}

// ---------------------------------------------------------------------------
// Float / re-dock (programmatic)
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::undockPage_createsFloatingWrapper()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    tw->addPage(a);
    tw->addPage(b);

    QSignalSpy spy(tw, &VDraggableTabWidget::pageUndocked);
    tw->undockPage(a);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<QWidget*>(), a);

    QCOMPARE(tw->dockedCount(), 1);
    QCOMPARE(tw->floatingCount(), 1);
    QVERIFY(tw->isPageFloating(a));
    QVERIFY(!tw->isPageFloating(b));

    delete tw;
}

void tst_VDraggableTabWidget::redockPage_restoresToStack()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    tw->addPage(a);
    tw->addPage(b);
    tw->undockPage(a);
    QVERIFY(tw->isPageFloating(a));

    QSignalSpy spy(tw, &VDraggableTabWidget::pageRedocked);
    tw->redockPage(a, 1);    // insert at position 1 (between existing B and end)
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).value<QWidget*>(), a);
    QCOMPARE(args.at(1).toInt(), 1);

    QCOMPARE(tw->dockedCount(), 2);
    QCOMPARE(tw->floatingCount(), 0);
    QVERIFY(!tw->isPageFloating(a));
    QCOMPARE(tw->pageAt(1), a);

    delete tw;
}

void tst_VDraggableTabWidget::floatingCount_tracksUndockedPages()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    QWidget* c = makePage(QStringLiteral("C"));
    tw->addPage(a);
    tw->addPage(b);
    tw->addPage(c);
    tw->undockPage(a);
    tw->undockPage(c);
    QCOMPARE(tw->dockedCount(), 1);
    QCOMPARE(tw->floatingCount(), 2);
    tw->redockPage(a);
    QCOMPARE(tw->dockedCount(), 2);
    QCOMPARE(tw->floatingCount(), 1);
    delete tw;
}

// ---------------------------------------------------------------------------
// Cross-widget move
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::movePageTo_movesToTargetAtIndex()
{
    auto* src = new VDraggableTabWidget(m_host);
    auto* dst = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    QWidget* b = makePage(QStringLiteral("B"));
    QWidget* c = makePage(QStringLiteral("C"));
    QWidget* d = makePage(QStringLiteral("D"));
    src->addPage(a);
    src->addPage(b);
    dst->addPage(c);
    dst->addPage(d);

    QSignalSpy movedSpy(src, &VDraggableTabWidget::pageMoved);
    QSignalSpy addedSpy(dst, &VDraggableTabWidget::pageAdded);

    // Move 'a' from src -> dst at index 1.
    src->movePageTo(a, dst, 1);

    QCOMPARE(movedSpy.count(), 1);
    QList<QVariant> m = movedSpy.takeFirst();
    QCOMPARE(m.at(0).value<QWidget*>(), a);
    QCOMPARE(m.at(1).value<VDraggableTabWidget*>(), dst);
    QCOMPARE(m.at(2).toInt(), 1);

    QCOMPARE(addedSpy.count(), 1);

    // src now has only [b].
    QCOMPARE(src->pageCount(), 1);
    QCOMPARE(src->pageAt(0), b);
    QCOMPARE(src->indexOf(a), -1);

    // dst now has [c, a, d].
    QCOMPARE(dst->pageCount(), 3);
    QCOMPARE(dst->pageAt(0), c);
    QCOMPARE(dst->pageAt(1), a);
    QCOMPARE(dst->pageAt(2), d);

    // Page is now somewhere under dst (parented into its QStackedWidget
    // or its QTabBar for a floating page; we only check the ancestor
    // chain here, not the immediate parent which is the internal
    // QStackedWidget).
    QVERIFY(dst->isAncestorOf(a));

    delete src;
    delete dst;
}

// ---------------------------------------------------------------------------
// Composite strategy (P0 placeholder)
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::compositeStrategy_defaultIsSequential()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QCOMPARE(tw->compositeStrategy(), CompositeStrategy::Sequential);
    delete tw;
}

void tst_VDraggableTabWidget::compositeStrategy_settable()
{
    auto* tw = new VDraggableTabWidget(m_host);
    tw->setCompositeStrategy(CompositeStrategy::Parallel);
    QCOMPARE(tw->compositeStrategy(), CompositeStrategy::Parallel);
    // Back to default.
    tw->setCompositeStrategy(CompositeStrategy::Sequential);
    QCOMPARE(tw->compositeStrategy(), CompositeStrategy::Sequential);
    delete tw;
}

// ---------------------------------------------------------------------------
// VDragManager drag handshake
// ---------------------------------------------------------------------------
void tst_VDraggableTabWidget::dragManager_beginEndClearsActiveSource()
{
    auto* tw = new VDraggableTabWidget(m_host);
    QWidget* a = makePage(QStringLiteral("A"));
    tw->addPage(a);

    VDragManager& mgr = VDragManager::instance();
    QCOMPARE(mgr.activeDragSource(), nullptr);
    QCOMPARE(mgr.activeDragPage(),   nullptr);

    QSignalSpy startedSpy(&mgr, &VDragManager::dragStarted);
    QSignalSpy endedSpy(&mgr,   &VDragManager::dragEnded);

    mgr.beginDrag(tw, a);
    QCOMPARE(mgr.activeDragSource(), tw);
    QCOMPARE(mgr.activeDragPage(),   a);
    QCOMPARE(startedSpy.count(), 1);

    mgr.endDrag();
    QCOMPARE(mgr.activeDragSource(), nullptr);
    QCOMPARE(mgr.activeDragPage(),   nullptr);
    QCOMPARE(endedSpy.count(), 1);

    delete tw;
}

QTEST_MAIN(tst_VDraggableTabWidget)
#include "tst_VDraggableTabWidget.moc"
