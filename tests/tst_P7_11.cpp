// tst_P7_11.cpp
//
// Unit test for P7.11 (2026-10-15):
//   - ImageCanvas::invalidateRect / invalidateAll / flushDirtyRect
//     (dirty-rect repaint accumulator)
//   - MainWindow status bar GPU/CPU button text reflects V*Page stats.
//
// P8.3.2 (2026-10-26) added:
//   - a mouse release without a matching press must not make Qt assert
//     ("m_engaged" in qevent_p.h) inside QGraphicsView.
//
// We do NOT drive a full MainWindow here (that would require a windowed
// integration test). Instead, we verify the stat-counting lambda
// semantics via a direct RightPanelDock + GPU text-formatting unit.
//
//   P7.11 commit (2026-10-15).

#include <QTest>
#include <QApplication>
#include <QWidget>
#include <QToolButton>
#include <QStatusBar>
#include <QMainWindow>
#include <QMouseEvent>

#include "../src/media/imagewindow/ImageCanvas.h"
#include "../src/media/docks/RightPanelDock.h"
#include "../src/ui/dock/VPages.h"
#include "../src/ui/dock/VDraggableTabWidget.h"
#include "../src/ui/dock/VTabPageBase.h"
#include "../src/ui/dock/VRenderPath.h"

using vistella::ui::VDraggableTabWidget;
using vistella::ui::VTabPageBase;
using vistella::ui::RenderPath;
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

namespace {

// Count RenderPath distribution across 3 sub-docks of a RightPanelDock.
// Returns a tuple {gpu, cpu, auto, total}.
struct RenderStats { int gpu = 0; int cpu = 0; int au = 0; int total = 0; };
RenderStats countStats(docks::RightPanelDock* rp)
{
    RenderStats s;
    auto countPage = [&](VTabPageBase* p) {
        if (!p) return;
        ++s.total;
        switch (p->preferredRenderPath()) {
        case RenderPath::GpuPreferred: ++s.gpu; break;
        case RenderPath::CpuOnly:      ++s.cpu; break;
        case RenderPath::Auto:         ++s.au;  break;
        }
    };
    auto countSub = [&](VDraggableTabWidget* sd) {
        if (!sd) return;
        for (int i = 0; i < sd->pageCount(); ++i) {
            countPage(static_cast<VTabPageBase*>(sd->pageAt(i)));
        }
    };
    countSub(rp->subDockColor());
    countSub(rp->subDockProperty());
    countSub(rp->subDockLayer());
    return s;
}

} // namespace

class tst_P7_11 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // ImageCanvas dirty rect.
    void invalidateRect_accumulatesAcrossCalls();
    void invalidateAll_skipsAccumulation();
    void flushDirtyRect_resetsAccumulator();

    // P8.3.2 (2026-10-26): stray mouse release must not assert.
    void strayMouseRelease_doesNotAssert();

    // RenderPath stats.
    void renderStats_matchP7_11Spec();
    void statusBarTextFormat_isCorrect();
};

// ---------------------------------------------------------------------------
// ImageCanvas dirty rect
// ---------------------------------------------------------------------------
void tst_P7_11::initTestCase() {}
void tst_P7_11::cleanupTestCase() {}

void tst_P7_11::invalidateRect_accumulatesAcrossCalls()
{
    ImageCanvas canvas;
    QVERIFY(canvas.testAccDirtyRect_isNull());

    canvas.invalidateRect(QRectF(0, 0, 10, 10));
    QVERIFY(!canvas.testAccDirtyRect_isNull());

    canvas.invalidateRect(QRectF(20, 20, 30, 30));
    // After two calls, the dirty rect should be the union of both
    // (which is 0,0 -> 50,50).
    QCOMPARE(canvas.testAccDirtyRect(), QRectF(0, 0, 50, 50));
}

void tst_P7_11::invalidateAll_skipsAccumulation()
{
    ImageCanvas canvas;
    canvas.invalidateAll();
    // After invalidateAll, m_dirtyAll=true and m_pendingDirtyRect is
    // reset. A subsequent invalidateRect should still be picked up,
    // because flushDirtyRect will see the all flag and skip.
    QVERIFY(canvas.testDirtyAll());
    QCOMPARE(canvas.testAccDirtyRect(), QRectF());
}

void tst_P7_11::flushDirtyRect_resetsAccumulator()
{
    ImageCanvas canvas;
    canvas.invalidateRect(QRectF(0, 0, 10, 10));
    canvas.invalidateRect(QRectF(20, 20, 30, 30));
    canvas.testFlushDirtyRect();   // simulates drawForeground hook
    // After flush, accumulator is reset (the test exposes a hook to
    // drive the flush without a real paint event).
    QVERIFY(canvas.testAccDirtyRect_isNull());
}

// ---------------------------------------------------------------------------
// P8.3.2 (2026-10-26): a release without a matching press
// ---------------------------------------------------------------------------
void tst_P7_11::strayMouseRelease_doesNotAssert()
{
    // ImageCanvas uses QGraphicsView::ScrollHandDrag. In that mode
    // QGraphicsView::mouseReleaseEvent() reads the stored mouse event
    // BEFORE it stores the release:
    //
    //   if (dragMode == ScrollHandDrag && event->button() == LeftButton)
    //       ... !d->lastMouseEvent->isAccepted() ...
    //   d->storeMouseEvent(event);      // only now the event is stored
    //
    // So a left-button release that never had a press makes Qt read an
    // unengaged QEventStorage and abort with
    //   ASSERT: "m_engaged" in .../gui/kernel/qevent_p.h
    //
    // That is exactly what happened when a file was opened from the home
    // page: the press landed on the file list, the new canvas appeared
    // under the cursor and the release landed on the fresh canvas.
    ImageCanvas canvas;
    canvas.resize(400, 300);
    canvas.show();
    QTest::qWait(0);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(10.0, 10.0),   // local pos
                        QPointF(10.0, 10.0),   // global pos
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);

    QApplication::sendEvent(canvas.viewport(), &release);

    // Reaching this line at all means the assert did not fire.
    QVERIFY(true);
}

// ---------------------------------------------------------------------------
// RenderPath stats
// ---------------------------------------------------------------------------
void tst_P7_11::renderStats_matchP7_11Spec()
{
    // The 9 V*Page have explicit preferredRenderPath() values from
    // VPages.cpp:
    //   color, gradient, pattern, property, history    -> CpuOnly (5)
    //   swatch, layer                                  -> Auto     (2)
    //   adjustment, channelpath                        -> GpuPreferred (2)
    docks::RightPanelDock rp;
    RenderStats s = countStats(&rp);
    QCOMPARE(s.total, 9);
    QCOMPARE(s.cpu,  5);
    QCOMPARE(s.au,   2);
    QCOMPARE(s.gpu,  2);
}

void tst_P7_11::statusBarTextFormat_isCorrect()
{
    // The button text format from MainWindow::refreshGpuStatusButton
    // is "GPU: <gpu> / <total>". Verify the count produces a string
    // that the format would render correctly.
    docks::RightPanelDock rp;
    RenderStats s = countStats(&rp);
    QString formatted = QStringLiteral("GPU: %1 / %2").arg(s.gpu).arg(s.total);
    QCOMPARE(formatted, QStringLiteral("GPU: 2 / 9"));
}

QTEST_MAIN(tst_P7_11)
#include "tst_P7_11.moc"
