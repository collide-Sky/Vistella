// ColorRange unit tests.
//
// Coverage:
//   1.  computeMask on a null source returns empty.
//   2.  computeMask with empty samples returns empty.
//   3.  Sample pixel at distance 0 gets full alpha (255).
//   4.  Far-away pixels get low alpha when fuzziness is small.
//   5.  Increasing fuzziness extends the selected region.
//   6.  Invert flips high alpha to low alpha.
//   7.  Sample outside the image bounds is ignored.
//   8.  Result mask matches source dimensions.

#include <QtTest>
#include <QtMath>

#include "../src/media/masks/ColorRange.h"
#include "../src/media/selection/SelectionStrategy.h"

using namespace masks;
using namespace selection;

class TestColorRange : public QObject {
    Q_OBJECT

private:
    // Build a 100x100 image with a solid blue region in the center.
    static QImage makeBlueSquare() {
        QImage img(100, 100, QImage::Format_ARGB32);
        img.fill(QColor(220, 220, 220, 255));   // gray bg
        for (int y = 25; y < 75; ++y) {
            for (int x = 25; x < 75; ++x) {
                img.setPixelColor(x, y, QColor(40, 60, 220, 255));  // blue
            }
        }
        return img;
    }

private slots:
    void emptySourceReturnsEmpty();
    void emptySamplesReturnsEmpty();
    void samplePixelFullAlpha();
    void farPixelsLowAlpha();
    void largerFuzzinessExtendsRegion();
    void invertFlipsAlpha();
    void outOfBoundsSampleIgnored();
    void resultMaskDimensions();

    // P2.4 (2026-09-22): SelectionStrategy wrapper
    void strategy_setterDefaults();
    void strategy_emptySamples_returnsEmptyMask();
    void strategy_singleSample_selectsMatchingPixels();
};

void TestColorRange::emptySourceReturnsEmpty() {
    ColorRangeParams p;
    p.samplePoints.append(QPoint(0, 0));
    QVERIFY(ColorRange::computeMask(QImage(), p).empty());
}

void TestColorRange::emptySamplesReturnsEmpty() {
    const QImage img = makeBlueSquare();
    ColorRangeParams p;
    p.samplePoints.clear();
    QVERIFY(ColorRange::computeMask(img, p).empty());
}

void TestColorRange::samplePixelFullAlpha() {
    const QImage img = makeBlueSquare();
    ColorRangeParams p;
    p.samplePoints.append(QPoint(50, 50));     // center of blue region
    p.fuzziness = 50;
    const cv::Mat m = ColorRange::computeMask(img, p);
    QCOMPARE(int(m.at<uchar>(50, 50)), 255);
}

void TestColorRange::farPixelsLowAlpha() {
    const QImage img = makeBlueSquare();
    ColorRangeParams p;
    p.samplePoints.append(QPoint(50, 50));     // blue
    p.fuzziness = 30;
    const cv::Mat m = ColorRange::computeMask(img, p);
    // Pixel (0, 0) is far from the sample (gray vs blue) -> low alpha.
    QVERIFY(int(m.at<uchar>(0, 0)) < 50);
}

void TestColorRange::largerFuzzinessExtendsRegion() {
    const QImage img = makeBlueSquare();
    // Pick a pixel just outside the blue square (e.g. (20, 50)) where
    // the HSV distance to the blue sample is moderate.
    ColorRangeParams p;
    p.samplePoints.append(QPoint(50, 50));
    p.fuzziness = 10;     // small
    const cv::Mat small = ColorRange::computeMask(img, p);
    p.fuzziness = 200;    // large
    const cv::Mat large = ColorRange::computeMask(img, p);
    QVERIFY(int(large.at<uchar>(50, 50)) >= int(small.at<uchar>(50, 50)));
    // The off-square pixel sees more alpha with larger fuzziness.
    QVERIFY(int(large.at<uchar>(20, 50)) >= int(small.at<uchar>(20, 50)));
}

void TestColorRange::invertFlipsAlpha() {
    const QImage img = makeBlueSquare();
    ColorRangeParams p;
    p.samplePoints.append(QPoint(50, 50));
    p.fuzziness = 80;
    p.invert = false;
    const cv::Mat normal = ColorRange::computeMask(img, p);
    p.invert = true;
    const cv::Mat flipped = ColorRange::computeMask(img, p);
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const int n = int(normal.at<uchar>(y, x));
            const int f = int(flipped.at<uchar>(y, x));
            QVERIFY2(std::abs(n + f - 255) <= 1,
                     qPrintable(QString("y=%1 x=%2 n=%3 f=%4")
                                 .arg(y).arg(x).arg(n).arg(f)));
        }
    }
}

void TestColorRange::outOfBoundsSampleIgnored() {
    const QImage img = makeBlueSquare();
    ColorRangeParams p;
    p.samplePoints.append(QPoint(1000, 1000));  // off-image
    p.fuzziness = 30;
    QVERIFY(ColorRange::computeMask(img, p).empty());
}

void TestColorRange::resultMaskDimensions() {
    const QImage img = makeBlueSquare();
    ColorRangeParams p;
    p.samplePoints.append(QPoint(50, 50));
    const cv::Mat m = ColorRange::computeMask(img, p);
    QCOMPARE(m.rows, 100);
    QCOMPARE(m.cols, 100);
    QCOMPARE(m.type(), CV_8UC1);
}

// ============================================================================
// P2.4 (2026-09-22): selection::ColorRangeSelectionStrategy wrapper tests
// ============================================================================
//
// Verify SelectionStrategy interface integration with masks::ColorRange.
//   - setter defaults
//   - empty samples -> empty mask
//   - single sample -> mask matches sample color region (QImage Format_Alpha8)
//

void TestColorRange::strategy_setterDefaults()
{
    selection::ColorRangeSelectionStrategy s;
    QCOMPARE(s.fuzziness(), 30);             // matches ColorRangeParams default
    QVERIFY(!s.invert());
    QCOMPARE(s.samplePoints().size(), 0);
    QCOMPARE(static_cast<int>(s.kind()),
             static_cast<int>(SelectionStrategy::Kind::ColorRange));
}

void TestColorRange::strategy_emptySamples_returnsEmptyMask()
{
    ColorRangeSelectionStrategy s;
    s.setSamplePoints({});                    // explicit empty
    const QImage img = makeBlueSquare();
    const QImage mask = s.end(img);
    QVERIFY(mask.isNull());
}

void TestColorRange::strategy_singleSample_selectsMatchingPixels()
{
    ColorRangeSelectionStrategy s;
    s.setFuzziness(50);                        // broad enough to capture blue square
    s.begin(QPointF(50, 50));                  // sample inside blue square
    const QImage img = makeBlueSquare();
    const QImage mask = s.end(img);
    QCOMPARE(mask.size(), img.size());
    QCOMPARE(mask.format(), QImage::Format_Alpha8);

    // Verify: center pixel (50, 50) is in mask (>= 1 = blue match)
    const uchar* bits = mask.constBits();
    const int stride = mask.bytesPerLine();
    QVERIFY(bits[50 * stride + 50] > 0);

    // Verify: gray corner (5, 5) is NOT in mask (gray far from blue)
    QCOMPARE(bits[5 * stride + 5], uchar(0));
}

QTEST_MAIN(TestColorRange)
#include "tst_ColorRange.moc"