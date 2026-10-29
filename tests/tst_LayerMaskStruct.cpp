// tst_LayerMaskStruct.cpp
//
// LayerMask + LayerMaskRenderer unit tests.
//
// protects: a mask changes only the alpha channel of a layer; it never
//           multiplies RGB (CP-07). Layer pixels are CV_8UC4 BGRA with
//           straight alpha (docs/CONTRACTS.md 7.6).
// path:     mask model -> buildAlpha() coverage -> applyAlpha() into a
//           layer -> alpha scaled, colour untouched.
//
// Coverage:
//  1.  Default LayerMask is inactive (kind=None).
//  2.  Pixel mask isActive only when enabled + has pixel data.
//  3.  Vector mask isActive only when enabled + has paths.
//  4.  clear() resets all fields.
//  5.  applyAlpha on an inactive mask is a no-op.
//  6.  applyAlpha rejects a 3-channel layer (no alpha to write).
//  7.  applyAlpha with a full-opacity mask keeps alpha at 255.
//  8.  applyAlpha with a half-opacity mask halves the ALPHA, not RGB.
//  9.  applyAlpha with density 0 fully hides the layer (alpha 0).
// 10.  applyAlpha with invert flips visible/hidden.
// 11.  applyAlpha with a vector mask uses the path interior.
// 12.  buildAlpha applies feather (gaussian blur reduces sharpness).
// 13.  buildAlpha handles mismatched dims by resizing the pixel mask.

#include <QtTest>
#include <QtMath>

#include "../src/media/imageworker/layers/LayerMask.h"
#include "../src/media/imageworker/layers/LayerMaskRenderer.h"

using namespace layers;

class TestLayerMaskStruct : public QObject {
    Q_OBJECT

private:
    // Opaque grey layer in the pixel contract format (4-channel BGRA).
    static cv::Mat makeSolidColor(int w, int h, uchar v = 200) {
        return cv::Mat(h, w, CV_8UC4, cv::Scalar(v, v, v, 255));
    }

    // Alpha channel of a 4-channel BGRA layer.
    static int alphaAt(const cv::Mat& layer, int y, int x) {
        return int(layer.at<cv::Vec4b>(y, x)[3]);
    }

    // Blue channel of a 4-channel BGRA layer.
    static int blueAt(const cv::Mat& layer, int y, int x) {
        return int(layer.at<cv::Vec4b>(y, x)[0]);
    }

private slots:
    void defaultInactive();
    void pixelMaskActivation();
    void vectorMaskActivation();
    void clearResetsFields();
    void applyInactiveIsNoop();
    void applyRejectsThreeChannelLayer();
    void applyFullOpacityPreservesLayer();
    void applyHalfOpacityScalesAlphaOnly();
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
    QVERIFY(!LayerMaskRenderer::applyAlpha(layer, m));
    QCOMPARE(alphaAt(layer, 0, 0), 255);
    QCOMPARE(blueAt(layer, 0, 0), 200);
}

void TestLayerMaskStruct::applyRejectsThreeChannelLayer() {
    // A 3-channel layer has no alpha channel to write. The renderer
    // must refuse rather than silently degrade into an RGB multiply
    // (CP-07).
    cv::Mat layer(20, 20, CV_8UC3, cv::Scalar(200, 200, 200));
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(0));
    m.enabled = true;
    QVERIFY(!LayerMaskRenderer::applyAlpha(layer, m));
    QCOMPARE(int(layer.at<cv::Vec3b>(0, 0)[0]), 200);
}

void TestLayerMaskStruct::applyFullOpacityPreservesLayer() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(255));
    m.enabled = true;
    QVERIFY(LayerMaskRenderer::applyAlpha(layer, m));
    QCOMPARE(alphaAt(layer, 0, 0), 255);
    QCOMPARE(alphaAt(layer, 19, 19), 255);
    QCOMPARE(blueAt(layer, 0, 0), 200);
    QCOMPARE(blueAt(layer, 19, 19), 200);
}

void TestLayerMaskStruct::applyHalfOpacityScalesAlphaOnly() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(128));
    m.enabled = true;
    QVERIFY(LayerMaskRenderer::applyAlpha(layer, m));
    // alpha = 255 * 128 / 255 = 128
    QCOMPARE(alphaAt(layer, 0, 0), 128);
    QCOMPARE(alphaAt(layer, 19, 19), 128);
    // The RGB channels are UNCHANGED -- the mask hides the pixel, it
    // does not paint it black (CP-07).
    QCOMPARE(blueAt(layer, 0, 0), 200);
    QCOMPARE(blueAt(layer, 19, 19), 200);
}

void TestLayerMaskStruct::applyDensityZeroHidesLayer() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Pixel;
    m.pixel = cv::Mat(20, 20, CV_8UC1, cv::Scalar(255));
    m.enabled = true;
    m.density = 0.0;
    QVERIFY(LayerMaskRenderer::applyAlpha(layer, m));
    // Coverage collapses to 0 -> the layer is fully hidden.
    QCOMPARE(alphaAt(layer, 0, 0), 0);
    // Colour still untouched.
    QCOMPARE(blueAt(layer, 0, 0), 200);
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
    QVERIFY(LayerMaskRenderer::applyAlpha(layer, m));
    // Left half was covered -> inverted to hidden.
    QCOMPARE(alphaAt(layer, 0, 0), 0);
    // Right half was hidden -> inverted to visible.
    QCOMPARE(alphaAt(layer, 0, 19), 255);
}

void TestLayerMaskStruct::applyVectorMaskUsesInterior() {
    cv::Mat layer = makeSolidColor(20, 20, 200);
    LayerMask m;
    m.kind = LayerMask::Vector;
    QPainterPath path;
    path.addRect(5, 5, 10, 10);
    m.vectorPaths.append(path);
    m.enabled = true;
    QVERIFY(LayerMaskRenderer::applyAlpha(layer, m));
    // Inside the path -> visible.
    QCOMPARE(alphaAt(layer, 10, 10), 255);
    // Outside the path -> hidden.
    QCOMPARE(alphaAt(layer, 0, 0), 0);
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
