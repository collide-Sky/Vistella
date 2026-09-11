// =============================================================================
//  tst_ColorAdjust - 色彩调整完整流程测试 (阶段 1 W3, 2026-09-04)
//
//  覆盖:
//    - 饱和度: ImageProcessor::saturation + ImageProcessorAsync::saturation
//    - 色调: ImageProcessor::hueShift + ImageProcessorAsync::hueShift
//    - 曝光: ImageProcessor::exposure + ImageProcessorAsync::exposure
//    - 组合: sat + hue + exposure 顺序应用
//    - 边界: scale=0/1.0, hue=0, ev=0/-3/+3
//    - 异步 Result 错误传递
// =============================================================================

#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QFutureWatcher>

#include <opencv2/core.hpp>
#include <cmath>

#include "../src/core/Result.h"
#include "../src/media/imageprocessor.h"
#include "../src/media/imageworker/ImageProcessorAsync.h"

class tst_ColorAdjust : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- 同步 (ImageProcessor) ----
    void test_saturation_sync();
    void test_saturation_noChange();
    void test_hueShift_sync();
    void test_hueShift_noChange();
    void test_exposure_sync();
    void test_exposure_brighten();
    void test_exposure_darken();

    // ---- 异步 (ImageProcessorAsync) ----
    void test_saturation_async();
    void test_hueShift_async();
    void test_exposure_async();
    void test_sharpen_async();
    void test_edgeDetect_async();

    // ---- 组合 ----
    void test_combined_satHueExp();

    // ---- Result 错误传递 ----
    void test_asyncResult_value();
    void test_asyncResult_errorOnEmpty();

    // ---- 边界 ----
    void test_extremeExposure();
    void test_saturationZero();

private:
    template <typename T>
    bool spinUntilFinished(QFuture<T> &future, int timeoutMs = 5000)
    {
        QElapsedTimer t;
        t.start();
        while (future.isRunning() && t.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return !future.isRunning();
    }

    cv::Mat makeMat(int w, int h, cv::Scalar bgr = cv::Scalar(50, 100, 200))
    {
        return cv::Mat(h, w, CV_8UC3, bgr);
    }
};

void tst_ColorAdjust::initTestCase()
{
    QVERIFY(QThreadPool::globalInstance() != nullptr);
}

void tst_ColorAdjust::cleanupTestCase()
{
}

// =============================================================================
//  同步测试
// =============================================================================

void tst_ColorAdjust::test_saturation_sync()
{
    cv::Mat src = makeMat(8, 8, cv::Scalar(0, 0, 255));   // 红色
    cv::Mat out;
    ImageProcessor::saturation(src, out, 0.5);
    QVERIFY(!out.empty());
    // 半饱和: 仍应该是红色 (R > B/G)
    const cv::Vec3b pix = out.at<cv::Vec3b>(4, 4);
    QVERIFY(pix[2] > pix[0]);
    QVERIFY(pix[2] > pix[1]);
}

void tst_ColorAdjust::test_saturation_noChange()
{
    // scale = 1.0 应该原样 (内部走 in.clone() 快速路径)
    cv::Mat src = makeMat(8, 8, cv::Scalar(100, 150, 200));
    cv::Mat out;
    ImageProcessor::saturation(src, out, 1.0);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[0]), 100);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[1]), 150);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);
}

void tst_ColorAdjust::test_hueShift_sync()
{
    // 红色 (BGR=0,0,255) hue 0 → +60° 应该变黄色 (B=0, G=255, R=255)
    cv::Mat src = makeMat(8, 8, cv::Scalar(0, 0, 255));
    cv::Mat out;
    ImageProcessor::hueShift(src, out, 60.0);
    const cv::Vec3b pix = out.at<cv::Vec3b>(4, 4);
    QVERIFY(pix[1] > 200);   // G
    QVERIFY(pix[2] > 200);   // R
}

void tst_ColorAdjust::test_hueShift_noChange()
{
    cv::Mat src = makeMat(8, 8, cv::Scalar(50, 100, 200));
    cv::Mat out;
    ImageProcessor::hueShift(src, out, 0.0);   // degrees = 0 → 快速路径
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[0]), 50);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[1]), 100);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[2]), 200);
}

void tst_ColorAdjust::test_exposure_sync()
{
    cv::Mat src = makeMat(8, 8, cv::Scalar(100, 100, 100));
    cv::Mat out;
    ImageProcessor::exposure(src, out, 1.0);   // ev=+1 → 2x
    // 100 * 2 = 200
    const int v = out.at<cv::Vec3b>(4, 4)[0];
    QVERIFY(v >= 195 && v <= 205);
}

void tst_ColorAdjust::test_exposure_brighten()
{
    // ev=+2 (4x): 100 * 4 = 400 → clamp 255
    cv::Mat src = makeMat(2, 2, cv::Scalar(100, 100, 100));
    cv::Mat out;
    ImageProcessor::exposure(src, out, 2.0);
    QCOMPARE(int(out.at<cv::Vec3b>(0, 0)[0]), 255);
}

void tst_ColorAdjust::test_exposure_darken()
{
    // ev=-2 (0.25x): 100 * 0.25 = 25
    cv::Mat src = makeMat(2, 2, cv::Scalar(100, 100, 100));
    cv::Mat out;
    ImageProcessor::exposure(src, out, -2.0);
    const int v = out.at<cv::Vec3b>(0, 0)[0];
    QVERIFY(v >= 22 && v <= 28);
}

// =============================================================================
//  异步测试
// =============================================================================

void tst_ColorAdjust::test_saturation_async()
{
    cv::Mat src = makeMat(16, 16, cv::Scalar(0, 0, 255));
    auto future = ImageProcessorAsync::saturation(src, 0.5);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    QVERIFY(r.isOk());
    QVERIFY(!r.value().empty());
    QCOMPARE(r.value().size(), src.size());
}

void tst_ColorAdjust::test_hueShift_async()
{
    cv::Mat src = makeMat(16, 16, cv::Scalar(0, 0, 255));
    auto future = ImageProcessorAsync::hueShift(src, 60.0);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    QVERIFY(r.isOk());
    const cv::Vec3b pix = r.value().at<cv::Vec3b>(8, 8);
    QVERIFY(pix[1] > 200);   // G (红色 hueShift+60 → 黄色)
    QVERIFY(pix[2] > 200);   // R
}

void tst_ColorAdjust::test_exposure_async()
{
    cv::Mat src = makeMat(16, 16, cv::Scalar(100, 100, 100));
    auto future = ImageProcessorAsync::exposure(src, 1.0);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    QVERIFY(r.isOk());
    QVERIFY(r.value().at<cv::Vec3b>(8, 8)[0] >= 195);
}

void tst_ColorAdjust::test_sharpen_async()
{
    cv::Mat src = makeMat(16, 16, cv::Scalar(100, 100, 100));
    auto future = ImageProcessorAsync::sharpen(src);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    QVERIFY(r.isOk());
    QVERIFY(!r.value().empty());
}

void tst_ColorAdjust::test_edgeDetect_async()
{
    cv::Mat src = cv::Mat::zeros(16, 16, CV_8UC3);
    src.at<cv::Vec3b>(8, 8) = cv::Vec3b(255, 255, 255);
    auto future = ImageProcessorAsync::edgeDetect(src, 80, 180);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    QVERIFY(r.isOk());
    QVERIFY(!r.value().empty());
}

// =============================================================================
//  组合测试
// =============================================================================

void tst_ColorAdjust::test_combined_satHueExp()
{
    // 模拟 imageWindow applyCurrentParams: exposure → saturation → hueShift 顺序
    cv::Mat work = makeMat(8, 8, cv::Scalar(100, 100, 100));
    cv::Mat tmp;

    ImageProcessor::exposure(work, tmp, 1.0);   work = tmp;   // 2x
    ImageProcessor::saturation(work, tmp, 1.5); work = tmp;
    ImageProcessor::hueShift(work, tmp, 0.0);  work = tmp;   // no-op

    // 100 * 2 = 200 (exposure)
    const int v = work.at<cv::Vec3b>(4, 4)[0];
    QVERIFY(v >= 195 && v <= 205);
}

// =============================================================================
//  Result 错误传递
// =============================================================================

void tst_ColorAdjust::test_asyncResult_value()
{
    cv::Mat src = makeMat(8, 8, cv::Scalar(50, 100, 200));
    auto future = ImageProcessorAsync::saturation(src, 1.0);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    QVERIFY(r.isOk());
    QCOMPARE(r.value().cols, 8);
    QCOMPARE(r.value().rows, 8);
    QCOMPARE(r.value().type(), CV_8UC3);
}

void tst_ColorAdjust::test_asyncResult_errorOnEmpty()
{
    cv::Mat empty;
    auto future = ImageProcessorAsync::saturation(empty, 0.5);
    QVERIFY(spinUntilFinished(future));
    auto r = future.result();
    // 空 Mat 走 saturation 内部 in.clone() 快速路径 → out empty → runOp 返 err
    QVERIFY(r.isErr());
    QCOMPARE(r.error().code, vistella::ErrorCode::InvalidArgument);
}

// =============================================================================
//  边界
// =============================================================================

void tst_ColorAdjust::test_extremeExposure()
{
    cv::Mat src = makeMat(2, 2, cv::Scalar(100, 100, 100));

    // ev = -3 (0.125x): 100 * 0.125 = 12.5 ≈ 12 或 13
    cv::Mat dark;
    ImageProcessor::exposure(src, dark, -3.0);
    int v = dark.at<cv::Vec3b>(0, 0)[0];
    QVERIFY(v >= 10 && v <= 15);

    // ev = +3 (8x): 100 * 8 = 800 → clamp 255
    cv::Mat bright;
    ImageProcessor::exposure(src, bright, 3.0);
    v = bright.at<cv::Vec3b>(0, 0)[0];
    QCOMPARE(v, 255);
}

void tst_ColorAdjust::test_saturationZero()
{
    // scale = 0: 完全去饱和, R = G = B (灰度)
    cv::Mat src = makeMat(8, 8, cv::Scalar(0, 0, 255));   // 纯红
    cv::Mat out;
    ImageProcessor::saturation(src, out, 0.0);
    const cv::Vec3b pix = out.at<cv::Vec3b>(4, 4);
    // R/G/B 差 < 10 (允许噪声)
    QVERIFY(std::abs(int(pix[2]) - int(pix[1])) < 10);
    QVERIFY(std::abs(int(pix[1]) - int(pix[0])) < 10);
}

QTEST_MAIN(tst_ColorAdjust)
#include "tst_ColorAdjust.moc"
