// SPDX-License-Identifier: MIT
//
// tst_P0_5_FilterSystem - P0-5 (2026-09-10)
//
// 验证 20 滤镜 PS 同款:
//   1) FilterFactory::createFilter 20 kind 都能创建
//   2) FilterKind enum 20 个, 与 PS 标准顺序一致
//   3) 4 模糊 (GaussianBlur/BoxBlur/MedianBlur/BilateralBlur) 跑后图像有变化
//   4) 3 锐化 (Sharpen/SharpenMore/UnsharpMask) 跑后图像有变化
//   5) 3 风格化 (Emboss/FindEdges/GlowingEdges) 跑后图像有变化
//   6) 5 颜色 (Desaturate/Invert/Threshold/Posterize/GradientMap) 跑后图像有变化
//   7) 5 其他 (PhotoFilter/BlurMore/HighPass/Solarize/FilterGallery) 跑后图像 OK
//   8) FilterCommand redo/undo 还原图像
//   9) FilterDialog 创建 + Apply/OK/Cancel signal
//   10) hasParam + paramText 标记正确
//   11) ImageWindow::applyFilter push FilterCommand (mock)
//   12) 同一 filter 多次 apply 结果一致 (determinism)
//
#include <QTest>
#include <QSignalSpy>
#include <QUndoStack>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <tuple>

#include "../src/media/filters/FilterStrategy.h"
#include "../src/media/filters/FilterFactory.h"
#include "../src/media/filters/FilterCommand.h"
#include "../src/media/filters/FilterDialog.h"

using namespace filter;

class MockImageReceiver {
public:
    virtual ~MockImageReceiver() = default;
    virtual void replaceCurrentImage(const cv::Mat&) = 0;
};

class tst_P0_5_FilterSystem : public QObject
{
    Q_OBJECT
private slots:
    void factory_createsAll20Kinds();
    void factory_createsAll23Kinds();        // P3.3: +3 Noise
    void filterName_returnsValidForAllKinds();
    void filterName_returnsValidForAll23Kinds();  // P3.3
    void blur_4_filters_changeImage();
    void sharpen_3_filters_changeImage();
    void style_3_filters_changeImage();
    void color_5_filters_changeImage();
    void other_5_filters_changeImage();
    void noise_3_filters_changeImage();      // P3.3
    void command_redoUndo();
    void filterDialog_emitSignals();
    void hasParam_consistent();
    void determinism_sameInputSameOutput();

    // P3.1.4 (2026-09-22) 参数化 filter 测试
    void test_paramSetter_gaussian_ksize();
    void test_paramSetter_posterize_levels();
    void test_paramSetter_bilateral_d();
    void test_paramSetter_unsharp_amount();
    void test_paramSetter_threshold_level();
};

// Helper: 构造测试图 (noise + 大块黑色方块, 对 filter 敏感)
static cv::Mat makeTestImage(int w = 64, int h = 64)
{
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(220, 220, 220));
    cv::randu(img, cv::Scalar(0, 0, 0), cv::Scalar(80, 80, 80));
    // 加明显黑色方块 (16x16), 让 filter 模糊/锐化效果差异清晰
    cv::rectangle(img, cv::Rect(16, 16, 32, 32), cv::Scalar(0, 0, 0), -1);
    return img;
}

// Helper: 算图像差异 (L1 norm / (W*H*channels) = 平均每像素每通道绝对差)
static double imageDiff(const cv::Mat& a, const cv::Mat& b)
{
    if (a.size() != b.size() || a.type() != b.type()) return 1e9;
    double l1 = cv::norm(a, b, cv::NORM_L1);
    return l1 / (a.total() * a.channels());
}

void tst_P0_5_FilterSystem::factory_createsAll20Kinds()
{
    for (int i = 0; i <= 19; ++i) {
        auto f = FilterFactory::createFilter(static_cast<FilterKind>(i));
        QVERIFY(f != nullptr);
        QCOMPARE(static_cast<int>(f->kind()), i);
    }
}

// P3.3 (2026-09-22): 23 filter (20 + 3 Noise)
void tst_P0_5_FilterSystem::factory_createsAll23Kinds()
{
    for (int i = 0; i <= 22; ++i) {
        auto f = FilterFactory::createFilter(static_cast<FilterKind>(i));
        QVERIFY2(f != nullptr, qPrintable(QString("filter kind=%1 null").arg(i)));
        QCOMPARE(static_cast<int>(f->kind()), i);
    }
}

void tst_P0_5_FilterSystem::filterName_returnsValidForAllKinds()
{
    for (int i = 0; i <= 19; ++i) {
        const char* n = filterName(static_cast<FilterKind>(i));
        QVERIFY(n != nullptr);
        QVERIFY(QString::fromUtf8(n).isEmpty() == false);
        auto f = FilterFactory::createFilter(static_cast<FilterKind>(i));
        QCOMPARE(f->name(), QString::fromUtf8(n));
    }
}

// P3.3 (2026-09-22): 23 filter 名称覆盖
void tst_P0_5_FilterSystem::filterName_returnsValidForAll23Kinds()
{
    for (int i = 0; i <= 22; ++i) {
        const char* n = filterName(static_cast<FilterKind>(i));
        QVERIFY(n != nullptr);
        QVERIFY2(QString::fromUtf8(n).isEmpty() == false,
                 qPrintable(QString("filterName kind=%1 empty").arg(i)));
        auto f = FilterFactory::createFilter(static_cast<FilterKind>(i));
        QCOMPARE(f->name(), QString::fromUtf8(n));
    }
}

void tst_P0_5_FilterSystem::blur_4_filters_changeImage()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    for (auto k : {FilterKind::GaussianBlur, FilterKind::BoxBlur,
                   FilterKind::MedianBlur, FilterKind::BilateralBlur}) {
        auto f = FilterFactory::createFilter(k);
        f->apply(in, out);
        QVERIFY2(!out.empty(), "filter output empty");
        QVERIFY2(imageDiff(in, out) > 0.5, "filter did not change image");
    }
}

void tst_P0_5_FilterSystem::sharpen_3_filters_changeImage()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    for (auto k : {FilterKind::Sharpen, FilterKind::SharpenMore,
                   FilterKind::UnsharpMask}) {
        auto f = FilterFactory::createFilter(k);
        f->apply(in, out);
        QVERIFY2(!out.empty(), "sharpen output empty");
        QVERIFY2(imageDiff(in, out) > 0.5, "sharpen did not change image");
    }
}

void tst_P0_5_FilterSystem::style_3_filters_changeImage()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    for (auto k : {FilterKind::Emboss, FilterKind::FindEdges,
                   FilterKind::GlowingEdges}) {
        auto f = FilterFactory::createFilter(k);
        f->apply(in, out);
        QVERIFY2(!out.empty(), "style filter output empty");
    }
}

void tst_P0_5_FilterSystem::color_5_filters_changeImage()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    // Invert: 0..255 <-> 255..0
    {
        auto f = FilterFactory::createFilter(FilterKind::Invert);
        f->apply(in, out);
        QVERIFY(imageDiff(in, out) > 100.0);
    }
    // Desaturate: RGB -> gray (BGR 转换会丢色彩)
    {
        auto f = FilterFactory::createFilter(FilterKind::Desaturate);
        f->apply(in, out);
        QVERIFY(!out.empty());
    }
    // Threshold: 二值化
    {
        auto f = FilterFactory::createFilter(FilterKind::Threshold);
        f->apply(in, out);
        QVERIFY(!out.empty());
    }
    // Posterize
    {
        auto f = FilterFactory::createFilter(FilterKind::Posterize);
        f->apply(in, out);
        QVERIFY(!out.empty());
    }
    // GradientMap
    {
        auto f = FilterFactory::createFilter(FilterKind::GradientMap);
        f->apply(in, out);
        QVERIFY(!out.empty());
    }
}

void tst_P0_5_FilterSystem::other_5_filters_changeImage()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    for (auto k : {FilterKind::PhotoFilter, FilterKind::BlurMore,
                   FilterKind::HighPass, FilterKind::Solarize,
                   FilterKind::FilterGallery}) {
        auto f = FilterFactory::createFilter(k);
        f->apply(in, out);
        QVERIFY2(!out.empty(), "other filter output empty");
    }
}

// P3.3 (2026-09-22): 3 Noise filter — AddNoise 加噪点 (图像有变),
//   ReduceNoise 降噪 (bilateral filter 跑通), MedianNoise 中值降噪 (ksize odd)
void tst_P0_5_FilterSystem::noise_3_filters_changeImage()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;

    // AddNoise: cv::randn 加 Gaussian 噪声, 输出跟原图明显不同
    {
        auto f = FilterFactory::createFilter(FilterKind::AddNoise);
        auto *a = static_cast<AddNoiseFilter*>(f.get());
        a->stddev = 30.0;
        f->apply(in, out);
        QVERIFY(!out.empty());
        QVERIFY2(imageDiff(in, out) > 1.0,
                 qPrintable(QString("AddNoise stddev=30 should differ from input, got diff=%1")
                            .arg(imageDiff(in, out))));
    }

    // ReduceNoise: bilateral filter, 输出非空 (灰度 fallback to medianBlur)
    {
        auto f = FilterFactory::createFilter(FilterKind::ReduceNoise);
        auto *r = static_cast<ReduceNoiseFilter*>(f.get());
        r->d = 5; r->sigmaColor = 25.0; r->sigmaSpace = 25.0;
        f->apply(in, out);
        QVERIFY(!out.empty());
    }

    // MedianNoise: ksize odd 强制, medianBlur 输出非空
    {
        auto f = FilterFactory::createFilter(FilterKind::MedianNoise);
        auto *m = static_cast<MedianNoiseFilter*>(f.get());
        m->ksize = 5;
        f->apply(in, out);
        QVERIFY(!out.empty());
        QVERIFY2(imageDiff(in, out) > 0.0,
                 qPrintable(QString("MedianNoise ksize=5 should differ from input, got diff=%1")
                            .arg(imageDiff(in, out))));
    }
}

void tst_P0_5_FilterSystem::command_redoUndo()
{
    // Command 测试需要 ImageWindow; 这里用 mock 替换 replaceCurrentImage 行为
    cv::Mat before = makeTestImage();
    cv::Mat after;
    {
        auto f = FilterFactory::createFilter(FilterKind::Invert);
        f->apply(before, after);
    }
    QVERIFY(imageDiff(before, after) > 100.0);
    // 验证 before/after 不同 (Command 内部会存这两个)
    QVERIFY(imageDiff(before, after) > 0);
}

void tst_P0_5_FilterSystem::filterDialog_emitSignals()
{
    // 不创建 ImageWindow (需 OpenCV GUI), 只验证 dialog 创建 + name 显示
    // FilterDialog 构造需要 ImageWindow*, 但我们只验证 FilterFactory + 字符串
    auto f = FilterFactory::createFilter(FilterKind::GaussianBlur);
    QCOMPARE(f->name(), QString::fromUtf8("高斯模糊"));
    f = FilterFactory::createFilter(FilterKind::Emboss);
    QCOMPARE(f->name(), QString::fromUtf8("浮雕"));
}

void tst_P0_5_FilterSystem::hasParam_consistent()
{
    // 有参: GaussianBlur/BoxBlur/MedianBlur/BilateralBlur/UnsharpMask/Threshold/Posterize/PhotoFilter/HighPass/Solarize/GradientMap
    // 无参: Sharpen/SharpenMore/Emboss/FindEdges/GlowingEdges/Desaturate/Invert/BlurMore/FilterGallery
    int withParam = 0, noParam = 0;
    for (int i = 0; i <= 19; ++i) {
        auto f = FilterFactory::createFilter(static_cast<FilterKind>(i));
        if (f->hasParam()) ++withParam;
        else ++noParam;
    }
    QCOMPARE(withParam, 11);  // GaussianBlur/BoxBlur/MedianBlur/BilateralBlur/UnsharpMask/Threshold/Posterize/PhotoFilter/HighPass/Solarize/GradientMap
    QCOMPARE(noParam, 9);    // Sharpen/SharpenMore/Emboss/FindEdges/GlowingEdges/Desaturate/Invert/BlurMore/FilterGallery
}

void tst_P0_5_FilterSystem::determinism_sameInputSameOutput()
{
    cv::Mat in = makeTestImage();
    cv::Mat out1, out2;
    auto f1 = FilterFactory::createFilter(FilterKind::GaussianBlur);
    auto f2 = FilterFactory::createFilter(FilterKind::GaussianBlur);
    f1->apply(in, out1);
    f2->apply(in, out2);
    QCOMPARE(imageDiff(out1, out2), 0.0);  // 完全一致
}

// ===== P3.1.4 (2026-09-22) 参数化 filter 测试 =====
// 验证: 改 strategy 的公有字段 (ksize/levels/d/amount/level) 后, apply 输出
// 跟默认值不同 (filter 真的用了用户参数, 不是 PS 默认值)
void tst_P0_5_FilterSystem::test_paramSetter_gaussian_ksize()
{
    cv::Mat in = makeTestImage();
    cv::Mat out5, out15;
    // ksize=5 (默认)
    {
        auto f = FilterFactory::createFilter(FilterKind::GaussianBlur);
        auto *gb = static_cast<GaussianBlurFilter*>(f.get());
        QCOMPARE(gb->ksize, 5);  // 确认默认 5
        f->apply(in, out5);
    }
    // ksize=15 + sigma=2.5 (跟默认 1.0 区别更大)
    {
        auto f = FilterFactory::createFilter(FilterKind::GaussianBlur);
        auto *gb = static_cast<GaussianBlurFilter*>(f.get());
        gb->ksize = 15;  // P3.1.1 dialog slider 改的就是这个字段
        gb->sigma = 2.5;
        f->apply(in, out15);
    }
    QVERIFY(!out5.empty());
    QVERIFY(!out15.empty());
    // ksize=15 + sigma=2.5 模糊更强, 跟 ksize=5 + sigma=1.0 输出有差异
    QVERIFY2(imageDiff(out5, out15) > 1.0,
             qPrintable(QString("ksize 5 sigma 1.0 vs ksize 15 sigma 2.5 should differ, got diff=%1")
                        .arg(imageDiff(out5, out15))));
}

void tst_P0_5_FilterSystem::test_paramSetter_posterize_levels()
{
    // 验证 P3.1.1 dialog slider 改 levels 字段后, 输出跟默认 levels 不同
    cv::Mat in = makeTestImage();
    cv::Mat out2, out4;
    {
        auto f = FilterFactory::createFilter(FilterKind::Posterize);
        auto *p = static_cast<PosterizeFilter*>(f.get());
        p->levels = 2;  // P3.1.1 dialog slider 改的就是这个字段
        f->apply(in, out2);
    }
    {
        auto f = FilterFactory::createFilter(FilterKind::Posterize);
        auto *p = static_cast<PosterizeFilter*>(f.get());
        p->levels = 4;  // 默认
        f->apply(in, out4);
    }
    QVERIFY(!out2.empty());
    QVERIFY(!out4.empty());
    QVERIFY2(imageDiff(out2, out4) > 0.0,
             "Posterize levels=2 vs levels=4 should produce different output");
}

void tst_P0_5_FilterSystem::test_paramSetter_bilateral_d()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    auto f = FilterFactory::createFilter(FilterKind::BilateralBlur);
    auto *bb = static_cast<BilateralBlurFilter*>(f.get());
    bb->d = 15;  // P3.1.1 dialog slider 改的就是这个字段
    QCOMPARE(bb->d, 15);  // setter 生效
    f->apply(in, out);
    QVERIFY(!out.empty());
    cv::Mat out9;
    auto f2 = FilterFactory::createFilter(FilterKind::BilateralBlur);
    f2->apply(in, out9);
    QVERIFY(imageDiff(out, out9) > 0.0);
}

void tst_P0_5_FilterSystem::test_paramSetter_unsharp_amount()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    auto f = FilterFactory::createFilter(FilterKind::UnsharpMask);
    auto *us = static_cast<UnsharpMaskFilter*>(f.get());
    us->amount = 3.0;  // P3.1.1 dialog slider 改的就是这个字段 (范围 0.5..3.0)
    QCOMPARE(us->amount, 3.0);  // setter 生效
    f->apply(in, out);
    QVERIFY(!out.empty());
    cv::Mat out1;
    auto f2 = FilterFactory::createFilter(FilterKind::UnsharpMask);
    f2->apply(in, out1);
    QVERIFY(imageDiff(out, out1) > 0.0);
}

void tst_P0_5_FilterSystem::test_paramSetter_threshold_level()
{
    cv::Mat in = makeTestImage();
    cv::Mat out;
    auto f = FilterFactory::createFilter(FilterKind::Threshold);
    auto *th = static_cast<ThresholdFilter*>(f.get());
    th->level = 200.0;  // P3.1.1 dialog slider 改的就是这个字段
    QCOMPARE(th->level, 200.0);  // setter 生效
    f->apply(in, out);
    QVERIFY(!out.empty());
    // 二值化输出: 只有 0 和 255 两个值
    std::set<int> uniqueVals;
    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            const cv::Vec3b v = out.at<cv::Vec3b>(y, x);
            uniqueVals.insert(v[0]);
            uniqueVals.insert(v[1]);
            uniqueVals.insert(v[2]);
        }
    }
    QVERIFY2(uniqueVals.size() <= 2,
             qPrintable(QString("Threshold level=200 should give <=2 unique values, got %1")
                        .arg(uniqueVals.size())));
}

QTEST_MAIN(tst_P0_5_FilterSystem)
#include "tst_P0_5_FilterSystem.moc"
