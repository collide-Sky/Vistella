// LayerMask + LayerMaskRenderer unit tests.
//
// Coverage:
//  1.  Default LayerMask is inactive (kind=None).
//  2.  Pixel mask isActive only when enabled + has pixel data.
//  3.  Vector mask isActive only when enabled + has paths.
//  4.  clear() resets all fields.
//  5.  LayerMaskRenderer::apply on inactive mask is a no-op.
//  6.  apply with full-opacity pixel mask leaves the layer unchanged.
//  7.  apply with half-opacity pixel mask halves pixel values.
//  8.  apply with density 0 fully hides the layer.
//  9.  apply with invert flips visible/hidden.
// 10.  apply with vector mask uses the path interior.
// 11.  buildAlpha applies feather (gaussian blur reduces sharpness).
// 12.  buildAlpha handles mismatched dims by resizing the pixel mask.

#include <QtTest>
#include <QtMath>

#include "../src/media/imageworker/layers/LayerMask.h"
#include "../src/media/imageworker/layers/LayerMaskRenderer.h"

using namespace layers;

class TestLayerMaskStruct : public QObject {
    Q_OBJECT

private:
    static cv::Mat makeSolidColor(int w, int h, uchar v = 200) {
        cv::Mat img(h, w, CV_8UC3, cv::Scalar(v, v, v, 0));
        return img;
    }

private slots:
    void defaultInactive();
    void pixelMaskActivation();
    void vectorMaskActivation();
    void clearResetsFields();
    void applyInactiveIsNoop();
    void applyFullOpacityPreservesLayer();
    void applyHalfOpacityHalvesPixels();
    void applyDensityZeroHidesLayer();
    void applyInvertFlipsVisibility();
    void applyVectorMaskUsesInterior();
    void buildAlphaFeatherSoftens();
    void buildAlphaResizesMismatchedMask();
};

void TestLayerMaskStruct::defaultInactive() {
    LayerMask m;
    QCOMPARE(m.kind, LayerMask::None);
    QVERIFY(!m.enabled);
    QVERIFY(!m.isActive());
    QVERIFY(!m.hasData());
}

void TestLayerMaskStruct::pixelMaskActivation() {
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(10, 10, CV_8UC1, cv::Scalar(255));
    QVERIFY(m.hasData());
    QVERIFY(!m.isActive());
    m.enabled = true;
    QVERIFY(m.isActive());
}

void TestLayerMaskStruct::vectorMaskActivation() {
    LayerMask m;
    m.kind = LayerMask::Vector;
    m.vectorPaths.append(QPainterPath());
    m.vectorPaths.last().addRect(0, 0, 50, 50);
    QVERIFY(m.hasData());
    QVERIFY(!m.isActive());
    m.enabled = true;
    QVERIFY(m.isActive());
}

void TestLayerMaskStruct::clearResetsFields() {
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(10, 10, CV_8UC1, cv::Scalar(128));
    m.enabled = true;
    m.density = 0.5;
    m.feather = 3.0;
    m.invert = true;
    m.clear();
    QCOMPARE(m.kind, LayerMask::None);
    QVERIFY(!m.enabled);
    QVERIFY(m.pixel.empty());
    QCOMPARE(m.density, 1.0);
    QCOMPARE(m.feather, 0.0);
    QVERIFY(!m.invert);
}

void TestLayerMaskStruct::applyInactiveIsNoop() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    QVERIFY(!LayerMaskRenderer::apply(layer, m));
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 200);
}

void TestLayerMaskStruct::applyFullOpacityPreservesLayer() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(255));
    m.enabled = true;
    QVERIFY(LayerMaskRenderer::apply(layer, m));
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 200);
    QCOMPARE(int(layer.at<cv::Vec3b>(19, 19)[2]), 200);
}

void TestLayerMaskStruct::applyHalfOpacityHalvesPixels() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(128));
    m.enabled = true;
    QVERIFY(LayerMaskRenderer::apply(layer, m));
    // 200 * 128 / 255 = 100 (rounded down)
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 100);
    QCOMPARE(int(layer.at<cv::Vec3b>(19, 19)[2]), 100);
}

void TestLayerMaskStruct::applyDensityZeroHidesLayer() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(255));
    m.enabled = true;
    m.density = 0.0;
    QVERIFY(LayerMaskRenderer::apply(layer, m));
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 0);
}

void TestLayerMaskStruct::applyInvertFlipsVisibility() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 10; ++x) {
            m.pixel.at<uchar>(y, x) = 255;
        }
    }
    m.enabled = true;
    m.invert = true;
    LayerMaskRenderer::apply(layer, m);
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 0);
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 19)[0]), 200);
}

void TestLayerMaskStruct::applyVectorMaskUsesInterior() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Vector;
    QPainterPath path;
    path.addRect(5, 5, 10, 10);
    m.vectorPaths.append(path);
    m.enabled = true;
    LayerMaskRenderer::apply(layer, m);
    QCOMPARE(int(layer.at<cv::Vec3b>(10, 10)[0]), 200);
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 0);
}

void TestLayerMaskStruct::buildAlphaFeatherSoftens() {
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 10; ++x) {
            m.pixel.at<uchar>(y, x) = 255;
        }
    }
    m.enabled = true;
    m.feather = 3.0;

    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(20, 20));
    QVERIFY(!alpha.empty());
    const uchar v = alpha.at<uchar>(10, 12);
    QVERIFY2(v > 0, qPrintable(QString("feather alpha at (10,12)=%1").arg(int(v))));
}

void TestLayerMaskStruct::buildAlphaResizesMismatchedMask() {
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(40, 40, CV_8UC1, cv::Scalar(255));
    m.enabled = true;
    const cv::Mat alpha = LayerMaskRenderer::buildAlpha(m, QSize(20, 20));
    QCOMPARE(alpha.rows, 20);
    QCOMPARE(alpha.cols, 20);
    QCOMPARE(int(alpha.at<uchar>(0, 0)), 255);
}

QTEST_MAIN(TestLayerMaskStruct)
#include "tst_LayerMaskStruct.moc"
