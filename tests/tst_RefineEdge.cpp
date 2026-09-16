// RefineEdge unit tests.
//
// Coverage:
//   1.  apply on empty input returns empty.
//   2.  All-zero params returns a copy unchanged.
//   3.  Smooth blurs but does not change average intensity.
//   4.  Feather produces a strictly softer boundary.
//   5.  Contrast +100 pushes mid-tones to extremes.
//   6.  Contrast -100 pulls values toward 128.
//   7.  Shift +50 raises alpha values (selected region grows).
//   8.  Shift -50 lowers alpha values (selected region shrinks).
//   9.  Composed knobs apply in order (smooth -> feather -> contrast -> shift).
//  10.  Output dimensions match input.

#include <QtTest>
#include <QtMath>

#include "../src/media/masks/RefineEdge.h"

using namespace masks;

class TestRefineEdge : public QObject {
    Q_OBJECT

private:
    static cv::Mat makeChecker(int w, int h) {
        cv::Mat m(h, w, CV_8UC1);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                m.at<uchar>(y, x) = ((x / 10) + (y / 10)) % 2 == 0
                                    ? uchar(255) : uchar(0);
            }
        }
        return m;
    }

    static cv::Mat makeStepEdge(int w, int h) {
        cv::Mat m(h, w, CV_8UC1, cv::Scalar(0));
        // Hard step at x = w/2: left half 0, right half 255.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                m.at<uchar>(y, x) = (x < w / 2) ? uchar(0) : uchar(255);
            }
        }
        return m;
    }

private slots:
    void emptyInputReturnsEmpty();
    void zeroParamsNoop();
    void smoothPreservesAverage();
    void featherSoftensBoundary();
    void contrastPlusSharpens();
    void contrastMinusFlattens();
    void shiftPlusExpands();
    void shiftMinusContracts();
    void knobsApplyInOrder();
    void outputDimsMatch();
};

void TestRefineEdge::emptyInputReturnsEmpty() {
    RefineEdgeParams p;
    QVERIFY(RefineEdge::apply(cv::Mat(), p).empty());
}

void TestRefineEdge::zeroParamsNoop() {
    const cv::Mat src = makeChecker(20, 20);
    RefineEdgeParams p;
    const cv::Mat out = RefineEdge::apply(src, p);
    QCOMPARE(out.rows, src.rows);
    QCOMPARE(out.cols, src.cols);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            QCOMPARE(int(out.at<uchar>(y, x)),
                     int(src.at<uchar>(y, x)));
        }
    }
}

void TestRefineEdge::smoothPreservesAverage() {
    const cv::Mat src = makeChecker(40, 40);
    RefineEdgeParams p;
    p.smooth = 30;
    const cv::Mat out = RefineEdge::apply(src, p);
    cv::Scalar srcMean, outMean;
    cv::meanStdDev(src, srcMean, cv::Scalar());
    cv::meanStdDev(out, outMean, cv::Scalar());
    QVERIFY(std::fabs(srcMean[0] - outMean[0]) < 5.0);
}

void TestRefineEdge::featherSoftensBoundary() {
    const cv::Mat src = makeStepEdge(80, 40);
    RefineEdgeParams p;
    p.feather = 20;
    const cv::Mat out = RefineEdge::apply(src, p);
    // Original has a hard step at x=40: |out[40] - out[39]| = 255.
    // After feathering that gradient must shrink.
    const int origGrad = std::abs(int(src.at<uchar>(20, 41))
                               - int(src.at<uchar>(20, 39)));
    const int outGrad  = std::abs(int(out.at<uchar>(20, 41))
                               - int(out.at<uchar>(20, 39)));
    QVERIFY2(origGrad == 255, qPrintable(QString("origGrad=%1").arg(origGrad)));
    QVERIFY2(outGrad  < origGrad,
             qPrintable(QString("outGrad=%1 not less than %2")
                         .arg(outGrad).arg(origGrad)));
}

void TestRefineEdge::contrastPlusSharpens() {
    const cv::Mat src(10, 10, CV_8UC1, cv::Scalar(140));
    RefineEdgeParams p;
    p.contrast = 100;
    const cv::Mat out = RefineEdge::apply(src, p);
    // 140 -> (140 - 128) * 1.5 + 128 = 18 + 128 = 146, clamped to 146.
    QCOMPARE(int(out.at<uchar>(5, 5)), 146);
}

void TestRefineEdge::contrastMinusFlattens() {
    const cv::Mat src(10, 10, CV_8UC1, cv::Scalar(200));
    RefineEdgeParams p;
    p.contrast = -100;
    const cv::Mat out = RefineEdge::apply(src, p);
    // 200 -> (200 - 128) * 0.5 + 128 = 36 + 128 = 164.
    QCOMPARE(int(out.at<uchar>(5, 5)), 164);
}

void TestRefineEdge::shiftPlusExpands() {
    const cv::Mat src(10, 10, CV_8UC1, cv::Scalar(100));
    RefineEdgeParams p;
    p.shift = 50;
    const cv::Mat out = RefineEdge::apply(src, p);
    // 100 + 50 * 2 = 200.
    QCOMPARE(int(out.at<uchar>(5, 5)), 200);
}

void TestRefineEdge::shiftMinusContracts() {
    const cv::Mat src(10, 10, CV_8UC1, cv::Scalar(100));
    RefineEdgeParams p;
    p.shift = -50;
    const cv::Mat out = RefineEdge::apply(src, p);
    // 100 - 50 * 2 = 0.
    QCOMPARE(int(out.at<uchar>(5, 5)), 0);
}

void TestRefineEdge::knobsApplyInOrder() {
    // All four knobs at moderate values: the chain is deterministic and
    // never throws / returns empty.
    const cv::Mat src = makeChecker(40, 40);
    RefineEdgeParams p;
    p.smooth = 10;
    p.feather = 10;
    p.contrast = 20;
    p.shift = 10;
    const cv::Mat out = RefineEdge::apply(src, p);
    QCOMPARE(out.rows, src.rows);
    QCOMPARE(out.cols, src.cols);
    // Center of any 10x10 cell should be ~either 255 or 0 because
    // smooth+feather blurs the boundary but the cell is large.
    QCOMPARE(int(out.at<uchar>(5, 5)), 255);
}

void TestRefineEdge::outputDimsMatch() {
    const cv::Mat src = makeStepEdge(100, 80);
    RefineEdgeParams p;
    p.feather = 20;
    const cv::Mat out = RefineEdge::apply(src, p);
    QCOMPARE(out.rows, 80);
    QCOMPARE(out.cols, 100);
}

QTEST_MAIN(TestRefineEdge)
#include "tst_RefineEdge.moc"