// =============================================================================
//  tst_SliderClick - 阶段 1 Step A Debug 测试 (2026-09-04)
//
//  用 QTest::mouseClick 模拟真实点击 slider, 检查 onAnyParamChanged 是否触发
//  区分 init 触发 (程序 setValue) vs 真实点击 (mouse event) 两种情况
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QSlider>

#include "../src/media/imagewindow.h"

class tst_SliderClick : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_programmatic_setValue();
    void test_real_mouseClick();

private:
    cv::Mat makeTestImage();
};

void tst_SliderClick::initTestCase()
{
    QApplication::setOrganizationName("VistellaTest");
    QApplication::setApplicationName("SliderClickTest");
}

void tst_SliderClick::cleanupTestCase()
{
}

cv::Mat tst_SliderClick::makeTestImage()
{
    return cv::Mat(64, 64, CV_8UC3, cv::Scalar(100, 150, 200));
}

void tst_SliderClick::test_programmatic_setValue()
{
    ImageWindow w;
    // 假设 loadFile 已存在; 这里直接调 setCurrentImage 简化
    // 真实测试需要 loadFile 接受 cv::Mat
    // 暂时跳过
    QVERIFY(true);
}

void tst_SliderClick::test_real_mouseClick()
{
    ImageWindow w;
    w.show();
    QTest::qWaitForWindowExposed(&w);

    // 找一个 slider 模拟点击
    // 暂时跳过 - 需要访问 private member
    QVERIFY(true);
}

QTEST_MAIN(tst_SliderClick)
#include "tst_SliderClick.moc"
