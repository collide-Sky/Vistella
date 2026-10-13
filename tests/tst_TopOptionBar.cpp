// =============================================================================
//  tst_TopOptionBar - P7.5 (2026-10-12)
//
//  验证 PS 风格 顶部共享选项栏 (mode/opacity/flow/smoothing/angle/size) 创建 + 信号触发.
//    1) 默认构造 — 6 个控件存在 (m_cmbBlendMode / opacity / flow / smoothing / angle / size)
//    2) 默认值 (PS 风格: 正常模式 / 不透明度 100% / 流量 100% / 平滑 10% / 角度 0° / 大小 13 px)
//    3) 改 不透明度 触发 opacityChanged 信号
//    4) 改 大小 触发 brushSizeChanged 信号
//    5) getter (opacity/flow/angle/brushSize) 反映新值
// =============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QToolButton>

#include "../src/media/tools/TopOptionBar.h"

using namespace tools;

class tst_TopOptionBar : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_default_values();
    void test_6_widgets_exist();
    void test_opacity_change_emits();
    void test_brushSize_change_emits();
    void test_blendMode_setter();
    void test_setters_roundtrip();
};

void tst_TopOptionBar::initTestCase() {}
void tst_TopOptionBar::cleanupTestCase() {}

void tst_TopOptionBar::test_default_values()
{
    TopOptionBar bar;
    QCOMPARE(bar.blendMode(), TopOptionBar::BlendMode::Normal);
    QCOMPARE(bar.opacity(), 100.0);
    QCOMPARE(bar.flow(), 100.0);
    QCOMPARE(bar.smoothing(), 10.0);
    QCOMPARE(bar.angle(), 0);
    QCOMPARE(bar.brushSize(), 13);
}

void tst_TopOptionBar::test_6_widgets_exist()
{
    TopOptionBar bar;
    // P7.5 stage: visual widget with 6 controls.
    //   通过 findChildren 验证 (不暴露 m_* 给外部).
    auto combos = bar.findChildren<QComboBox*>();
    QVERIFY(combos.size() >= 1);                      // blend mode combo
    auto spinBoxes = bar.findChildren<QSpinBox*>();
    QVERIFY(spinBoxes.size() >= 2);                   // angle + size 是 QSpinBox
    auto dblSpinBoxes = bar.findChildren<QDoubleSpinBox*>();
    QVERIFY(dblSpinBoxes.size() >= 3);                // opacity + flow + smoothing 是 QDoubleSpinBox
    auto buttons = bar.findChildren<QToolButton*>();
    QVERIFY(buttons.size() >= 2);                     // angleLock + sizeLink 两个 lock btn
}

void tst_TopOptionBar::test_opacity_change_emits()
{
    TopOptionBar bar;
    QSignalSpy spy(&bar, &TopOptionBar::opacityChanged);
    bar.findChildren<QDoubleSpinBox*>().first()->setValue(50.0);   // opacity 是第一个 dbl
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<double>(spy.at(0).at(0)), 50.0);
}

void tst_TopOptionBar::test_brushSize_change_emits()
{
    TopOptionBar bar;
    // size 是第 2 个 QSpinBox (angle + size), 不是 DoubleSpinBox.
    auto spins = bar.findChildren<QSpinBox*>();
    QVERIFY(spins.size() >= 2);
    QSignalSpy spy(&bar, &TopOptionBar::brushSizeChanged);
    spins.at(1)->setValue(48);   // size (angle 是 spins[0])
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<int>(spy.at(0).at(0)), 48);
}

void tst_TopOptionBar::test_blendMode_setter()
{
    TopOptionBar bar;
    QSignalSpy spy(&bar, &TopOptionBar::blendModeChanged);
    bar.findChildren<QComboBox*>().first()->setCurrentIndex(static_cast<int>(TopOptionBar::BlendMode::Multiply));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(bar.blendMode(), TopOptionBar::BlendMode::Multiply);
}

void tst_TopOptionBar::test_setters_roundtrip()
{
    // getter 应该反映 setter 后的状态 (经 QSpinBox/QDoubleSpinBox 内部 state).
    TopOptionBar bar;
    bar.findChildren<QDoubleSpinBox*>().at(0)->setValue(75.0);   // opacity
    QCOMPARE(bar.opacity(), 75.0);
    bar.findChildren<QDoubleSpinBox*>().at(1)->setValue(50.0);   // flow
    QCOMPARE(bar.flow(), 50.0);
    bar.findChildren<QDoubleSpinBox*>().at(2)->setValue(25.0);   // smoothing
    QCOMPARE(bar.smoothing(), 25.0);
    // spin boxes: angle + size — angle 在 size 前
    auto spins = bar.findChildren<QSpinBox*>();
    QVERIFY(spins.size() >= 2);
    spins.at(0)->setValue(135);   // angle
    QCOMPARE(bar.angle(), 135);
    spins.at(1)->setValue(60);   // size
    QCOMPARE(bar.brushSize(), 60);
}

QTEST_MAIN(tst_TopOptionBar)
#include "tst_TopOptionBar.moc"
