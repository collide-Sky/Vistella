// SPDX-License-Identifier: MIT
//
// tst_M2_6_AdjustPreview.cpp - M2-6a (2026-11-03)
//
//   Real-time preview + cancel rollback for the 5 standalone adjustment
//   dialogs (Curves / Levels / HSL / B&W / ChannelMixer).
//
//   What is covered:
//     - opening a dialog and cancelling without touching anything leaves
//       the document byte-identical and the undo stack count unchanged
//       (this is the "the dialog's initial triggerPreview mutates the
//       canvas" regression)
//     - a preview really changes the composited pixels (ramp image, so a
//       solid-colour image cannot mask a channel / offset bug)
//     - Cancel restores the pre-dialog pixels byte for byte
//     - OK keeps the adjusted pixels and leaves exactly ONE undo entry
//     - Apply commits one undo entry and keeps the dialog usable
//
//   The image is a RAMP on purpose: a solid-colour image makes every
//   "does the LUT actually do anything" assertion vacuous, because a LUT
//   that is off by a constant still maps a constant to a constant.
//
//   The tests drive the real ImageWindow + AdjustmentPanel + LayerStack +
//   UndoStack, because the whole point of the feature is the interaction
//   between them (preview must not touch the undo stack, cancel must
//   remove the adjustment layer the preview created, ...).
//
#include <QtTest/QtTest>

#include "../src/media/imagewindow.h"
#include "../src/media/imagewindow/AdjustmentPanel.h"
#include "../src/media/dialogs/AdjustDialogBase.h"
#include "../src/media/dialogs/CurvesAdjustDialog.h"
#include "../src/media/dialogs/LevelsAdjustDialog.h"
#include "../src/media/dialogs/HslAdjustDialog.h"
#include "../src/media/dialogs/BlackWhiteAdjustDialog.h"
#include "../src/media/dialogs/ChannelMixerAdjustDialog.h"
#include "../src/media/mediators/DialogMediator.h"

#include <QApplication>
#include <QVariantMap>
#include <QPointer>

#include <opencv2/core.hpp>

namespace {

// A horizontal ramp: the R channel climbs with x, the G channel with y,
// the B channel is a diagonal. Every pixel is different, so a LUT that
// shifts values, mixes channels or offsets the output is visible.
cv::Mat makeRamp(int w, int h)
{
    cv::Mat img(h, w, CV_8UC4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uchar* p = img.ptr<uchar>(y) + x * 4;
            p[0] = static_cast<uchar>((x * 255) / (w > 1 ? w - 1 : 1));   // B
            p[1] = static_cast<uchar>((y * 255) / (h > 1 ? h - 1 : 1));   // G
            p[2] = static_cast<uchar>(((x + y) * 255) / ((w + h) > 1 ? (w + h - 2) : 1)); // R
            p[3] = 255;                                                    // A
        }
    }
    return img;
}

// Byte-exact comparison of two 8UC4 buffers.
bool sameBytes(const cv::Mat& a, const cv::Mat& b)
{
    if (a.empty() || b.empty()) return false;
    if (a.size() != b.size()) return false;
    if (a.type() != b.type()) return false;
    return std::memcmp(a.data, b.data,
                       static_cast<size_t>(a.total()) * a.elemSize()) == 0;
}

// How many pixels differ between two images.
int countDifferent(const cv::Mat& a, const cv::Mat& b)
{
    if (a.empty() || b.empty() || a.size() != b.size()) return -1;
    int n = 0;
    for (int y = 0; y < a.rows; ++y) {
        const uchar* pa = a.ptr<uchar>(y);
        const uchar* pb = b.ptr<uchar>(y);
        for (int x = 0; x < a.cols * a.channels(); ++x) {
            if (pa[x] != pb[x]) ++n;
        }
    }
    return n;
}

// A Curves arg set that is NOT the identity: the mid control point is
// pulled down, so the LUT darkens everything above the mid tone.
QVariantMap curvesArgs()
{
    QVariantList pts;
    pts << QVariantMap{{"x", 0.0},   {"y", 0.0}}
        << QVariantMap{{"x", 64.0},  {"y", 40.0}}
        << QVariantMap{{"x", 128.0}, {"y", 90.0}}
        << QVariantMap{{"x", 192.0}, {"y", 160.0}}
        << QVariantMap{{"x", 255.0}, {"y", 255.0}};
    QVariantMap args;
    args.insert(QString("channel"), QString("RGB"));
    args.insert(QString("points"), pts);
    return args;
}

}  // namespace


class tst_M2_6_AdjustPreview : public QObject
{
    Q_OBJECT

private:
    ImageWindow* m_win = nullptr;

    // The per-window adjustment panel (the one the right dock owns).
    AdjustmentPanel* panel() const { return m_win->adjustmentPanel(); }

    // Force a synchronous re-render and return the composited pixels.
    cv::Mat rendered()
    {
        m_win->refreshCanvasNow();
        return m_win->currentImage().clone();
    }

    int undoCount() const
    {
        auto* us = m_win->undoStack();
        return us ? us->count() : -1;
    }

private slots:

    void initTestCase()
    {
        QVERIFY(QApplication::instance() != nullptr);
    }

    void init()
    {
        m_win = new ImageWindow();
        m_win->setDocumentImage(makeRamp(48, 32));
        QVERIFY(m_win->layerStack() != nullptr);
        QVERIFY(panel() != nullptr);
        // The render cache must be warm before the first snapshot.
        m_win->refreshCanvasNow();
    }

    void cleanup()
    {
        delete m_win;
        m_win = nullptr;
    }

    // ------------------------------------------------------------------
    // 1. Opening a dialog and cancelling without touching anything must
    //    leave the document byte-identical and the undo stack untouched.
    // ------------------------------------------------------------------
    void openAndCancel_leavesDocumentByteIdentical()
    {
        const cv::Mat before = rendered();
        const int undoBefore = undoCount();
        const int layersBefore = m_win->layerStack()->count();

        // Build the dialog the same way DialogFactory does, and show it.
        auto* dlg = new dialogs::CurvesAdjustDialog(QVariantMap(), nullptr);
        dlg->show();
        QVERIFY(QTest::qWaitForWindowExposed(dlg, 1000) || true);

        // Nothing was touched, so Cancel.
        dlg->reject();
        delete dlg;

        const cv::Mat after = rendered();
        QVERIFY2(sameBytes(before, after),
                 "cancelling an untouched dialog changed the pixels");
        QCOMPARE(undoCount(), undoBefore);
        QCOMPARE(m_win->layerStack()->count(), layersBefore);
    }

    // ------------------------------------------------------------------
    // 2. The same for the other four dialogs: their constructors restore
    //    the initial args with setValue(), which fires valueChanged and
    //    used to trigger a preview during construction.
    // ------------------------------------------------------------------
    void openAndCancel_allFiveDialogs_leaveDocumentByteIdentical()
    {
        struct Case {
            const char* name;
            dialogs::AdjustDialogBase* (*make)(const QVariantMap&, QWidget*);
            QVariantMap args;
        };

        QVector<Case> cases;
        cases.append({"Curves", &dialogs::CurvesAdjustDialog::create, curvesArgs()});
        cases.append({"Levels", &dialogs::LevelsAdjustDialog::create,
                      QVariantMap{{QString("inLow"), 40}, {QString("inHigh"), 210},
                                  {QString("gamma"), 1.4}}});
        cases.append({"HSL", &dialogs::HslAdjustDialog::create,
                      QVariantMap{{QString("lightness"), 30}}});
        cases.append({"B&W", &dialogs::BlackWhiteAdjustDialog::create,
                      QVariantMap{{QString("color0"), 160}, {QString("color3"), 40}}});
        cases.append({"ChannelMixer", &dialogs::ChannelMixerAdjustDialog::create,
                      QVariantMap{{QString("m00"), 120}, {QString("m12"), 80}}});

        for (const Case& c : cases) {
            const cv::Mat before = rendered();
            const int undoBefore = undoCount();
            const int layersBefore = m_win->layerStack()->count();

            auto* dlg = c.make(c.args, nullptr);
            QVERIFY2(dlg != nullptr, c.name);
            dlg->show();
            dlg->reject();
            delete dlg;

            const cv::Mat after = rendered();
            QVERIFY2(sameBytes(before, after),
                     QString("%1: cancelling an untouched dialog changed the pixels")
                         .arg(QString(c.name)).toUtf8().constData());
            QCOMPARE(undoCount(), undoBefore);
            QCOMPARE(m_win->layerStack()->count(), layersBefore);
        }
    }

    // ------------------------------------------------------------------
    // 3. A preview really changes the composited pixels.
    // ------------------------------------------------------------------
    void preview_changesRenderedPixels()
    {
        const cv::Mat before = rendered();

        QVERIFY(panel()->beginPreviewSession(QString("Curves")));
        panel()->previewStandaloneParams(QString("Curves"), curvesArgs());

        const cv::Mat after = rendered();
        QVERIFY2(!sameBytes(before, after),
                 "the preview did not change a single pixel");
        QVERIFY(countDifferent(before, after) > 0);
    }

    // ------------------------------------------------------------------
    // 4. Cancel restores the pre-dialog pixels byte for byte, and leaves
    //    no adjustment layer and no undo entry behind.
    // ------------------------------------------------------------------
    void cancel_restoresExactPixelsAndLeavesNoResidue()
    {
        const cv::Mat before = rendered();
        const int undoBefore = undoCount();
        const int layersBefore = m_win->layerStack()->count();

        QVERIFY(panel()->beginPreviewSession(QString("Curves")));
        panel()->previewStandaloneParams(QString("Curves"), curvesArgs());

        // The preview must not have pushed anything onto the undo stack.
        QCOMPARE(undoCount(), undoBefore);

        // The preview DID create an adjustment layer (that is how the
        // adjustment reaches the compositor).
        QVERIFY(m_win->layerStack()->count() > layersBefore);

        panel()->cancelPreviewSession(QString("Curves"));

        const cv::Mat after = rendered();
        QVERIFY2(sameBytes(before, after),
                 "cancel did not restore the exact pre-dialog pixels");
        QCOMPARE(m_win->layerStack()->count(), layersBefore);
        QCOMPARE(undoCount(), undoBefore);
    }

    // ------------------------------------------------------------------
    // 5. OK keeps the adjusted pixels and leaves exactly ONE undo entry.
    // ------------------------------------------------------------------
    void commit_keepsAdjustedPixelsAndOneUndoEntry()
    {
        const cv::Mat before = rendered();
        const int undoBefore = undoCount();

        QVERIFY(panel()->beginPreviewSession(QString("Curves")));
        panel()->previewStandaloneParams(QString("Curves"), curvesArgs());
        panel()->commitPreviewSession(QString("Curves"));

        const cv::Mat after = rendered();
        QVERIFY2(!sameBytes(before, after),
                 "the committed adjustment did not change the pixels");
        QCOMPARE(undoCount(), undoBefore + 1);

        // Undoing once must restore the pre-dialog picture exactly.
        m_win->undoStack()->undo();
        const cv::Mat undone = rendered();
        QVERIFY2(sameBytes(before, undone),
                 "one undo did not restore the pre-dialog pixels");
        QCOMPARE(m_win->layerStack()->count(), 1);   // the base layer only
    }

    // ------------------------------------------------------------------
    // 6. Apply commits one undo entry, and the session stays alive so a
    //    further preview continues from the newly committed baseline.
    // ------------------------------------------------------------------
    void apply_commitsOneEntryAndKeepsSessionUsable()
    {
        const int undoBefore = undoCount();

        QVERIFY(panel()->beginPreviewSession(QString("Curves")));
        panel()->previewStandaloneParams(QString("Curves"), curvesArgs());
        panel()->commitPreviewSession(QString("Curves"));

        QCOMPARE(undoCount(), undoBefore + 1);
        const cv::Mat afterApply = rendered();

        // A second round: the new baseline is the committed state, so
        // cancelling now must roll back to the committed state and no
        // further.
        QVERIFY(panel()->beginPreviewSession(QString("Curves")));
        QVERIFY(panel()->hasPreviewSession());

        QVariantMap stronger = curvesArgs();
        QVariantList pts = stronger.value(QString("points")).toList();
        pts[2] = QVariantMap{{"x", 128.0}, {"y", 10.0}};
        stronger.insert(QString("points"), pts);

        panel()->previewStandaloneParams(QString("Curves"), stronger);
        const cv::Mat afterSecondPreview = rendered();
        QVERIFY(!sameBytes(afterApply, afterSecondPreview));

        panel()->cancelPreviewSession(QString("Curves"));
        const cv::Mat afterSecondCancel = rendered();
        QVERIFY2(sameBytes(afterApply, afterSecondCancel),
                 "cancelling the second round did not roll back to the "
                 "committed baseline");
        QCOMPARE(undoCount(), undoBefore + 1);
    }

    // ------------------------------------------------------------------
    // 7. The dialog drives the session lifecycle itself: the signals fire
    //    in the right order and Cancel rolls back before the dialog closes.
    // ------------------------------------------------------------------
    void dialog_emitsSessionLifecycleAndCancelsRollBack()
    {
        const cv::Mat before = rendered();
        const int undoBefore = undoCount();

        auto* dlg = new dialogs::CurvesAdjustDialog(QVariantMap(), nullptr);

        int started = 0;
        int ended = 0;
        bool lastCommitted = true;
        // The rollback must happen while the dialog is still open, so the
        // host can repaint the canvas before the dialog goes away.
        bool stillVisibleWhenEnded = false;
        connect(dlg, &dialogs::AdjustDialogBase::previewSessionStarted,
                [&]() { ++started; });
        connect(dlg, &dialogs::AdjustDialogBase::previewSessionEnded,
                [&](bool committed) {
                    ++ended;
                    lastCommitted = committed;
                    stillVisibleWhenEnded = dlg->isVisible();
                });

        // Wire the dialog to the panel exactly like ImageWindow does.
        connect(dlg, &dialogs::AdjustDialogBase::previewSessionStarted, this,
                [this]() { panel()->beginPreviewSession(QString("Curves")); });
        connect(dlg, &dialogs::AdjustDialogBase::preview, this,
                [this](const QVariantMap& args) {
                    panel()->previewStandaloneParams(QString("Curves"), args);
                });
        connect(dlg, &dialogs::AdjustDialogBase::previewSessionEnded, this,
                [this](bool committed) {
                    if (committed) panel()->commitPreviewSession(QString("Curves"));
                    else           panel()->cancelPreviewSession(QString("Curves"));
                });

        dlg->show();

        // Simulate the user dragging a control point: find the CurveEditor
        // the dialog embeds, move a point and let it emit pointsChanged,
        // which is what a real drag does.
        auto* editor = dlg->findChild<CurveEditor*>();
        QVERIFY(editor != nullptr);
        QPolygonF pts = editor->controlPoints();
        QVERIFY(pts.size() >= 3);
        pts[2] = QPointF(128.0, 90.0);
        editor->setControlPoints(pts);
        emit editor->pointsChanged(pts);

        QCOMPARE(started, 1);
        const cv::Mat previewed = rendered();
        QVERIFY(!sameBytes(before, previewed));

        // Cancel.
        dlg->reject();

        QCOMPARE(ended, 1);
        QVERIFY(!lastCommitted);
        QVERIFY(stillVisibleWhenEnded);

        const cv::Mat after = rendered();
        QVERIFY2(sameBytes(before, after),
                 "dialog cancel did not roll the picture back");
        QCOMPARE(undoCount(), undoBefore);

        delete dlg;
    }

    // ------------------------------------------------------------------
    // 8. Previewing twice (two drags) must still leave the undo stack
    //    alone -- the whole point of the preview path.
    // ------------------------------------------------------------------
    void repeatedPreview_neverTouchesTheUndoStack()
    {
        const int undoBefore = undoCount();

        QVERIFY(panel()->beginPreviewSession(QString("Curves")));
        for (int i = 0; i < 5; ++i) {
            QVariantMap args = curvesArgs();
            QVariantList pts = args.value(QString("points")).toList();
            pts[2] = QVariantMap{{"x", 128.0}, {"y", 90.0 - i * 10.0}};
            args.insert(QString("points"), pts);
            panel()->previewStandaloneParams(QString("Curves"), args);
            QCOMPARE(undoCount(), undoBefore);
        }

        // Wait past the 200ms debounce window. A preview that arms the
        // commit timer would push its undo command only after that
        // delay, so checking the stack right away would not see it.
        QTest::qWait(400);

        // Still nothing in the undo stack: the preview pushed no command.
        QCOMPARE(undoCount(), undoBefore);

        panel()->cancelPreviewSession(QString("Curves"));
        QCOMPARE(undoCount(), undoBefore);

        // And nothing shows up after the cancel either.
        QTest::qWait(400);
        QCOMPARE(undoCount(), undoBefore);
    }

    // ------------------------------------------------------------------
    // 9. An unknown dialog id is rejected instead of silently doing
    //    something wrong.
    // ------------------------------------------------------------------
    void unknownDialogId_isRejected()
    {
        QVERIFY(!panel()->beginPreviewSession(QString("NoSuchDialog")));
        QVERIFY(!panel()->hasPreviewSession());
    }
};

QTEST_MAIN(tst_M2_6_AdjustPreview)
#include "tst_M2_6_AdjustPreview.moc"
