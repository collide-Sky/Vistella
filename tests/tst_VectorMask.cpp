// Vector mask path-to-mask pipeline tests.
//
// Coverage:
//   1.  Empty vector mask is a no-op on apply.
//   2.  Path interior produces full alpha (255) at the centroid.
//   3.  Path exterior produces zero alpha outside the path.
//   4.  Multiple paths combine (union) -- inside any path = 255.
//   5.  Vector mask + density scales the effective alpha.
//   6.  Vector mask + feather smooths the path edge.
//   7.  Vector mask + invert flips interior/exterior.
//   8.  Vector mask + maskRenderer::apply on a real cv::Mat integrates
//      alpha into the layer's RGB pixels.

#include <QtTest>
#include <QtMath>

#include "filters/liquify/LiquifyMesh.h"  // not used directly but include path test
#include "../src/media/imageworker/layers/LayerMask.h"
#include "../src/media/imageworker/layers/LayerMaskRenderer.h"

using namespace layers;

class TestVectorMask : public QObject {
    Q_OBJECT

private:
    static cv::Mat makeLayer(int w, int h, uchar v = 100) {
        return cv::Mat(h, w, CV_8UC3, cv::Scalar(v, v, v));
    }

private slots:
    void emptyVectorMaskIsNoop();
    void pathInteriorFullAlpha();
    void pathExteriorZeroAlpha();
    void multiplePathsUnion();
    void densityScalesAlpha();
    void featherSmoothsEdge();
    void invertFlipsMask();
    void applyIntegratesIntoLayer();
};

void TestVectorMask::emptyVectorMaskIsNoop() {
    cv::Mat layer = makeLayer(40, 40);
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.enabled = true;
    // Empty vectorPaths => isActive() is false => apply returns false and
    // the layer is left untouched.
    QVERIFY(!LayerMaskRenderer::apply(layer, m));
    QCOMPARE(int(layer.at<cv::Vec3b>(20, 20)[0]), 100);
}

void TestVectorMask::pathInteriorFullAlpha() {
    QPainterPath path;
    path.addRect(5, 5, 30, 30);  // box from (5,5) to (35,35)
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(path);
    m.enabled = true;

    // Compute alpha directly and check.
    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(40, 40));
    QCOMPARE(int(alpha.at<uchar>(20, 20)), 255);  // centroid
    QCOMPARE(int(alpha.at<uchar>(0, 0)), 0);      // outside
}

void TestVectorMask::pathExteriorZeroAlpha() {
    QPainterPath path;
    path.addEllipse(QPointF(20, 20), 15, 15);  // circle radius 15
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(path);
    m.enabled = true;
    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(40, 40));
    // (0,0) is far outside the circle.
    QCOMPARE(int(alpha.at<uchar>(0, 0)), 0);
}

void TestVectorMask::multiplePathsUnion() {
    QPainterPath p1, p2;
    p1.addRect(0, 0, 10, 10);   // top-left
    p2.addRect(30, 30, 10, 10); // bottom-right
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(p1);
    m.vectorPaths.append(p2);
    m.enabled = true;
    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(40, 40));
    QCOMPARE(int(alpha.at<uchar>(5, 5)), 255);     // inside p1
    QCOMPARE(int(alpha.at<uchar>(35, 35)), 255);   // inside p2
    QCOMPARE(int(alpha.at<uchar>(20, 20)), 0);     // outside both
}

void TestVectorMask::densityScalesAlpha() {
    QPainterPath path;
    path.addRect(5, 5, 30, 30);
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(path);
    m.enabled = true;
    m.density = 0.5;

    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(40, 40));
    // buildAlpha rounds(255 * 0.5) = 128, then 255 * 128 / 255 = 128.
    QCOMPARE(int(alpha.at<uchar>(20, 20)), 128);
}

void TestVectorMask::featherSmoothsEdge() {
    QPainterPath path;
    path.addRect(0, 0, 20, 40);  // sharp left boundary at x=0/20
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(path);
    m.enabled = true;
    m.feather = 5.0;

    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(40, 40));
    // After gaussian blur ( sigma=5 ), pixel just inside the boundary
    // should be softened: alpha at (19, 20) should be > 0 but less than 255.
    const int v = int(alpha.at<uchar>(20, 19));
    QVERIFY(v > 0);
    QVERIFY(v < 255);
}

void TestVectorMask::invertFlipsMask() {
    QPainterPath path;
    path.addRect(0, 0, 40, 40);  // covers entire mask
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(path);
    m.enabled = true;
    m.invert = true;

    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(40, 40));
    // Interior is 0 (since path covers everything -> invert flips to 0).
    QCOMPARE(int(alpha.at<uchar>(20, 20)), 0);
}

void TestVectorMask::applyIntegratesIntoLayer() {
    cv::Mat layer = makeLayer(40, 40, 200);
    QPainterPath path;
    path.addRect(0, 0, 20, 40);  // left half visible
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(path);
    m.enabled = true;

    QVERIFY(LayerMaskRenderer::apply(layer, m));
    QCOMPARE(int(layer.at<cv::Vec3b>(20, 5)[0]), 200);   // left half = 200
    QCOMPARE(int(layer.at<cv::Vec3b>(20, 35)[0]), 0);    // right half = 0
}

QTEST_MAIN(TestVectorMask)
#include "tst_VectorMask.moc"