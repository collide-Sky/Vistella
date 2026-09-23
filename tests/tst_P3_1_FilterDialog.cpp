// SPDX-License-Identifier: MIT
//
// tst_P3_1_FilterDialog - P3.1.4 (2026-09-22)
//
// P3.1.1 (2026-09-22) FilterDialog 参数 UI 单测:
//   1) GaussianBlur dialog: ksize slider + spinbox 联动; spinbox 改动=11 后,
//      strategy->ksize == 11, m_paramLabel 文本包含 "ksize=11"
//   2) UnsharpMask dialog: 3 rows, amount slider 改动 strategy->amount 同步
//   3) Sharpen dialog (无参): 显示 "(无参数)" placeholder, 无 slider
//   4) strategy() accessor 暴露 FilterStrategy* (P3.1.1 接入 Apply 预览)
//   5) 全 11 种 hasParam=true filter 创建 dialog 不崩溃 + 都能取 strategy()
//   6) 全 9 种 hasParam=false filter 创建 dialog 不崩溃
//
#include <QTest>
#include <QApplication>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QGridLayout>

#include <opencv2/core.hpp>

#include "../src/media/imagewindow.h"
#include "../src/media/filters/FilterStrategy.h"
#include "../src/media/filters/FilterDialog.h"
#include "../src/media/filters/FilterFactory.h"

using namespace filter;

class tst_P3_1_FilterDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- 4 个核心用例 ----
    void test_paramUI_gaussian();
    void test_paramUI_unsharp();
    void test_paramUI_noParam_sharpen();
    void test_strategy_accessor_gaussian();

    // ---- 全 11 hasParam=true filter dialog 创建 + strategy() 取出来 ----
    void test_allParamFilters_createDialog();
    // ---- 全 9 hasParam=false filter dialog 创建 ----
    void test_allNonParamFilters_createDialog();

private:
    ImageWindow *m_w = nullptr;
};

void tst_P3_1_FilterDialog::initTestCase()
{
    // ImageWindow 是 QMainWindow (跟 tst_ImageEditCommand 一样)
    m_w = new ImageWindow();
}

void tst_P3_1_FilterDialog::cleanupTestCase()
{
    delete m_w;
    m_w = nullptr;
}

// 工具: 找 dialog 里第一个 int slider (assumes single param row with int field)
static QSlider* findIntSlider(FilterDialog* dlg)
{
    auto sliders = dlg->findChildren<QSlider*>();
    for (auto *s : sliders) {
        if (s->maximum() <= 255 || s->minimum() >= 1) return s;
    }
    return sliders.value(0, nullptr);
}

static QSpinBox* findIntSpin(FilterDialog* dlg)
{
    auto spins = dlg->findChildren<QSpinBox*>();
    return spins.value(0, nullptr);
}

static QDoubleSpinBox* findDoubleSpin(FilterDialog* dlg)
{
    auto spins = dlg->findChildren<QDoubleSpinBox*>();
    return spins.value(0, nullptr);
}

static QSlider* findFirstSlider(FilterDialog* dlg)
{
    auto sliders = dlg->findChildren<QSlider*>();
    return sliders.value(0, nullptr);
}

static QLabel* findParamLabel(FilterDialog* dlg)
{
    // P3.1.4: 找 objectName == "paramLabel" 的 QLabel (FilterDialog 唯一指定的标识)
    return dlg->findChild<QLabel*>(QStringLiteral("paramLabel"));
}

void tst_P3_1_FilterDialog::test_paramUI_gaussian()
{
    auto *dlg = new FilterDialog(m_w, FilterKind::GaussianBlur);
    auto *s = static_cast<GaussianBlurFilter*>(dlg->strategy());
    QVERIFY(s != nullptr);
    QCOMPARE(s->kind(), FilterKind::GaussianBlur);

    // 默认 ksize=5
    QCOMPARE(s->ksize, 5);

    // 改 ksize 到 15: 找到第一个 QSpinBox (ksize), setValue(15)
    auto *spin = findIntSpin(dlg);
    QVERIFY(spin != nullptr);
    spin->setValue(15);
    // 验证 strategy 已同步 (这是 P3.1.1 关键 contract)
    QCOMPARE(s->ksize, 15);

    // 验证 m_paramLabel 文本包含 "ksize=15"
    auto *paramLab = findParamLabel(dlg);
    QVERIFY(paramLab != nullptr);
    QVERIFY(paramLab->text().contains("ksize=15"));

    delete dlg;
}

void tst_P3_1_FilterDialog::test_paramUI_unsharp()
{
    auto *dlg = new FilterDialog(m_w, FilterKind::UnsharpMask);
    auto *s = static_cast<UnsharpMaskFilter*>(dlg->strategy());
    QVERIFY(s != nullptr);
    QCOMPARE(s->kind(), FilterKind::UnsharpMask);

    // 默认 amount=1.0
    QCOMPARE(s->amount, 1.0);

    // 改 amount 到 2.5
    auto *dspin = findDoubleSpin(dlg);
    QVERIFY(dspin != nullptr);
    dspin->setValue(2.5);
    QCOMPARE(s->amount, 2.5);

    // 验证 paramText 包含 amount=2.5
    auto *paramLab = findParamLabel(dlg);
    QVERIFY(paramLab != nullptr);
    QVERIFY(paramLab->text().contains("amount=2.5"));

    delete dlg;
}

void tst_P3_1_FilterDialog::test_paramUI_noParam_sharpen()
{
    auto *dlg = new FilterDialog(m_w, FilterKind::Sharpen);
    auto *s = dlg->strategy();
    QVERIFY(s != nullptr);
    QVERIFY(!s->hasParam());

    // 不应该有 QSpinBox / QDoubleSpinBox / QSlider (无参 filter)
    auto spins = dlg->findChildren<QSpinBox*>();
    auto dspins = dlg->findChildren<QDoubleSpinBox*>();
    auto sliders = dlg->findChildren<QSlider*>();
    QCOMPARE(spins.size(), 0);
    QCOMPARE(dspins.size(), 0);
    QCOMPARE(sliders.size(), 0);

    // m_paramLabel 应包含 "无"
    auto *paramLab = findParamLabel(dlg);
    QVERIFY(paramLab != nullptr);
    QVERIFY(paramLab->text().contains("无"));

    delete dlg;
}

void tst_P3_1_FilterDialog::test_strategy_accessor_gaussian()
{
    auto *dlg = new FilterDialog(m_w, FilterKind::GaussianBlur);
    QVERIFY(dlg->strategy() != nullptr);
    QCOMPARE(dlg->strategy()->kind(), FilterKind::GaussianBlur);

    // strategy 是 dialog-owned (unique_ptr). 改 spinbox → 同一个 strategy 实例字段变
    auto *s = static_cast<GaussianBlurFilter*>(dlg->strategy());
    auto *spin = findIntSpin(dlg);
    QVERIFY(spin != nullptr);
    spin->setValue(21);
    QCOMPARE(s->ksize, 21);

    delete dlg;
}

void tst_P3_1_FilterDialog::test_allParamFilters_createDialog()
{
    // 11 hasParam=true filter kinds (GaussianBlur/BoxBlur/MedianBlur/BilateralBlur/
    //   UnsharpMask/Threshold/Posterize/PhotoFilter/HighPass/Solarize/GradientMap)
    const FilterKind paramKinds[] = {
        FilterKind::GaussianBlur, FilterKind::BoxBlur, FilterKind::MedianBlur,
        FilterKind::BilateralBlur, FilterKind::UnsharpMask, FilterKind::Threshold,
        FilterKind::Posterize, FilterKind::GradientMap, FilterKind::PhotoFilter,
        FilterKind::HighPass, FilterKind::Solarize,
    };
    for (auto k : paramKinds) {
        auto *dlg = new FilterDialog(m_w, k);
        QVERIFY2(dlg->strategy() != nullptr, "strategy null");
        QCOMPARE(dlg->strategy()->kind(), k);
        delete dlg;
    }
}

void tst_P3_1_FilterDialog::test_allNonParamFilters_createDialog()
{
    // 9 hasParam=false (Sharpen/SharpenMore/Emboss/FindEdges/GlowingEdges/
    //   Desaturate/Invert/BlurMore/FilterGallery)
    const FilterKind nonParamKinds[] = {
        FilterKind::Sharpen, FilterKind::SharpenMore, FilterKind::Emboss,
        FilterKind::FindEdges, FilterKind::GlowingEdges, FilterKind::Desaturate,
        FilterKind::Invert, FilterKind::BlurMore, FilterKind::FilterGallery,
    };
    for (auto k : nonParamKinds) {
        auto *dlg = new FilterDialog(m_w, k);
        QVERIFY2(dlg->strategy() != nullptr, "strategy null");
        QCOMPARE(dlg->strategy()->kind(), k);
        QVERIFY(!dlg->strategy()->hasParam());
        delete dlg;
    }
}

QTEST_MAIN(tst_P3_1_FilterDialog)
#include "tst_P3_1_FilterDialog.moc"