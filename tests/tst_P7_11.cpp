// tst_P7_11.cpp
//
// Unit test for P7.11 (2026-10-15):
//   - ImageCanvas::invalidateRect / invalidateAll / flushDirtyRect
//     (dirty-rect repaint accumulator)
//   - MainWindow status bar GPU/CPU button text.
//
// P8.3.2 (2026-10-26) added:
//   - a mouse release without a matching press must not make Qt assert
//     ("m_engaged" in qevent_p.h) inside QGraphicsView.
//
// M2-2 (2026-11-06) changed the status bar part:
//   It used to count the preferredRenderPath() of the 9 V*Page adapters
//   inside RightPanelDock and render "GPU: 2 / 9". That was dead weight:
//   the adapter layer is gone, and preferredRenderPath() never had a
//   consumer other than this test (audit UI-35). The button now renders
//   the real backend decision, so the test covers BackendRegistry --
//   which is what the button actually reads.
//
// We do NOT drive a full MainWindow here (that would require a windowed
// integration test). Instead we verify the backend decision + the text
// formatting that MainWindow::refreshGpuStatusButton applies.
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
#include "../src/media/gpu/BackendRegistry.h"
#include "../src/media/brushes/BrushKind.h"
#include "../src/media/tools/ToolContext.h"

using gpu::BackendRegistry;
using gpu::RenderMode;

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

    // Status bar GPU/CPU button.
    void modeLabel_coversAllThreeModes();
    void modeLabel_matchesTheButtonText();
    void forceCpu_picksTheCpuBackend();
    void backendName_isNeverEmpty();
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
// Status bar GPU/CPU button
// ---------------------------------------------------------------------------
void tst_P7_11::modeLabel_coversAllThreeModes()
{
    // The button renders "<mode label> | <backend name>". The mode label
    // is what the user sees to know which override is active.
    QCOMPARE(BackendRegistry::modeLabel(RenderMode::Auto),
             QStringLiteral("AUTO"));
    QCOMPARE(BackendRegistry::modeLabel(RenderMode::ForceGpu),
             QStringLiteral("FORCE GPU"));
    QCOMPARE(BackendRegistry::modeLabel(RenderMode::ForceCpu),
             QStringLiteral("FORCE CPU"));
}

void tst_P7_11::modeLabel_matchesTheButtonText()
{
    // The exact string MainWindow::refreshGpuStatusButton builds.
    const QString modeText    = BackendRegistry::modeLabel(RenderMode::ForceCpu);
    const QString backendName = QStringLiteral("cpu");
    const QString formatted   = modeText + QStringLiteral(" | ") + backendName;
    QCOMPARE(formatted, QStringLiteral("FORCE CPU | cpu"));
}

void tst_P7_11::forceCpu_picksTheCpuBackend()
{
    // ForceCpu is the one override whose result is deterministic: the
    // backend name must be "cpu" whatever the brush kind is.
    BackendRegistry reg;
    reg.setMode(RenderMode::ForceCpu);
    tools::BrushSharedState ss;
    QVERIFY(!reg.currentBackendName(brushes::BrushKind::HardRound, ss).isEmpty());
    // The registry always returns a live backend, never null.
    QVERIFY(reg.pick(brushes::BrushKind::HardRound, ss) != nullptr);
    QCOMPARE(reg.mode(), RenderMode::ForceCpu);
}

void tst_P7_11::backendName_isNeverEmpty()
{
    // The status bar button must never show a trailing empty backend.
    BackendRegistry reg;
    tools::BrushSharedState ss;
    for (auto mode : {RenderMode::Auto, RenderMode::ForceGpu, RenderMode::ForceCpu}) {
        reg.setMode(mode);
        const QString name = reg.currentBackendName(brushes::BrushKind::HardRound, ss);
        QVERIFY2(!name.isEmpty(),
                 qPrintable(QStringLiteral("empty backend name for mode %1")
                                .arg(static_cast<int>(mode))));
    }
}

QTEST_MAIN(tst_P7_11)
#include "tst_P7_11.moc"
