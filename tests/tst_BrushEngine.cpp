// SPDX-License-Identifier: MIT
//
// tst_BrushEngine - P1.1 (2026-09-15) Brush full implementation
//   Unit tests for BrushEngine (stamp generation, apply, scatter, texture, dual)
//   and PressureCurve + StrokeSmoother.

#include <QTest>

#include "../src/media/brushes/BrushEngine.h"
#include "../src/media/brushes/BrushPreset.h"
#include "../src/media/brushes/PressureCurve.h"
#include "../src/media/brushes/StrokeSmoother.h"

#include <opencv2/core.hpp>

using namespace brushes;

class tst_BrushEngine : public QObject
{
    Q_OBJECT

private slots:
    void stamp_round_size();
    void stamp_hardness_edge();
    void stamp_pressure_affects_size();
    void stamp_roundness_flattens();
    void stamp_angle_rotates();
    void apply_paint_blends_color();
    void apply_paint_grayscale();
    void apply_erase_subtracts();
    void pressure_curve_identity();
    void pressure_curve_minmax();
    void pressure_curve_min_greater_than_max();
    void smoother_disabled_passthrough();
    void smoother_centroid();
    void smoother_smoothing_disabled();
    void thumbnail_generated();
};

void tst_BrushEngine::stamp_round_size()
{
    BrushSettings s; s.size = 32; s.hardness = 80; s.roundness = 100;
    BrushShape sh;   sh.type = ShapeType::Round;
    auto r = BrushEngine::generateStamp(s, sh);
    QVERIFY(!r.stamp.empty());
    QCOMPARE(r.stamp.type(), CV_8UC1);
    QCOMPARE(r.stamp.rows, 32);
    QCOMPARE(r.stamp.cols, 32);
}

void tst_BrushEngine::stamp_hardness_edge()
{
    // Hardness 100 -> center should be near max (>= 200)
    // Hardness 0 -> center should still be max (full intensity, soft edge)
    BrushSettings sH; sH.size = 32; sH.hardness = 100; sH.roundness = 100;
    BrushSettings sS; sS.size = 32; sS.hardness = 0;   sS.roundness = 100;
    BrushShape sh; sh.type = ShapeType::Round;
    auto rH = BrushEngine::generateStamp(sH, sh);
    auto rS = BrushEngine::generateStamp(sS, sh);
    QVERIFY(rH.stamp.at<uchar>(16, 16) >= 200);
    QVERIFY(rS.stamp.at<uchar>(16, 16) >= 200);
    // At edge (radius-1), soft should be >= hard (soft brush extends further)
    const uchar hardEdge = rH.stamp.at<uchar>(2, 16);
    const uchar softEdge = rS.stamp.at<uchar>(2, 16);
    QVERIFY(softEdge >= hardEdge);
}

void tst_BrushEngine::stamp_pressure_affects_size()
{
    BrushSettings s; s.size = 100; s.hardness = 80; s.roundness = 100;
    BrushShape sh; sh.type = ShapeType::Round;
    auto full = BrushEngine::generateStamp(s, sh, 1.0);
    auto half = BrushEngine::generateStamp(s, sh, 0.5);
    QCOMPARE(full.stamp.rows, 100);
    QCOMPARE(half.stamp.rows, 50);   // 0.5 * 100 = 50
}

void tst_BrushEngine::stamp_roundness_flattens()
{
    // Roundness 100 vs 10: high should have larger coverage than flat
    BrushSettings sR; sR.size = 32; sR.hardness = 80; sR.roundness = 100;
    BrushSettings sF; sF.size = 32; sF.hardness = 80; sF.roundness = 10;
    BrushShape sh; sh.type = ShapeType::Flat;
    auto rR = BrushEngine::generateStamp(sR, sh);
    auto rF = BrushEngine::generateStamp(sF, sh);
    // Count non-zero pixels
    int cntR = 0, cntF = 0;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        if (rR.stamp.at<uchar>(y, x) > 0) cntR++;
        if (rF.stamp.at<uchar>(y, x) > 0) cntF++;
    }
    QVERIFY(cntR > cntF);   // Roundness 100 covers more area
}

void tst_BrushEngine::stamp_angle_rotates()
{
    BrushSettings s0;  s0.size = 32; s0.hardness = 80; s0.angle = 0;   s0.roundness = 50;
    BrushSettings s90; s90.size = 32; s90.hardness = 80; s90.angle = 90; s90.roundness = 50;
    BrushShape sh; sh.type = ShapeType::Flat;
    auto r0  = BrushEngine::generateStamp(s0, sh);
    auto r90 = BrushEngine::generateStamp(s90, sh);
    // Stamps should differ (rotation produces different layouts)
    int diffs = 0;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        if (r0.stamp.at<uchar>(y, x) != r90.stamp.at<uchar>(y, x)) diffs++;
    }
    QVERIFY(diffs > 50);   // rotated stamp differs significantly
}

void tst_BrushEngine::apply_paint_blends_color()
{
    BrushSettings s; s.size = 16; s.hardness = 100; s.opacity = 100; s.flow = 100;
    BrushShape sh; sh.type = ShapeType::Round;
    auto stamp = BrushEngine::generateStamp(s, sh, 1.0);

    cv::Mat target(40, 40, CV_8UC3, cv::Scalar(0, 0, 0));   // black
    QColor red(255, 0, 0, 255);
    BrushEngine::applyStamp(target, cv::Point(20, 20), stamp, red, 1.0, 1.0);

    // Center should now be ~red
    const cv::Vec3b c = target.at<cv::Vec3b>(20, 20);
    QVERIFY(c[2] >= 200);   // R channel
    QVERIFY(c[0] <= 50);    // B channel stays low
}

void tst_BrushEngine::apply_paint_grayscale()
{
    BrushSettings s; s.size = 16; s.hardness = 100; s.opacity = 100; s.flow = 100;
    BrushShape sh; sh.type = ShapeType::Round;
    auto stamp = BrushEngine::generateStamp(s, sh, 1.0);

    cv::Mat target(40, 40, CV_8UC1, cv::Scalar(0));
    QColor white(255, 255, 255, 255);
    BrushEngine::applyStamp(target, cv::Point(20, 20), stamp, white, 1.0, 1.0);
    QVERIFY(target.at<uchar>(20, 20) > 200);
}

void tst_BrushEngine::apply_erase_subtracts()
{
    BrushSettings s; s.size = 16; s.hardness = 100; s.opacity = 100; s.flow = 100;
    BrushShape sh; sh.type = ShapeType::Round;
    auto stamp = BrushEngine::generateStamp(s, sh, 1.0);

    cv::Mat target(40, 40, CV_8UC1, cv::Scalar(255));
    QColor white(255, 255, 255, 255);
    BrushEngine::applyStamp(target, cv::Point(20, 20), stamp, white, 1.0, 1.0, /*mode*/1);
    QVERIFY(target.at<uchar>(20, 20) < 255);   // erased
}

void tst_BrushEngine::pressure_curve_identity()
{
    PressureCurve pc;
    QVERIFY(pc.isLinear());
    QCOMPARE(pc.evaluate(0.0), 0.0);
    QCOMPARE(pc.evaluate(0.5), 0.5);
    QCOMPARE(pc.evaluate(1.0), 1.0);
}

void tst_BrushEngine::pressure_curve_minmax()
{
    PressureCurve pc;
    pc.setMinimum(20);
    pc.setMaximum(80);
    QVERIFY(!pc.isLinear());
    QCOMPARE(pc.evaluate(0.10), 0.0);   // below min -> 0
    QCOMPARE(pc.evaluate(0.20), 0.0);   // at min -> 0
    QVERIFY(qAbs(pc.evaluate(0.50) - 0.50) < 0.01);  // mid maps to mid
    QCOMPARE(pc.evaluate(0.80), 1.0);
    QCOMPARE(pc.evaluate(1.00), 1.0);
}

void tst_BrushEngine::pressure_curve_min_greater_than_max()
{
    PressureCurve pc;
    pc.setMinimum(80);
    pc.setMaximum(20);   // inverted
    // Degenerate: should not crash, returns min
    QCOMPARE(pc.evaluate(0.5), 0.8);
}

void tst_BrushEngine::smoother_disabled_passthrough()
{
    StrokeSmoother sm;
    sm.setEnabled(false);
    sm.addPoint(QPointF(10, 10), 1.0);
    sm.addPoint(QPointF(20, 20), 1.0);
    QCOMPARE(sm.smoothed(), QPointF(20, 20));   // last point wins when disabled
}

void tst_BrushEngine::smoother_centroid()
{
    StrokeSmoother sm;
    sm.setEnabled(true);
    sm.setAmount(100);
    sm.setRadius(10);
    sm.addPoint(QPointF(0, 0), 1.0);
    sm.addPoint(QPointF(10, 0), 1.0);
    sm.addPoint(QPointF(20, 0), 1.0);
    const QPointF p = sm.smoothed();
    QVERIFY(qAbs(p.x() - 10.0) < 0.01);   // centroid of (0,10,20) = 10
    QCOMPARE(p.y(), 0.0);
}

void tst_BrushEngine::smoother_smoothing_disabled()
{
    StrokeSmoother sm;
    sm.setEnabled(true);
    sm.setAmount(0);     // disabled via amount
    sm.addPoint(QPointF(5, 5), 1.0);
    sm.addPoint(QPointF(15, 15), 1.0);
    QCOMPARE(sm.smoothed(), QPointF(15, 15));   // passthrough
}

void tst_BrushEngine::thumbnail_generated()
{
    BrushPreset p = BrushPreset::makeBuiltInHardRound();
    QImage img = BrushEngine::generateThumbnail(p);
    QCOMPARE(img.width(), 64);
    QCOMPARE(img.height(), 64);
    QVERIFY(!img.isNull());
}

QTEST_MAIN(tst_BrushEngine)
#include "tst_BrushEngine.moc"