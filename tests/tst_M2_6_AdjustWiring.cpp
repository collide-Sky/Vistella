// SPDX-License-Identifier: MIT
//
// tst_M2_6_AdjustWiring.cpp - M2-6b (2026-11-03)
//
//   The AdjustmentPanel is the ONLY thing that puts an adjustment into a
//   document. Until M2-6b it only ever wrote a 1D LUT into the
//   adjustment layer, which meant the new per-pixel transforms (HSL /
//   black & white / channel mixer) were dead code in the product: only
//   the unit test called them.
//
//   This file checks the WIRING, not the maths (the maths lives in
//   tst_M2_6_AdjustTransform):
//
//     - the panel writes the per-pixel payload of the active tab into
//       the adjustment layer,
//     - the curves tab stays on the 1D LUT path,
//     - the payload reaches the render path (the composited pixels
//       really change),
//     - undo restores the WHOLE adjustment state (payload included).
//
//   The image is a RAMP: a solid-colour image would make every
//   "does this change anything" assertion vacuous.
//
#include <QtTest/QtTest>

#include "../src/media/imagewindow.h"
#include "../src/media/imagewindow/AdjustmentPanel.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/Layer.h"

#include <QApplication>
#include <QVariantMap>

#include <opencv2/core.hpp>

using layers::AdjustKind;
using layers::Layer;

namespace {

// A horizontal ramp: the R channel climbs with x, the G channel with y,
// the B channel is a diagonal.
cv::Mat makeRamp(int w, int h)
{
    cv::Mat img(h, w, CV_8UC4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uchar* p = img.ptr<uchar>(y) + x * 4;
            p[0] = static_cast<uchar>((x * 255) / (w > 1 ? w - 1 : 1));
            p[1] = static_cast<uchar>((y * 255) / (h > 1 ? h - 1 : 1));
            p[2] = static_cast<uchar>(((x + y) * 255) / ((w + h) > 1 ? (w + h - 2) : 1));
            p[3] = 255;
        }
    }
    return img;
}

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

QVariantMap hslArgs(int hueShift, int satShift, int lightness)
{
    QVariantList hues;
    QVariantList sats;
    for (int i = 0; i < 8; ++i) {
        hues << (i == 0 ? hueShift : 0);
        sats << (i == 0 ? satShift : 0);
    }
    QVariantMap args;
    args.insert(QString("hueShifts"), hues);
    args.insert(QString("satShifts"), sats);
    args.insert(QString("lightness"), lightness);
    return args;
}

}  // namespace


class tst_M2_6_AdjustWiring : public QObject
{
    Q_OBJECT

private:
    ImageWindow* m_win = nullptr;

    AdjustmentPanel* panel() const { return m_win->adjustmentPanel(); }

    // The index of the adjustment layer the panel created, or -1.
    int adjustmentLayerIndex() const
    {
        auto* stack = m_win->layerStack();
        if (!stack) return -1;
        for (int i = 0; i < stack->count(); ++i) {
            auto l = stack->at(i);
            if (l && l->kind == Layer::Adjustment) return i;
        }
        return -1;
    }

    // How many adjustment layers the document holds.
    //
    // The panel creates ONE adjustment layer per tab, so using the HSL
    // dialog must not leave a stray adjustment layer of another tab
    // behind.
    int adjustmentLayerCount() const
    {
        auto* stack = m_win->layerStack();
        if (!stack) return -1;
        int n = 0;
        for (int i = 0; i < stack->count(); ++i) {
            auto l = stack->at(i);
            if (l && l->kind == Layer::Adjustment) ++n;
        }
        return n;
    }

    Layer adjustmentLayer() const
    {
        const int idx = adjustmentLayerIndex();
        if (idx < 0) return Layer();
        return *m_win->layerStack()->at(idx);
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
        m_win->refreshCanvasNow();
    }

    void cleanup()
    {
        delete m_win;
        m_win = nullptr;
    }

    // ------------------------------------------------------------------
    // 1. The HSL dialog must land as a per-pixel payload.
    // ------------------------------------------------------------------
    void hslDialog_writesThePerPixelPayload()
    {
        panel()->setStandaloneParams(QString("HSL"), hslArgs(90, -40, 25));

        const int idx = adjustmentLayerIndex();
        QVERIFY2(idx >= 0, "the HSL dialog created no adjustment layer");
        // The panel creates ONE adjustment layer per tab: using the HSL
        // dialog must not leave a stray adjustment layer of another tab
        // behind (the sliders of a tab fire valueChanged while the
        // dialog values are pushed in, and an apply on the wrong tab
        // would create exactly that).
        QCOMPARE(adjustmentLayerCount(), 1);

        const Layer l = adjustmentLayer();
        QCOMPARE(int(l.adjustmentParams.kind), int(AdjustKind::Hsl));
        QCOMPARE(l.adjustmentParams.hueShifts.size(), 8);
        QCOMPARE(l.adjustmentParams.hueShifts.at(0), 90);
        QCOMPARE(l.adjustmentParams.satShifts.at(0), -40);
        QCOMPARE(l.adjustmentParams.lightness, 25);
    }

    // ------------------------------------------------------------------
    // 2. The black & white dialog must land as a per-pixel payload.
    // ------------------------------------------------------------------
    void blackWhiteDialog_writesTheMixers()
    {
        QVariantMap args;
        for (int i = 0; i < 6; ++i) {
            args.insert(QString("color%1").arg(i), 100 + i * 10);
        }
        panel()->setStandaloneParams(QString("B&W"), args);

        QVERIFY(adjustmentLayerIndex() >= 0);
        const Layer l = adjustmentLayer();
        QCOMPARE(int(l.adjustmentParams.kind), int(AdjustKind::BlackWhite));
        QCOMPARE(l.adjustmentParams.bwMixers.size(), 6);
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(l.adjustmentParams.bwMixers.at(i), 100.0 + i * 10);
        }
    }

    // ------------------------------------------------------------------
    // 3. The channel mixer dialog must land as a 3x3 matrix.
    //    The rows are the output channels (R, G, B) and the columns the
    //    input channels (R, G, B).
    // ------------------------------------------------------------------
    void channelMixerDialog_writesTheMatrix()
    {
        QVariantMap args;
        for (int i = 0; i < 9; ++i) {
            args.insert(QString("m%1%2").arg(i / 3).arg(i % 3), 10 * (i + 1));
        }
        args.insert(QString("monochrome"), true);
        panel()->setStandaloneParams(QString("ChannelMixer"), args);

        QVERIFY(adjustmentLayerIndex() >= 0);
        const Layer l = adjustmentLayer();
        QCOMPARE(int(l.adjustmentParams.kind), int(AdjustKind::ChannelMixer));
        for (int i = 0; i < 9; ++i) {
            QCOMPARE(l.adjustmentParams.cmMatrix[i], 10.0 * (i + 1));
        }
        QVERIFY(l.adjustmentParams.cmMonochrome);
    }

    // ------------------------------------------------------------------
    // 4. The curves tab stays on the 1D LUT path.
    // ------------------------------------------------------------------
    void curvesDialog_staysOnTheLutPath()
    {
        QVariantList pts;
        pts << QVariantMap{{"x", 0.0},   {"y", 0.0}}
            << QVariantMap{{"x", 128.0}, {"y", 90.0}}
            << QVariantMap{{"x", 255.0}, {"y", 255.0}};
        QVariantMap args;
        args.insert(QString("channel"), QString("RGB"));
        args.insert(QString("points"), pts);

        panel()->setStandaloneParams(QString("Curves"), args);

        QVERIFY(adjustmentLayerIndex() >= 0);
        const Layer l = adjustmentLayer();
        QCOMPARE(int(l.adjustmentParams.kind), int(AdjustKind::Lut));
        QVERIFY(!l.adjustmentLut.empty());
    }

    // ------------------------------------------------------------------
    // 5. The payload must reach the render path: the composited pixels
    //    have to change when the HSL payload changes.
    // ------------------------------------------------------------------
    void hslPayload_reachesTheRenderPath()
    {
        panel()->setStandaloneParams(QString("HSL"), hslArgs(0, 0, 0));
        m_win->refreshCanvasNow();
        const cv::Mat before = m_win->currentImage().clone();

        // The same tab, a real hue shift on top of it.
        panel()->setStandaloneParams(QString("HSL"), hslArgs(180, 0, 0));
        m_win->refreshCanvasNow();
        const cv::Mat after = m_win->currentImage().clone();

        QVERIFY2(countDifferent(before, after) > 0,
                 "the HSL payload never reached the render path");
    }

    // ------------------------------------------------------------------
    // 6. Undo has to restore the WHOLE adjustment state, payload
    //    included -- restoring only the LUT would leave the hue shift
    //    on the layer.
    // ------------------------------------------------------------------
    void undo_restoresTheWholeAdjustmentState()
    {
        panel()->setStandaloneParams(QString("HSL"), hslArgs(0, 0, 0));
        QVERIFY(adjustmentLayerIndex() >= 0);

        panel()->setStandaloneParams(QString("HSL"), hslArgs(120, -30, 10));
        QCOMPARE(adjustmentLayer().adjustmentParams.hueShifts.at(0), 120);

        const int undoBefore = m_win->undoStack() ? m_win->undoStack()->count() : -1;
        panel()->pushUndoCommand();
        const int undoAfter = m_win->undoStack() ? m_win->undoStack()->count() : -1;
        QVERIFY2(undoAfter == undoBefore + 1,
                 qPrintable(QString("the change pushed %1 undo entries, expected 1")
                                .arg(undoAfter - undoBefore)));

        m_win->undoStack()->undo();

        const Layer l = adjustmentLayer();
        QCOMPARE(int(l.adjustmentParams.kind), int(AdjustKind::Hsl));
        QCOMPARE(l.adjustmentParams.hueShifts.at(0), 0);
        QCOMPARE(l.adjustmentParams.satShifts.at(0), 0);
        QCOMPARE(l.adjustmentParams.lightness, 0);
    }
};

QTEST_MAIN(tst_M2_6_AdjustWiring)
#include "tst_M2_6_AdjustWiring.moc"
