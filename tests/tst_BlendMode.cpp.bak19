// =============================================================================
//  tst_BlendMode - 11 种 PS 混合模式单元测试 (阶段 1 W4.3 Phase 2, 2026-09-04)
//
//  每个模式测试:
//    1. 简单像素对: 验证公式正确性
//    2. 边界: opacity=0 / 1, 黑白极端
//    3. LayerStack::render() 集成: 多 layer 混合
// =============================================================================

#include <QTest>

#include <opencv2/core.hpp>
#include <cmath>

#include "../src/core/Result.h"
#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/BlendMode.h"

using namespace layers;

class tst_BlendMode : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- 11 种基础 blend 函数 ----
    void test_normal();
    void test_multiply();
    void test_screen();
    void test_overlay();
    void test_softLight();
    void test_hardLight();
    void test_colorDodge();
    void test_colorBurn();
    void test_darken();
    void test_lighten();
    void test_difference();
    void test_exclusion();

    // ---- 调度器 ----
    void test_applyBlend_dispatcher();
    void test_applyBlend_unsupportedMode();

    // ---- LayerStack 集成 ----
    void test_layerStack_renderWithBlend();

private:
    cv::Mat makeMat(int w = 4, int h = 4, cv::Scalar bgr = cv::Scalar(100, 150, 200))
    {
        return cv::Mat(h, w, CV_8UC3, bgr);
    }
    // 单像素 1x1 测试 (BGR 0,0,0 = 黑, 0,0,255 = 蓝, 0,255,0 = 绿, 0,255,255 = 青, 255,0,0 = 红, 255,0,255 = 紫, 255,255,0 = 黄, 255,255,255 = 白)
    cv::Mat singlePixel(int b, int g, int r)
    {
        return cv::Mat(1, 1, CV_8UC3, cv::Scalar(b, g, r));
    }
};

void tst_BlendMode::initTestCase() {}
void tst_BlendMode::cleanupTestCase() {}

// =====================================================================
//  Normal: alpha blend
// =====================================================================

void tst_BlendMode::test_normal()
{
    cv::Mat a = singlePixel(0, 0, 0);
    cv::Mat b = singlePixel(0, 0, 255);

    // opacity=0.5: 0.5 * 黑 + 0.5 * 红 (cv::addWeighted round 后 = 128)
    cv::Mat out = blendNormal(a, b, 0.5f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[0]), 0);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[1]), 0);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 128);

    // opacity=0: 返 base (黑)
    out = blendNormal(a, b, 0.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 0);

    // opacity=1: 返 blend (红)
    out = blendNormal(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 255);
}

// =====================================================================
//  Multiply: A * B / 255
// =====================================================================

void tst_BlendMode::test_multiply()
{
    // 白 * 红 = 红 (不变)
    cv::Mat white = singlePixel(255, 255, 255);
    cv::Mat red   = singlePixel(0, 0, 255);
    cv::Mat out = blendMultiply(white, red, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 255);

    // 黑 * 红 = 黑
    cv::Mat black = singlePixel(0, 0, 0);
    out = blendMultiply(black, red, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 0);

    // 128 * 200 = 100 (128 * 200 / 255 = 100.39, sat 100)
    cv::Mat a = singlePixel(0, 0, 128);
    cv::Mat b = singlePixel(0, 0, 200);
    out = blendMultiply(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);
}

// =====================================================================
//  Screen: 255 - (255-A) * (255-B) / 255
// =====================================================================

void tst_BlendMode::test_screen()
{
    // 白 Screen X = 白
    cv::Mat white = singlePixel(255, 255, 255);
    cv::Mat red   = singlePixel(0, 0, 255);
    cv::Mat out = blendScreen(white, red, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 255);

    // 黑 Screen X = X
    cv::Mat black = singlePixel(0, 0, 0);
    out = blendScreen(black, red, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 255);

    // 128 Screen 128 = 191 (255 - 127*127/255 = 255 - 63.24 = 191.76)
    cv::Mat a = singlePixel(0, 0, 128);
    cv::Mat b = singlePixel(0, 0, 128);
    out = blendScreen(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 191);
}

// =====================================================================
//  Overlay: base 亮度决定 Multiply 还是 Screen
// =====================================================================

void tst_BlendMode::test_overlay()
{
    // base=128 边界 (Multiply 分支, 2*128*X/255)
    cv::Mat a = singlePixel(0, 0, 128);
    cv::Mat b = singlePixel(0, 0, 200);
    cv::Mat out = blendOverlay(a, b, 1.0f);
    // 2*128*200/255 = 200.78 → 200
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);

    // base > 128 (Screen 分支): 200, 100 → 255 - 2*55*155/255 = 255 - 66.86 = 188
    a = singlePixel(0, 0, 200);
    b = singlePixel(0, 0, 100);
    out = blendOverlay(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 188);

    // base < 128 (Multiply 分支): 50, 200 → 2*50*200/255 = 78
    a = singlePixel(0, 0, 50);
    b = singlePixel(0, 0, 200);
    out = blendOverlay(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 78);
}

// =====================================================================
//  SoftLight
// =====================================================================

void tst_BlendMode::test_softLight()
{
    // blend < 128: result = base - (1 - 2*blend/255) * base * (1 - base/255)
    //   base=200, blend=64: 200 - (1 - 128/255) * 200 * 55/255 = 200 - 0.498 * 200 * 0.216 = 200 - 21.5 = 178
    cv::Mat a = singlePixel(0, 0, 200);
    cv::Mat b = singlePixel(0, 0, 64);
    cv::Mat out = blendSoftLight(a, b, 1.0f);
    const int v = int(out.at<cv::Vec3b>(0, 0)[2]);
    QVERIFY(v >= 175 && v <= 182);

    // blend >= 128: Pegtop 公式
    //   base=200, blend=200: 200 + (2*200/255 - 1) * (sqrt(200/255)*255 - 200)
    //                       = 200 + 0.569 * (225.8 - 200) = 200 + 14.7 = 215
    a = singlePixel(0, 0, 200);
    b = singlePixel(0, 0, 200);
    out = blendSoftLight(a, b, 1.0f);
    const int v2 = int(out.at<cv::Vec3b>(0, 0)[2]);
    QVERIFY(v2 >= 213 && v2 <= 217);
}

// =====================================================================
//  HardLight (Overlay 对称)
// =====================================================================

void tst_BlendMode::test_hardLight()
{
    // blend < 128: Multiply 分支
    //   base=200, blend=64: 2*200*64/255 = 100
    cv::Mat a = singlePixel(0, 0, 200);
    cv::Mat b = singlePixel(0, 0, 64);
    cv::Mat out = blendHardLight(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);

    // blend >= 128: Screen 分支
    //   base=100, blend=200: 255 - 2*155*55/255 = 255 - 66.86 = 188
    a = singlePixel(0, 0, 100);
    b = singlePixel(0, 0, 200);
    out = blendHardLight(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 188);
}

// =====================================================================
//  ColorDodge
// =====================================================================

void tst_BlendMode::test_colorDodge()
{
    // blend=255: 返 255
    cv::Mat a = singlePixel(0, 0, 100);
    cv::Mat b = singlePixel(0, 0, 255);
    cv::Mat out = blendColorDodge(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 255);

    // blend=0: 返 base
    b = singlePixel(0, 0, 0);
    out = blendColorDodge(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);

    // blend=128, base=100: 100 * 255 / 127 = 200
    b = singlePixel(0, 0, 128);
    out = blendColorDodge(a, b, 1.0f);
    // 100*255/(255-128) = 100*255/127 = 200.79 → 200
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);
}

// =====================================================================
//  ColorBurn
// =====================================================================

void tst_BlendMode::test_colorBurn()
{
    // blend=0: 返 0
    cv::Mat a = singlePixel(0, 0, 200);
    cv::Mat b = singlePixel(0, 0, 0);
    cv::Mat out = blendColorBurn(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 0);

    // blend=255: 返 base
    b = singlePixel(0, 0, 255);
    out = blendColorBurn(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);

    // blend=128, base=200: 255 - (255-200)*255/128 = 255 - 109.57 = 145
    b = singlePixel(0, 0, 128);
    out = blendColorBurn(a, b, 1.0f);
    // 255 - 55*255/128 = 255 - 109.57 = 145
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 145);
}

// =====================================================================
//  Darken / Lighten
// =====================================================================

void tst_BlendMode::test_darken()
{
    cv::Mat a = singlePixel(0, 0, 100);
    cv::Mat b = singlePixel(0, 0, 200);
    cv::Mat out = blendDarken(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);

    a = singlePixel(0, 0, 200);
    b = singlePixel(0, 0, 100);
    out = blendDarken(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);
}

void tst_BlendMode::test_lighten()
{
    cv::Mat a = singlePixel(0, 0, 100);
    cv::Mat b = singlePixel(0, 0, 200);
    cv::Mat out = blendLighten(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);

    a = singlePixel(0, 0, 200);
    b = singlePixel(0, 0, 100);
    out = blendLighten(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);
}

// =====================================================================
//  Difference / Exclusion
// =====================================================================

void tst_BlendMode::test_difference()
{
    cv::Mat a = singlePixel(0, 0, 200);
    cv::Mat b = singlePixel(0, 0, 100);
    cv::Mat out = blendDifference(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);

    a = singlePixel(0, 0, 100);
    b = singlePixel(0, 0, 200);
    out = blendDifference(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);

    // 相同 = 0
    a = singlePixel(0, 0, 150);
    b = singlePixel(0, 0, 150);
    out = blendDifference(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 0);
}

void tst_BlendMode::test_exclusion()
{
    // 200 + 100 - 2*200*100/255 = 300 - 156.86 = 143
    cv::Mat a = singlePixel(0, 0, 200);
    cv::Mat b = singlePixel(0, 0, 100);
    cv::Mat out = blendExclusion(a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 143);

    // 相同 = 0 (X + X - 2*X*X/255, 0.5 倍)
    a = singlePixel(0, 0, 128);
    b = singlePixel(0, 0, 128);
    out = blendExclusion(a, b, 1.0f);
    // 128 + 128 - 2*128*128/255 = 256 - 128.5 = 127
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 127);
}

// =====================================================================
//  调度器 / 集成
// =====================================================================

void tst_BlendMode::test_applyBlend_dispatcher()
{
    cv::Mat a = singlePixel(0, 0, 100);
    cv::Mat b = singlePixel(0, 0, 200);

    // Multiply
    cv::Mat out = applyBlend(Layer::Multiply, a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 78);   // 100*200/255 = 78

    // Screen
    out = applyBlend(Layer::Screen, a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 221);   // 255 - 155*55/255 = 255 - 33.43 = 221

    // Darken
    out = applyBlend(Layer::Darken, a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 100);

    // Lighten
    out = applyBlend(Layer::Lighten, a, b, 1.0f);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);
}

void tst_BlendMode::test_applyBlend_unsupportedMode()
{
    // Phase 3: HSV 4 种 — Phase 2 fallback 到 Normal
    cv::Mat a = singlePixel(0, 0, 100);
    cv::Mat b = singlePixel(0, 0, 200);

    cv::Mat out = applyBlend(Layer::Hue, a, b, 0.5f);
    // Fallback 到 Normal: 0.5 * 100 + 0.5 * 200 = 150
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 150);
}

void tst_BlendMode::test_layerStack_renderWithBlend()
{
    LayerStack stack;
    // base 红 (BGR 0,0,200)
    stack.addLayer(QStringLiteral("Base"), makeMat(4, 4, cv::Scalar(0, 0, 200)));
    // top 蓝 (BGR 200,0,0), opacity 0.5, Multiply
    stack.addLayer(QStringLiteral("Top"), makeMat(4, 4, cv::Scalar(200, 0, 0)));
    stack.setOpacity(1, 0.5f);
    stack.setBlend(1, Layer::Multiply);

    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    const cv::Vec3b pix = out.at<cv::Vec3b>(2, 2);
    // base canvas (BGR 0,0,200), top blend (BGR 200,0,0), Multiply opacity 0.5
    //   pixel multiply: (0*200/255, 0*0/255, 200*0/255) = (0, 0, 0)
    //   alpha 0.5: 0.5*(0,0,200) + 0.5*(0,0,0) = (0, 0, 100)
    QCOMPARE(int(pix[0]), 0);     // B
    QCOMPARE(int(pix[1]), 0);
    QCOMPARE(int(pix[2]), 100);   // R
}

QTEST_MAIN(tst_BlendMode)
#include "tst_BlendMode.moc"
