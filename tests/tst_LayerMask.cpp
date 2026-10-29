// tst_LayerMask.cpp
//
// Layer mask integration: LayerStack mask API + rendering + undo.
//
// protects: Layer::mask is the single mask model; a mask changes only
//           the alpha channel, never the RGB values (CP-07).
// path:     add pixel mask -> layer hides where coverage is 0 ->
//           still shows where coverage is 255 -> undo restores state.
//
// Pixel contract (docs/CONTRACTS.md 7.6): layer pixels inside the
// stack are CV_8UC4 BGRA with straight (non-premultiplied) alpha.
// Mask coverage is folded into the alpha channel by
// LayerMaskRenderer::applyAlpha().

#include <QTest>
#include <QUndoStack>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerMaskCommand.h"

using namespace layers;

class tst_LayerMask : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- Setter ----
    void test_addPixelMask();
    void test_addPixelMask_outOfRange();
    void test_clearMaskFull();
    void test_setMaskEnabled();
    void test_setMaskEnabled_noMask();

    // ---- Render ----
    void test_render_mask_hidesArea_transparently();
    void test_render_mask_disabled_keepsAll();
    void test_renderOne_appliesMask_toAlphaOnly();
    void test_render_mask_resize();

    // ---- LayerMaskCommand undo ----
    void test_addMaskUndo();
    void test_clearMaskUndo();
    void test_setEnabledUndo();

private:
    // Opaque layer in the pixel contract format (4-channel BGRA).
    //   The alpha channel must be given explicitly: cv::Scalar(b, g, r)
    //   leaves the 4th channel at 0, which would make the layer fully
    //   transparent and leave the mask nothing to modulate.
    cv::Mat makeMat(int w = 32, int h = 32, cv::Scalar bgr = cv::Scalar(255, 255, 255))
    {
        return cv::Mat(h, w, CV_8UC4, cv::Scalar(bgr[0], bgr[1], bgr[2], 255));
    }

    // Mask that keeps the left half visible and hides the right half.
    cv::Mat makeHalfMask(int w = 32, int h = 32)
    {
        cv::Mat m(h, w, CV_8U, cv::Scalar(0));
        m.colRange(0, w / 2) = 255;
        return m;
    }
};

void tst_LayerMask::initTestCase() {}
void tst_LayerMask::cleanupTestCase() {}

// =====================================================================
//  Setter
// =====================================================================

void tst_LayerMask::test_addPixelMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QVERIFY(stack.addPixelMask(0, makeHalfMask()));

    const LayerMask* m = stack.maskAt(0);
    QVERIFY(m != nullptr);
    QCOMPARE(m->kind, LayerMask::Pixel);
    QCOMPARE(m->enabled, true);
    QVERIFY(m->hasData());
    QCOMPARE(m->pixel.size(), cv::Size(32, 32));
}

void tst_LayerMask::test_addPixelMask_outOfRange()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    // Out of range index is rejected.
    QVERIFY(!stack.addPixelMask(99, makeHalfMask()));
    // An empty mask carries no data and is rejected.
    QVERIFY(!stack.addPixelMask(0, cv::Mat()));
}

void tst_LayerMask::test_clearMaskFull()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addPixelMask(0, makeHalfMask());
    QVERIFY(stack.clearMaskFull(0));

    const LayerMask* m = stack.maskAt(0);
    QVERIFY(m != nullptr);
    QCOMPARE(m->kind, LayerMask::None);
    QVERIFY(!m->enabled);
    QVERIFY(!m->hasData());
    // Clearing again changes nothing, so it reports false.
    QVERIFY(!stack.clearMaskFull(0));
}

void tst_LayerMask::test_setMaskEnabled()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addPixelMask(0, makeHalfMask());

    QVERIFY(stack.setMaskEnabled(0, false));
    QCOMPARE(stack.maskAt(0)->enabled, false);
    // Same value again is a no-op.
    QVERIFY(!stack.setMaskEnabled(0, false));

    QVERIFY(stack.setMaskEnabled(0, true));
    QCOMPARE(stack.maskAt(0)->enabled, true);
}

void tst_LayerMask::test_setMaskEnabled_noMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    // No mask data -> enabling is rejected.
    QVERIFY(!stack.setMaskEnabled(0, true));
}

// =====================================================================
//  Render
// =====================================================================

void tst_LayerMask::test_render_mask_hidesArea_transparently()
{
    LayerStack stack;
    stack.setCompositorEnabled(false);   // legacy path: deterministic order
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));
    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    stack.addPixelMask(1, makeHalfMask(32, 32));

    cv::Mat out = stack.render();
    QCOMPARE(out.size(), cv::Size(32, 32));
    QCOMPARE(out.channels(), 4);

    // Left half: coverage 255 -> pixel stays fully opaque white.
    const cv::Vec4b leftPx = out.at<cv::Vec4b>(16, 4);
    QCOMPARE(static_cast<int>(leftPx[0]), 255);
    QCOMPARE(static_cast<int>(leftPx[3]), 255);

    // Right half: coverage 0 -> the masked layer stops contributing and
    // the layer underneath (the opaque black base) shows through. The
    // result is the BASE's colour, not a darkened version of the top
    // layer -- that difference is exactly what CP-07 got wrong.
    //
    // The alpha stays 255 because the base itself is opaque; alpha 0
    // would only be correct if nothing at all were underneath.
    const cv::Vec4b rightPx = out.at<cv::Vec4b>(16, 28);
    QCOMPARE(static_cast<int>(rightPx[3]), 255);
    QCOMPARE(static_cast<int>(rightPx[0]), 0);   // base showing through
    QCOMPARE(static_cast<int>(rightPx[1]), 0);
    QCOMPARE(static_cast<int>(rightPx[2]), 0);
}

void tst_LayerMask::test_render_mask_disabled_keepsAll()
{
    LayerStack stack;
    stack.setCompositorEnabled(false);
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));
    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    stack.addPixelMask(1, makeHalfMask(32, 32));
    stack.setMaskEnabled(1, false);

    cv::Mat out = stack.render();
    QCOMPARE(out.channels(), 4);
    // A disabled mask has no effect at all: both halves stay opaque.
    QCOMPARE(static_cast<int>(out.at<cv::Vec4b>(16, 4)[3]), 255);
    QCOMPARE(static_cast<int>(out.at<cv::Vec4b>(16, 28)[3]), 255);
    QCOMPARE(static_cast<int>(out.at<cv::Vec4b>(16, 28)[0]), 255);
}

void tst_LayerMask::test_renderOne_appliesMask_toAlphaOnly()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    stack.addPixelMask(0, makeHalfMask(32, 32));

    cv::Mat out = stack.renderOne(0);
    QCOMPARE(out.size(), cv::Size(32, 32));
    QCOMPARE(out.channels(), 4);

    // Left half: fully opaque.
    const cv::Vec4b leftPx = out.at<cv::Vec4b>(16, 4);
    QCOMPARE(static_cast<int>(leftPx[3]), 255);
    // Right half: alpha 0, but the RGB channels are untouched -- the
    // mask hides the pixel, it does not paint it black.
    const cv::Vec4b rightPx = out.at<cv::Vec4b>(16, 28);
    QCOMPARE(static_cast<int>(rightPx[3]), 0);
    QCOMPARE(static_cast<int>(rightPx[0]), 255);
    QCOMPARE(static_cast<int>(rightPx[1]), 255);
    QCOMPARE(static_cast<int>(rightPx[2]), 255);
}

void tst_LayerMask::test_render_mask_resize()
{
    // A 16x16 mask on a 32x32 layer gets resized to the layer size.
    LayerStack stack;
    stack.setCompositorEnabled(false);
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));
    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    cv::Mat smallMask(16, 16, CV_8U, cv::Scalar(255));
    stack.addPixelMask(1, smallMask);

    cv::Mat out = stack.render();
    QCOMPARE(out.channels(), 4);
    // All-255 mask -> the layer stays fully opaque.
    QCOMPARE(static_cast<int>(out.at<cv::Vec4b>(16, 16)[3]), 255);
    QCOMPARE(static_cast<int>(out.at<cv::Vec4b>(16, 16)[0]), 255);
}

// =====================================================================
//  LayerMaskCommand undo
// =====================================================================

void tst_LayerMask::test_addMaskUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undo;

    const LayerMask before = stack.at(0)->mask;

    // The calling convention is: mutate the layer first, then push the
    // command holding the before/after snapshots.
    stack.addPixelMask(0, makeHalfMask());
    const LayerMask after = stack.at(0)->mask;

    undo.push(new LayerMaskCommand(&stack, 0, before, after,
                                   QStringLiteral("Add Mask")));
    QCOMPARE(stack.at(0)->mask.kind, LayerMask::Pixel);

    // undo: restore the previous (empty) mask.
    undo.undo();
    QCOMPARE(stack.at(0)->mask.kind, LayerMask::None);
    QVERIFY(!stack.at(0)->mask.hasData());
    QCOMPARE(stack.at(0)->mask.enabled, false);

    // redo: the mask comes back.
    undo.redo();
    QCOMPARE(stack.at(0)->mask.kind, LayerMask::Pixel);
    QVERIFY(stack.at(0)->mask.hasData());
    QCOMPARE(stack.at(0)->mask.enabled, true);
}

void tst_LayerMask::test_clearMaskUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addPixelMask(0, makeHalfMask());
    QUndoStack undo;

    const LayerMask before = stack.at(0)->mask;

    stack.clearMaskFull(0);
    const LayerMask after = stack.at(0)->mask;
    QCOMPARE(stack.at(0)->mask.kind, LayerMask::None);

    undo.push(new LayerMaskCommand(&stack, 0, before, after,
                                   QStringLiteral("Clear Mask")));

    // undo: the mask is restored.
    undo.undo();
    QCOMPARE(stack.at(0)->mask.kind, LayerMask::Pixel);
    QVERIFY(stack.at(0)->mask.hasData());
    QCOMPARE(stack.at(0)->mask.enabled, true);

    // redo: cleared again.
    undo.redo();
    QCOMPARE(stack.at(0)->mask.kind, LayerMask::None);
    QVERIFY(!stack.at(0)->mask.hasData());
}

void tst_LayerMask::test_setEnabledUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addPixelMask(0, makeHalfMask());
    QUndoStack undo;

    const LayerMask before = stack.at(0)->mask;

    stack.setMaskEnabled(0, false);
    const LayerMask after = stack.at(0)->mask;
    QCOMPARE(stack.at(0)->mask.enabled, false);

    undo.push(new LayerMaskCommand(&stack, 0, before, after,
                                   QStringLiteral("Toggle Mask")));

    // undo: the mask is enabled again.
    undo.undo();
    QCOMPARE(stack.at(0)->mask.enabled, true);

    // redo: disabled again.
    undo.redo();
    QCOMPARE(stack.at(0)->mask.enabled, false);
}

QTEST_MAIN(tst_LayerMask)
#include "tst_LayerMask.moc"
