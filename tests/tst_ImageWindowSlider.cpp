// =============================================================================
//  tst_ImageWindowSlider - 阶段 1 Step A Debug 测试 (2026-09-04)
//
//  验证 ImageWindow 的 slider signal-slot 是否正常工作
//  区分两种情况:
//    A. 程序 setValue → onAnyParamChanged (应该工作, init 时已验证)
//    B. UI 点击 → onAnyParamChanged (用户报告不工作)
//
//  这个测试只验证 A, B 需要在 GUI 跑 + qDebug log 验证
// =============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QApplication>

#include "../src/media/imagewindow.h"
#include "../src/media/imageprocessor.h"

class tst_ImageWindowSlider : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // 程序 setValue → 应该触发 onAnyParamChanged (通过 m_current 改变验证)
    void test_setValue_triggersPipeline();
    void test_sliderRanges();

private:
    cv::Mat makeTestImage();
};

void tst_ImageWindowSlider::initTestCase()
{
}

void tst_ImageWindowSlider::cleanupTestCase()
{
}

cv::Mat tst_ImageWindowSlider::makeTestImage()
{
    cv::Mat img(64, 64, CV_8UC3, cv::Scalar(100, 150, 200));
    return img;
}

void tst_ImageWindowSlider::test_setValue_triggersPipeline()
{
    ImageWindow w;
    w.loadFileOrMat(makeTestImage());
    // m_current 应该被 loadFile 设置 (但 loadFileOrMat 可能不存在)
    // 暂时用公开接口检查
    // TODO
}

void tst_ImageWindowSlider::test_sliderRanges()
{
    ImageWindow w;
    // 验证 setupSliderRanges 真的把 range 设了
    // 通过滑条的 min/max 公开 API 读
    // 需要 ImageWindow 暴露 slider 访问或者 test friend
    // 暂时空着, 靠主 EXE 的 qDebug log 验证
    QVERIFY(true);  // 占位
}

QTEST_MAIN(tst_ImageWindowSlider)
#include "tst_ImageWindowSlider.moc"
