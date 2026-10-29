// SPDX-License-Identifier: MIT
//
// tst_AlphaMask.cpp -- the pixel / mask contract of the M1-3 milestone.
//
// protects: the rules stated in docs/CONTRACTS.md section 7.6.1
//           (INV-PIXEL-1..3, INV-MASK-1..4, INV-PATH-1..2).
// path:     layer pixels -> mask -> render -> compare.
//
// The contract in one paragraph:
//
//   Layer pixels inside the stack are ALWAYS CV_8UC4 BGRA with straight
//   alpha. The mask is the single model (Layer::mask) and it only ever
//   changes the ALPHA channel -- never the colour. The layer opacity
//   scales the COVERAGE, not the colour. Compositing is Porter-Duff
//   "over". The base image is an ordinary layer over an opaque white
//   document canvas, so hiding it really reveals what is below it. And
//   the two render paths (legacy / compositor) must agree pixel for
//   pixel.
//
// Each test below was written against a measured failure, not against
// arithmetic: the expected values come from the contract, and the
// break-self-test in tools/break_test_contracts.py injects a violation
// of each rule to prove the test can actually fail.

#include <QTest>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerMaskCommand.h"
#include "../src/media/imageworker/layers/PixelFormat.h"

using namespace layers;

namespace {

// Opaque layer in the pixel contract format (4-channel BGRA).
// The alpha must be given explicitly: cv::Scalar(b, g, r) leaves the
// 4th channel at 0, which would make the layer fully transparent.
cv::Mat makeOpaque(int w, int h, cv::Scalar bgr)
{
    return cv::Mat(h, w, CV_8UC4, cv::Scalar(bgr[0], bgr[1], bgr[2], 255));
}

// A mask that is 0 on the left half and 255 on the right half.
cv::Mat halfMask(int w, int h)
{
    cv::Mat m(h, w, CV_8UC1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            m.at<uchar>(y, x) = (x < w / 2) ? 0 : 255;
        }
    }
    return m;
}

}   // namespace

class tst_AlphaMask : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- INV-PIXEL-1: the pixel format ----
    void test_layer_pixels_are4ChannelBgra();
    void test_toBgra_normalisesWithoutCopyingAlpha();
    void test_toBgra_preservesExistingAlpha();

    // ---- INV-PIXEL-2 / INV-PIXEL-3: the compositing maths ----
    void test_opacity_scalesAlpha_notColour();
    void test_render_output_is4Channel();

    // ---- INV-MASK-1..3: the mask model ----
    void test_mask_changesAlpha_notColour();
    void test_mask_zeroCoverage_hidesLayer();
    void test_mask_fullCoverage_keepsLayer();
    void test_mask_disabled_isNoOp();

    // ---- INV-MASK-4: undoability ----
    void test_maskCommand_undoRestoresDensity();

    // ---- INV-PATH-1: the two render paths ----
    void test_legacyAndCompositor_agreeWithMask();
    void test_legacyAndCompositor_agreeWithoutMask();

    // ---- INV-PATH-2: the base image is an ordinary layer ----
    void test_hiddenBase_revealsLayerBelow();

private:
    static int maxDelta(const cv::Mat& a, const cv::Mat& b);
};

void tst_AlphaMask::initTestCase() {}
void tst_AlphaMask::cleanupTestCase() {}

int tst_AlphaMask::maxDelta(const cv::Mat& a, const cv::Mat& b)
{
    if (a.empty() || b.empty()) return 255;
    if (a.size() != b.size()) return 255;
    if (a.channels() != b.channels()) return 255;
    const cv::Mat av = a.reshape(1, a.rows);
    const cv::Mat bv = b.reshape(1, b.rows);
    int worst = 0;
    for (int y = 0; y < av.rows; ++y) {
        const uchar* ap = av.ptr<uchar>(y);
        const uchar* bp = bv.ptr<uchar>(y);
        for (int x = 0; x < av.cols * av.channels(); ++x) {
            const int d = std::abs(int(ap[x]) - int(bp[x]));
            if (d > worst) worst = d;
        }
    }
    return worst;
}

// ---------------------------------------------------------------------
//  INV-PIXEL-1: the pixel format
// ---------------------------------------------------------------------

void tst_AlphaMask::test_layer_pixels_are4ChannelBgra()
{
    // A 3-channel layer added to the stack is normalised to 4 channels.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), cv::Mat(8, 8, CV_8UC3, cv::Scalar(10, 20, 30)));

    auto l = stack.at(0);
    QVERIFY(l);
    QCOMPARE(l->image.channels(), 4);
    QCOMPARE(l->image.type(), CV_8UC4);
    // The colour survives the normalisation, and the alpha is opaque.
    QCOMPARE(int(l->image.at<cv::Vec4b>(4, 4)[0]), 10);   // B
    QCOMPARE(int(l->image.at<cv::Vec4b>(4, 4)[1]), 20);   // G
    QCOMPARE(int(l->image.at<cv::Vec4b>(4, 4)[2]), 30);   // R
    QCOMPARE(int(l->image.at<cv::Vec4b>(4, 4)[3]), 255);  // A
}

void tst_AlphaMask::test_toBgra_normalisesWithoutCopyingAlpha()
{
    // 1 channel -> gray expanded, alpha 255.
    cv::Mat gray(4, 4, CV_8UC1, cv::Scalar(128));
    const cv::Mat g = layers::toBgra(gray);
    QCOMPARE(g.channels(), 4);
    QCOMPARE(int(g.at<cv::Vec4b>(1, 1)[0]), 128);
    QCOMPARE(int(g.at<cv::Vec4b>(1, 1)[3]), 255);

    // 3 channels -> alpha 255.
    cv::Mat bgr(4, 4, CV_8UC3, cv::Scalar(1, 2, 3));
    const cv::Mat c = layers::toBgra(bgr);
    QCOMPARE(c.channels(), 4);
    QCOMPARE(int(c.at<cv::Vec4b>(1, 1)[3]), 255);
}

void tst_AlphaMask::test_toBgra_preservesExistingAlpha()
{
    // 4 channels -> kept as is, the alpha is NOT touched.
    cv::Mat bgra(4, 4, CV_8UC4, cv::Scalar(1, 2, 3, 77));
    const cv::Mat r = layers::toBgra(bgra);
    QCOMPARE(r.channels(), 4);
    QCOMPARE(int(r.at<cv::Vec4b>(1, 1)[3]), 77);
}

// ---------------------------------------------------------------------
//  INV-PIXEL-2 / INV-PIXEL-3: the compositing maths
// ---------------------------------------------------------------------

void tst_AlphaMask::test_opacity_scalesAlpha_notColour()
{
    // A layer at 50% opacity over an opaque white document canvas.
    //   The opacity scales the COVERAGE, so the result is a 50/50 mix
    //   with the white canvas -- and the canvas alpha stays 255.
    //   Scaling the colour instead would darken the result twice.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(8, 8, cv::Scalar(0, 0, 0)));
    stack.setOpacity(0, 0.5f);

    const cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.channels(), 4);

    const cv::Vec4b p = out.at<cv::Vec4b>(4, 4);
    // 0.5 * black + 0.5 * white = 127/128 per channel.
    QVERIFY(std::abs(int(p[0]) - 127) <= 1);
    QVERIFY(std::abs(int(p[1]) - 127) <= 1);
    QVERIFY(std::abs(int(p[2]) - 127) <= 1);
    // The document canvas stays opaque.
    QCOMPARE(int(p[3]), 255);
}

void tst_AlphaMask::test_render_output_is4Channel()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(8, 8, cv::Scalar(1, 2, 3)));

    const cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.channels(), 4);
    QCOMPARE(out.type(), CV_8UC4);
}

// ---------------------------------------------------------------------
//  INV-MASK-1..3: the mask model
// ---------------------------------------------------------------------

void tst_AlphaMask::test_mask_changesAlpha_notColour()
{
    // A red layer with a mask that is 0 on the left and 255 on the right.
    //   Left: the mask covers it completely -> the white canvas shows
    //         through and the red is GONE.
    //   Right: the mask does not cover it -> the red shows.
    //   The point of the assertion: where the mask is 0 the pixel is
    //   NOT dark red (the CP-07 "multiply by black" bug) -- it is the
    //   canvas colour.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(16, 16, cv::Scalar(0, 0, 255)));
    stack.addPixelMask(0, halfMask(16, 16));

    const cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.channels(), 4);

    // Left half: fully masked -> the white document canvas shows.
    const cv::Vec4b left = out.at<cv::Vec4b>(8, 1);
    QVERIFY(std::abs(int(left[0]) - 255) <= 2);   // B
    QVERIFY(std::abs(int(left[1]) - 255) <= 2);   // G
    QVERIFY(std::abs(int(left[2]) - 255) <= 2);   // R
    QCOMPARE(int(left[3]), 255);                 // A (the canvas is opaque)

    // Right half: not masked -> the red shows.
    const cv::Vec4b right = out.at<cv::Vec4b>(8, 14);
    QCOMPARE(int(right[0]), 0);                  // B
    QCOMPARE(int(right[1]), 0);                  // G
    QCOMPARE(int(right[2]), 255);                // R
}

void tst_AlphaMask::test_mask_zeroCoverage_hidesLayer()
{
    // A mask that is entirely 0 hides the layer completely: the render
    // is the bare document canvas.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(16, 16, cv::Scalar(0, 0, 255)));
    stack.addPixelMask(0, cv::Mat(16, 16, CV_8UC1, cv::Scalar(0)));

    const cv::Mat out = stack.render();
    QCOMPARE(out.channels(), 4);
    const cv::Vec4b p = out.at<cv::Vec4b>(8, 8);
    QCOMPARE(int(p[0]), 255);
    QCOMPARE(int(p[1]), 255);
    QCOMPARE(int(p[2]), 255);
    QCOMPARE(int(p[3]), 255);
}

void tst_AlphaMask::test_mask_fullCoverage_keepsLayer()
{
    // A mask that is entirely 255 keeps the layer fully visible.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(16, 16, cv::Scalar(0, 0, 255)));
    stack.addPixelMask(0, cv::Mat(16, 16, CV_8UC1, cv::Scalar(255)));

    const cv::Mat out = stack.render();
    QCOMPARE(out.channels(), 4);
    const cv::Vec4b p = out.at<cv::Vec4b>(8, 8);
    QCOMPARE(int(p[0]), 0);
    QCOMPARE(int(p[1]), 0);
    QCOMPARE(int(p[2]), 255);
}

void tst_AlphaMask::test_mask_disabled_isNoOp()
{
    // A disabled mask must not change the render at all.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(16, 16, cv::Scalar(0, 0, 255)));
    stack.addPixelMask(0, halfMask(16, 16));

    const cv::Mat masked = stack.render();

    QVERIFY(stack.setMaskEnabled(0, false));
    const cv::Mat unmasked = stack.render();

    // The left half changes from "canvas" to "red".
    QCOMPARE(int(masked.at<cv::Vec4b>(8, 1)[2]), 255);     // white canvas
    QCOMPARE(int(unmasked.at<cv::Vec4b>(8, 1)[2]), 255);   // red
    QCOMPARE(int(unmasked.at<cv::Vec4b>(8, 1)[0]), 0);     // B of the red
}

// ---------------------------------------------------------------------
//  INV-MASK-4: undoability
// ---------------------------------------------------------------------

void tst_AlphaMask::test_maskCommand_undoRestoresDensity()
{
    // The density change must be undoable: the LayerMaskCommand carries
    // a full before/after snapshot of the mask, so undo restores the
    // density (and with it feather and invert).
    LayerStack stack;
    stack.addLayer(QStringLiteral("L"), makeOpaque(16, 16, cv::Scalar(0, 0, 255)));
    stack.addPixelMask(0, cv::Mat(16, 16, CV_8UC1, cv::Scalar(255)));

    const LayerMask before = stack.at(0)->mask;
    QVERIFY(stack.setMaskDensity(0, 0.5));
    const LayerMask after = stack.at(0)->mask;
    QVERIFY(std::abs(after.density - 0.5) < 0.001);

    QUndoStack undo;
    undo.push(new LayerMaskCommand(&stack, 0, before, after,
                                   QStringLiteral("Mask Density")));

    undo.undo();
    const LayerMask restored = stack.at(0)->mask;
    QVERIFY(std::abs(restored.density - before.density) < 0.001);

    undo.redo();
    QVERIFY(std::abs(stack.at(0)->mask.density - 0.5) < 0.001);
}

// ---------------------------------------------------------------------
//  INV-PATH-1: the two render paths
// ---------------------------------------------------------------------

void tst_AlphaMask::test_legacyAndCompositor_agreeWithMask()
{
    auto build = [](LayerStack& s) {
        s.addLayer(QStringLiteral("Base"), makeOpaque(32, 32, cv::Scalar(0, 0, 255)));
        Layer masked;
        masked.name = QStringLiteral("Masked");
        masked.kind = Layer::Bitmap;
        masked.image = makeOpaque(32, 32, cv::Scalar(255, 0, 0));
        masked.mask.kind = LayerMask::Pixel;
        masked.mask.pixel = halfMask(32, 32);
        masked.mask.enabled = true;
        masked.opacity = 0.7f;
        masked.blend = Layer::SoftLight;
        s.addLayer(masked);
    };

    LayerStack comp;
    build(comp);
    comp.setCompositorEnabled(true, false);
    const cv::Mat a = comp.render();

    LayerStack legacy;
    build(legacy);
    legacy.setCompositorEnabled(false, false);
    const cv::Mat b = legacy.render();

    QVERIFY(!a.empty());
    QVERIFY(!b.empty());
    QCOMPARE(a.channels(), 4);
    QCOMPARE(b.channels(), 4);
    QCOMPARE(maxDelta(a, b), 0);
}

void tst_AlphaMask::test_legacyAndCompositor_agreeWithoutMask()
{
    auto build = [](LayerStack& s) {
        s.addLayer(QStringLiteral("Base"), makeOpaque(32, 32, cv::Scalar(0, 0, 255)));
        s.addLayer(QStringLiteral("Top"), makeOpaque(32, 32, cv::Scalar(255, 0, 0)));
        s.setOpacity(1, 0.6f);
        s.setBlend(1, Layer::Multiply);
    };

    LayerStack comp;
    build(comp);
    comp.setCompositorEnabled(true, false);
    const cv::Mat a = comp.render();

    LayerStack legacy;
    build(legacy);
    legacy.setCompositorEnabled(false, false);
    const cv::Mat b = legacy.render();

    QCOMPARE(a.channels(), 4);
    QCOMPARE(maxDelta(a, b), 0);
}

// ---------------------------------------------------------------------
//  INV-PATH-2: the base image is an ordinary layer
// ---------------------------------------------------------------------

void tst_AlphaMask::test_hiddenBase_revealsLayerBelow()
{
    // The base image is an ordinary layer over the white document
    // canvas. Hiding it really reveals what is below it -- here the
    // layer that sits under it in the stack.
    LayerStack stack;
    stack.addLayer(QStringLiteral("Under"), makeOpaque(16, 16, cv::Scalar(0, 255, 0)));
    stack.addLayer(QStringLiteral("Base"), makeOpaque(16, 16, cv::Scalar(0, 0, 255)));

    // The base (topmost here) is visible -> red wins.
    const cv::Mat shown = stack.render();
    QCOMPARE(int(shown.at<cv::Vec4b>(8, 8)[2]), 255);   // R

    stack.setVisible(1, false);
    const cv::Mat hidden = stack.render();
    // The green layer underneath now shows.
    QCOMPARE(int(hidden.at<cv::Vec4b>(8, 8)[1]), 255);  // G
    QCOMPARE(int(hidden.at<cv::Vec4b>(8, 8)[2]), 0);    // R
}

QTEST_MAIN(tst_AlphaMask)
#include "tst_AlphaMask.moc"
