// =============================================================================
//  tst_M2_6_AdjustTransform - regression tests for the M2-6b adjustment
//  transforms.
//
//  M2-6b made three adjustments actually change the image. They used to
//  be silently broken:
//
//    HSL          buildHueSatLUT() did Q_UNUSED(hueSatPairs): only the
//                 lightness slider did anything, hue and saturation were
//                 dead (docs/DEBT.md#A-HUESATLUT).
//    B&W          buildBlackWhiteLUT() averaged the 6 mixers and applied
//                 a gamma-like scale: the mixers themselves did nothing.
//    ChannelMixer there was no buildChannelMixerLUT at all, so the 3x3
//                 matrix the dialog produced never reached the pixels.
//
//  A 1D LUT cannot express any of them -- they are per-pixel RGB
//  transforms. The adjustment layer therefore carries a per-pixel
//  payload (layers::AdjustmentParams) alongside the LUT.
//
//  Every pixel-level assertion uses a RAMP image (the value varies with
//  the position). A uniform image hides channel / offset mistakes
//  completely, so it is not allowed here.
// =============================================================================

#include <QTest>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/AdjustmentTransform.h"
#include "../src/media/imageworker/layers/LayerStack.h"

using layers::AdjustKind;
using layers::AdjustmentParams;
using layers::applyAdjustmentTransform;
using layers::applyChannelLuts;

class tst_M2_6_AdjustTransform : public QObject
{
    Q_OBJECT

private slots:
    // --- identity ---
    void identityParams_leaveTheImageByteIdentical();
    void identityParams_reportNoTransform();

    // --- HSL ---
    void hslHueShift_changesTheImage();
    void hslHueShift_movesRedTowardsGreen();
    void hslSaturationMinus100_turnsTheImageGrey();
    void hslLightnessShift_brightensTheImage();

    // --- B&W ---
    void blackWhite_neutralMixers_produceAGreyImage();
    void blackWhite_mixersChangeTheGreyValue();

    // --- ChannelMixer ---
    void channelMixer_pureRedOutputRow_zeroesGreenAndBlue();
    void channelMixer_monochrome_makesAllChannelsEqual();
    void channelMixer_scalesTheOutput();

    // --- per-channel LUTs ---
    void channelLuts_liftOnlyTheRedChannel();
    void channelLuts_aSingleLutAppliesToEveryChannel();

    // --- the two render paths must agree ---
    void legacyAndCompositorPaths_agreeOnATransform();
};

// =============================================================================
//  helpers
// =============================================================================

namespace {

// A BGR ramp: the value grows with x and y, and the three channels have
// DIFFERENT slopes, so a channel swap or an off-by-one in the plane
// addressing shows up immediately.
cv::Mat makeRamp(int w, int h)
{
    cv::Mat img(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.at<cv::Vec3b>(y, x) = cv::Vec3b(
                static_cast<uchar>(x * 3),
                static_cast<uchar>(y * 5),
                static_cast<uchar>(x + y));
        }
    }
    return img;
}

// A pure red image, for the hue-shift direction test.
cv::Mat makeRed(int w, int h)
{
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(0, 0, 255));
    return img;
}

// The identity LUT.
cv::Mat identityLut()
{
    cv::Mat lut(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) lut.at<uchar>(i) = static_cast<uchar>(i);
    return lut;
}

// A LUT that maps everything to `value`.
cv::Mat constantLut(int value)
{
    cv::Mat lut(256, 1, CV_8U, cv::Scalar(value));
    return lut;
}

} // namespace

// =============================================================================
//  identity
// =============================================================================

void tst_M2_6_AdjustTransform::identityParams_leaveTheImageByteIdentical()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::Hsl;   // all shifts are zero -> identity
    p.hueShifts = QVector<int>(8, 0);
    p.satShifts  = QVector<int>(8, 0);
    p.lightness = 0;

    // The transform reports "nothing to do" and leaves `out` alone.
    QVERIFY(!applyAdjustmentTransform(src, p, out));
    QVERIFY(out.empty());
}

void tst_M2_6_AdjustTransform::identityParams_reportNoTransform()
{
    AdjustmentParams hsl;
    hsl.kind = AdjustKind::Hsl;
    hsl.hueShifts = QVector<int>(8, 0);
    hsl.satShifts  = QVector<int>(8, 0);
    QVERIFY(hsl.isIdentity());

    AdjustmentParams bw;
    bw.kind = AdjustKind::BlackWhite;
    bw.bwMixers = QVector<double>(6, 100.0);   // neutral
    // NOT the identity: a Black & White layer turns the image grey
    // even with neutral mixers. Only an EMPTY mixer set is the
    // identity (nothing was configured at all).
    QVERIFY(!bw.isIdentity());
    AdjustmentParams bwEmpty;
    bwEmpty.kind = AdjustKind::BlackWhite;
    QVERIFY(bwEmpty.isIdentity());

    AdjustmentParams cm;
    cm.kind = AdjustKind::ChannelMixer;
    QVERIFY(cm.isIdentity());                   // the default matrix
}

// =============================================================================
//  HSL
// =============================================================================

void tst_M2_6_AdjustTransform::hslHueShift_changesTheImage()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::Hsl;
    p.hueShifts = QVector<int>(8, 0);
    p.satShifts  = QVector<int>(8, 0);
    // Shift the master band by 40 degrees.
    p.hueShifts[0] = 40;

    QVERIFY(applyAdjustmentTransform(src, p, out));
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), src.size());

    // At least one pixel must have changed.
    bool anyChanged = false;
    for (int y = 0; y < src.rows && !anyChanged; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            if (src.at<cv::Vec3b>(y, x) != out.at<cv::Vec3b>(y, x)) {
                anyChanged = true;
                break;
            }
        }
    }
    QVERIFY2(anyChanged, "a hue shift of 40 degrees changed nothing");
}

void tst_M2_6_AdjustTransform::hslHueShift_movesRedTowardsGreen()
{
    // A red pixel (BGR 0,0,255) has hue 0. Shifting the hue by +180
    // degrees (OpenCV hue units: +90) rotates it all the way round to
    // cyan: the RED channel has to shrink and GREEN / BLUE have to
    // grow.
    //
    // The shift is in DEGREES, so +180 degrees is +90 OpenCV hue
    // units: the unit conversion is part of what this case checks.
    const cv::Mat src = makeRed(8, 8);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::Hsl;
    p.hueShifts = QVector<int>(8, 0);
    p.satShifts  = QVector<int>(8, 0);
    p.hueShifts[0] = 180;   // degrees -> 90 OpenCV hue units

    QVERIFY(applyAdjustmentTransform(src, p, out));

    const cv::Vec3b before = src.at<cv::Vec3b>(4, 4);
    const cv::Vec3b after  = out.at<cv::Vec3b>(4, 4);

    QCOMPARE(int(before[0]), 0);        // blue starts at 0
    QCOMPARE(int(before[1]), 0);        // green starts at 0
    QCOMPARE(int(before[2]), 255);      // red starts at 255

    QVERIFY2(after[1] > before[1],
             qPrintable(QString("green did not grow: %1 -> %2")
                            .arg(before[1]).arg(after[1])));
    QVERIFY2(after[0] > before[0],
             qPrintable(QString("blue did not grow: %1 -> %2")
                            .arg(before[0]).arg(after[0])));
    QVERIFY2(after[2] < before[2],
             qPrintable(QString("red did not shrink: %1 -> %2")
                            .arg(before[2]).arg(after[2])));
}

void tst_M2_6_AdjustTransform::hslSaturationMinus100_turnsTheImageGrey()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::Hsl;
    p.hueShifts = QVector<int>(8, 0);
    p.satShifts  = QVector<int>(8, 0);
    p.satShifts[0] = -100;   // the master band: remove all saturation

    QVERIFY(applyAdjustmentTransform(src, p, out));

    // With the saturation gone, every pixel is grey: R == G == B.
    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            const cv::Vec3b px = out.at<cv::Vec3b>(y, x);
            QCOMPARE(int(px[0]), int(px[1]));
            QCOMPARE(int(px[1]), int(px[2]));
        }
    }
}

void tst_M2_6_AdjustTransform::hslLightnessShift_brightensTheImage()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::Hsl;
    p.hueShifts = QVector<int>(8, 0);
    p.satShifts  = QVector<int>(8, 0);
    p.lightness = 40;

    QVERIFY(applyAdjustmentTransform(src, p, out));

    // Every pixel has to get brighter (or stay at the ceiling).
    long long brighter = 0;
    long long darker = 0;
    for (int y = 0; y < src.rows; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            const int b = src.at<cv::Vec3b>(y, x)[0];
            const int a = out.at<cv::Vec3b>(y, x)[0];
            if (a > b) ++brighter;
            if (a < b) ++darker;
        }
    }
    QVERIFY2(brighter > 0, "a positive lightness shift brightened nothing");
    QVERIFY2(darker == 0,
             qPrintable(QString("a positive lightness shift darkened %1 pixels")
                            .arg(darker)));
}

// =============================================================================
//  B&W
// =============================================================================

void tst_M2_6_AdjustTransform::blackWhite_neutralMixers_produceAGreyImage()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::BlackWhite;
    p.bwMixers = QVector<double>(6, 100.0);   // neutral

    QVERIFY(applyAdjustmentTransform(src, p, out));

    // Every pixel is grey: R == G == B.
    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            const cv::Vec3b px = out.at<cv::Vec3b>(y, x);
            QCOMPARE(int(px[0]), int(px[1]));
            QCOMPARE(int(px[1]), int(px[2]));
        }
    }
}

void tst_M2_6_AdjustTransform::blackWhite_mixersChangeTheGreyValue()
{
    const cv::Mat src = makeRamp(16, 12);

    AdjustmentParams neutral;
    neutral.kind = AdjustKind::BlackWhite;
    neutral.bwMixers = QVector<double>(6, 100.0);

    AdjustmentParams heavy;
    heavy.kind = AdjustKind::BlackWhite;
    // Push every mixer up: the grey must come out lighter.
    heavy.bwMixers = QVector<double>(6, 160.0);

    cv::Mat outNeutral, outHeavy;
    QVERIFY(applyAdjustmentTransform(src, neutral, outNeutral));
    QVERIFY(applyAdjustmentTransform(src, heavy, outHeavy));

    long long lighter = 0;
    for (int y = 0; y < src.rows; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            if (outHeavy.at<cv::Vec3b>(y, x)[0] > outNeutral.at<cv::Vec3b>(y, x)[0]) {
                ++lighter;
            }
        }
    }
    QVERIFY2(lighter > 0,
             "raising every B&W mixer did not brighten anything");
}

// =============================================================================
//  ChannelMixer
// =============================================================================

void tst_M2_6_AdjustTransform::channelMixer_pureRedOutputRow_zeroesGreenAndBlue()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::ChannelMixer;
    // Red output = 100% of the input red.
    p.cmMatrix[0] = 200; p.cmMatrix[1] = 0;   p.cmMatrix[2] = 0;
    // Green output = 0.
    p.cmMatrix[3] = 0;   p.cmMatrix[4] = 0;   p.cmMatrix[5] = 0;
    // Blue output = 0.
    p.cmMatrix[6] = 0;   p.cmMatrix[7] = 0;   p.cmMatrix[8] = 0;

    QVERIFY(applyAdjustmentTransform(src, p, out));

    for (int y = 0; y < src.rows; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            const cv::Vec3b before = src.at<cv::Vec3b>(y, x);
            const cv::Vec3b after  = out.at<cv::Vec3b>(y, x);
            // OpenCV BGR: channel 2 is red.
            QCOMPARE(int(after[2]), int(before[2]));
            QCOMPARE(int(after[1]), 0);
            QCOMPARE(int(after[0]), 0);
        }
    }
}

void tst_M2_6_AdjustTransform::channelMixer_monochrome_makesAllChannelsEqual()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::ChannelMixer;
    p.cmMonochrome = true;

    QVERIFY(applyAdjustmentTransform(src, p, out));

    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            const cv::Vec3b px = out.at<cv::Vec3b>(y, x);
            QCOMPARE(int(px[0]), int(px[1]));
            QCOMPARE(int(px[1]), int(px[2]));
        }
    }
}

void tst_M2_6_AdjustTransform::channelMixer_scalesTheOutput()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    AdjustmentParams p;
    p.kind = AdjustKind::ChannelMixer;
    // Every output channel = 50% of the matching input channel
    // (100 is 50% of the 200 that means 100%).
    p.cmMatrix[0] = 100; p.cmMatrix[1] = 0;   p.cmMatrix[2] = 0;
    p.cmMatrix[3] = 0;   p.cmMatrix[4] = 100; p.cmMatrix[5] = 0;
    p.cmMatrix[6] = 0;   p.cmMatrix[7] = 0;   p.cmMatrix[8] = 100;

    QVERIFY(applyAdjustmentTransform(src, p, out));

    for (int y = 0; y < src.rows; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            const cv::Vec3b before = src.at<cv::Vec3b>(y, x);
            const cv::Vec3b after  = out.at<cv::Vec3b>(y, x);
            for (int c = 0; c < 3; ++c) {
                const int expected = before[c] / 2;
                // One unit of slack for the rounding of the 0.5 factor.
                QVERIFY2(std::abs(int(after[c]) - expected) <= 1,
                         qPrintable(QString("channel %1: %2 expected ~%3")
                                        .arg(c).arg(after[c]).arg(expected)));
            }
        }
    }
}

// =============================================================================
//  per-channel LUTs
// =============================================================================

void tst_M2_6_AdjustTransform::channelLuts_liftOnlyTheRedChannel()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    // Lift red to 255, leave green and blue alone.
    QVector<cv::Mat> luts;
    luts.push_back(identityLut());        // B
    luts.push_back(identityLut());        // G
    luts.push_back(constantLut(255));     // R

    QVERIFY(applyChannelLuts(src, cv::Mat(), luts, out));

    for (int y = 0; y < src.rows; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            const cv::Vec3b before = src.at<cv::Vec3b>(y, x);
            const cv::Vec3b after  = out.at<cv::Vec3b>(y, x);
            QCOMPARE(int(after[0]), int(before[0]));   // B unchanged
            QCOMPARE(int(after[1]), int(before[1]));   // G unchanged
            QCOMPARE(int(after[2]), 255);              // R lifted
        }
    }
}

void tst_M2_6_AdjustTransform::channelLuts_aSingleLutAppliesToEveryChannel()
{
    const cv::Mat src = makeRamp(16, 12);
    cv::Mat out;

    QVERIFY(applyChannelLuts(src, constantLut(200), QVector<cv::Mat>(), out));

    for (int y = 0; y < src.rows; ++y) {
        for (int x = 0; x < src.cols; ++x) {
            const cv::Vec3b px = out.at<cv::Vec3b>(y, x);
            QCOMPARE(int(px[0]), 200);
            QCOMPARE(int(px[1]), 200);
            QCOMPARE(int(px[2]), 200);
        }
    }
}

// =============================================================================
//  the two render paths must agree
// =============================================================================

void tst_M2_6_AdjustTransform::legacyAndCompositorPaths_agreeOnATransform()
{
    // The project invariant is that the legacy render path and the
    // compositor render path produce the same picture. The adjustment
    // transform is applied in both, so both have to agree on it.
    //
    // The two paths are exercised through LayerStack:
    //   renderLegacy()             -- the default path
    //   renderViaCompositor()      -- the DAG path
    //
    // (The LayerStack API is used here rather than the node directly,
    // because the node is an internal detail of the compositor.)

    layers::LayerStack legacy;
    layers::LayerStack compos;

    const cv::Mat base = makeRamp(16, 12);

    AdjustmentParams p;
    p.kind = AdjustKind::ChannelMixer;
    p.cmMatrix[0] = 200; p.cmMatrix[1] = 0;   p.cmMatrix[2] = 0;
    p.cmMatrix[3] = 0;   p.cmMatrix[4] = 100; p.cmMatrix[5] = 0;
    p.cmMatrix[6] = 0;   p.cmMatrix[7] = 0;   p.cmMatrix[8] = 50;

    for (layers::LayerStack* stack : { &legacy, &compos }) {
        const int baseIdx = stack->addLayer(QStringLiteral("base"), base);
        QVERIFY(baseIdx >= 0);
        const int adjIdx = stack->addAdjustmentLayer(
            QStringLiteral("adj"), QStringLiteral("channelMixer"), cv::Mat(), p);
        QVERIFY(adjIdx >= 0);
    }

    legacy.setCompositorEnabled(false);
    compos.setCompositorEnabled(true);

    const cv::Mat legacyOut = legacy.render();
    const cv::Mat composOut = compos.render();

    QVERIFY(!legacyOut.empty());
    QVERIFY(!composOut.empty());
    QCOMPARE(legacyOut.size(), composOut.size());
    QCOMPARE(legacyOut.type(), composOut.type());

    // The two paths must agree pixel for pixel.
    const size_t bytes = static_cast<size_t>(legacyOut.total())
                       * static_cast<size_t>(legacyOut.elemSize());
    QCOMPARE(std::memcmp(legacyOut.data, composOut.data, bytes), 0);
}

QTEST_MAIN(tst_M2_6_AdjustTransform)
#include "tst_M2_6_AdjustTransform.moc"
