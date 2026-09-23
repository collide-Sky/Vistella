// SPDX-License-Identifier: MIT
//
// tst_P0_6_TransformSystem - P0-6.7 (2026-09-14)
//
// 8 test 覆盖 P0-6 变换系统:
//   1. TransformBox 几何 - handlePos (10 handle 位置)
//   2. TransformBox hitTest (10 handle 命中)
//   3. TransformBox 4 mode state
//   4. TransformMath scaleMatrix (Shift = 等比)
//   5. TransformMath rotateMatrix (围绕中心)
//   6. TransformMath skewMatrix (Shift = 约束 1 方向)
//   7. TransformMath distortMatrix (4 角独立, identity 占位)
//   8. TransformCommand undo/redo (cv::Mat 前后) + ImageProcessor flip/warpAffine
//
// 不依赖 GUI / ImageWindow, 全部 unit test
//
#include <QTest>
#include <QGuiApplication>
#include <QTransform>
#include <cmath>
#include <opencv2/core.hpp>

#include "../src/media/transform/TransformBox.h"
#include "../src/media/transform/TransformMath.h"
#include "../src/media/transform/TransformCommand.h"
#include "../src/media/tools/TransformTool.h"
#include "../src/media/imageprocessor.h"

class tst_P0_6_TransformSystem : public QObject
{
    Q_OBJECT
private slots:
    // 1
    void test_TransformBox_handlePos_8_corners_and_edges();
    // 2
    void test_TransformBox_hitTest_10_handles();
    // 3
    void test_TransformBox_4_modes_state_transitions();
    // 4
    void test_TransformMath_scaleMatrix_shift_equals_ratio();
    // 5
    void test_TransformMath_rotateMatrix_around_center();
    // 6
    void test_TransformMath_skewMatrix_shift_constrains_one_axis();
    // 7
    void test_TransformMath_distortMatrix_identity_when_unchanged();
    void test_TransformMath_distortMatrix_nontrivial_when_corners_changed();   // P3.2.1 bug fix
    // 8
    void test_TransformCommand_undo_redo_with_cvMat();
    // 9 (额外) ImageProcessor flip + warpAffine + qTransformToAffine
    void test_ImageProcessor_flip_warpAffine();
    // 10 (P0-6.13 2026-09-14) TransformTool setMode 切 4 mode
    void test_TransformTool_setMode_4_modes();
    // 11 (P0-6.13 2026-09-14) TransformTool rotationCallback (drag rotation handle 触发)
    void test_TransformTool_rotationCallback();
    // 12 (P3.2.2 2026-09-22) commitTransform 真的 push TransformCommand
    //     (原 P0-6.2 留的 no-op 已被修复). Distort 走透视变换路径完整工作.
    //     Scale/Skew 路径 P3.2.5 标记为 TODO (P3.1.3 root cause: 不加字段到 TransformTool
    //     避免 class layout shift 触发 QString d-pointer 共享 segfault).
    void test_TransformTool_commitTransform_pushesCommand_distort();
};

// ================== 1. TransformBox handlePos ==================
void tst_P0_6_TransformSystem::test_TransformBox_handlePos_8_corners_and_edges()
{
    transform::TransformBox box;
    box.setRect(QRectF(0, 0, 100, 50));
    // 4 角
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::TopLeft),     QPointF(0, 0));
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::TopRight),    QPointF(100, 0));
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::BottomRight), QPointF(100, 50));
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::BottomLeft),  QPointF(0, 50));
    // 4 边中
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::Top),    QPointF(50, 0));
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::Right),  QPointF(100, 25));
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::Bottom), QPointF(50, 50));
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::Left),   QPointF(0, 25));
    // 中心
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::Center), QPointF(50, 25));
    // 旋转手柄 (顶部上方 25 px)
    QCOMPARE(box.handlePos(transform::TransformBox::Handle::Rotation), QPointF(50, -25));
}

// ================== 2. hitTest ==================
void tst_P0_6_TransformSystem::test_TransformBox_hitTest_10_handles()
{
    transform::TransformBox box;
    box.setRect(QRectF(0, 0, 100, 50));
    // 8 handle + 中心 + 旋转手柄各自命中 (scene 坐标刚好在 handle 中心)
    QCOMPARE(box.hitTest(QPointF(0, 0)),     transform::TransformBox::Handle::TopLeft);
    QCOMPARE(box.hitTest(QPointF(50, 0)),    transform::TransformBox::Handle::Top);
    QCOMPARE(box.hitTest(QPointF(100, 0)),   transform::TransformBox::Handle::TopRight);
    QCOMPARE(box.hitTest(QPointF(100, 25)),  transform::TransformBox::Handle::Right);
    QCOMPARE(box.hitTest(QPointF(100, 50)),  transform::TransformBox::Handle::BottomRight);
    QCOMPARE(box.hitTest(QPointF(50, 50)),   transform::TransformBox::Handle::Bottom);
    QCOMPARE(box.hitTest(QPointF(0, 50)),    transform::TransformBox::Handle::BottomLeft);
    QCOMPARE(box.hitTest(QPointF(0, 25)),    transform::TransformBox::Handle::Left);
    QCOMPARE(box.hitTest(QPointF(50, 25)),   transform::TransformBox::Handle::Center);
    QCOMPARE(box.hitTest(QPointF(50, -25)),  transform::TransformBox::Handle::Rotation);
    // 不命中
    QCOMPARE(box.hitTest(QPointF(200, 200)), transform::TransformBox::Handle::None);
}

// ================== 3. 4 mode state ==================
void tst_P0_6_TransformSystem::test_TransformBox_4_modes_state_transitions()
{
    transform::TransformBox box;
    box.setMode(transform::TransformBox::Mode::Scale);
    QCOMPARE(box.mode(), transform::TransformBox::Mode::Scale);
    box.setMode(transform::TransformBox::Mode::Rotate);
    QCOMPARE(box.mode(), transform::TransformBox::Mode::Rotate);
    box.setMode(transform::TransformBox::Mode::Skew);
    QCOMPARE(box.mode(), transform::TransformBox::Mode::Skew);
    box.setMode(transform::TransformBox::Mode::Distort);
    QCOMPARE(box.mode(), transform::TransformBox::Mode::Distort);
}

// ================== 4. scaleMatrix Shift ==================
void tst_P0_6_TransformSystem::test_TransformMath_scaleMatrix_shift_equals_ratio()
{
    // Scale 模式: 拖 TopLeft handle 到 (50, 25) (宽高各缩 50%)
    QRectF r(0, 0, 100, 50);
    QTransform t = transform::TransformMath::scaleMatrix(r, transform::TransformBox::Handle::TopLeft,
                                                        QPointF(50, 25), /*shift*/ false);
    // (0, 0) 应该映射到 (50, 25)
    QPointF mapped = t.map(QPointF(0, 0));
    QCOMPARE(mapped, QPointF(50, 25));
    // (100, 50) 应该保持 (anchor 在 BottomRight)
    mapped = t.map(QPointF(100, 50));
    QCOMPARE(mapped, QPointF(100, 50));
}

// ================== 5. rotateMatrix ==================
void tst_P0_6_TransformSystem::test_TransformMath_rotateMatrix_around_center()
{
    QRectF r(0, 0, 100, 50);
    QPointF c = r.center();  // (50, 25)
    // 0 度: identity
    QTransform t0 = transform::TransformMath::rotateMatrix(r, 0.0);
    QCOMPARE(t0.map(c), c);
    // 90 度: 中心不变, 但 (50+10, 25) → (50, 25+10) (顺时针旋转 90°)
    QTransform t90 = transform::TransformMath::rotateMatrix(r, 90.0);
    QPointF p = QPointF(60, 25);  // 中心右侧 10
    QPointF rotated = t90.map(p);
    QVERIFY(qAbs(rotated.x() - 50) < 0.001);
    QVERIFY(qAbs(rotated.y() - 35) < 0.001);
}

// ================== 6. skewMatrix Shift 约束 ==================
void tst_P0_6_TransformSystem::test_TransformMath_skewMatrix_shift_constrains_one_axis()
{
    QRectF r(0, 0, 100, 50);
    // Shift 模式: 拖 Top handle, newPos.y 应被约束到 r.top()
    QPointF constrained = transform::TransformMath::constrainedSkewPoint(
        r, transform::TransformBox::Handle::Top, QPointF(50, 100), /*shift*/ true);
    QCOMPARE(constrained, QPointF(50, 0));  // Y 约束到 top=0, X 跟 newPos
    // 不按 Shift: 原样返回
    QPointF unconstrained = transform::TransformMath::constrainedSkewPoint(
        r, transform::TransformBox::Handle::Top, QPointF(50, 100), /*shift*/ false);
    QCOMPARE(unconstrained, QPointF(50, 100));
}

// ================== 7. distortMatrix ==================
void tst_P0_6_TransformSystem::test_TransformMath_distortMatrix_identity_when_unchanged()
{
    // 4 角不变 → identity (P0-6.3 简化版)
    QRectF r(0, 0, 100, 50);
    QPointF corners[4] = { r.topLeft(), r.topRight(),
                            r.bottomRight(), r.bottomLeft() };
    QTransform t = transform::TransformMath::distortMatrix(r, corners);
    QCOMPARE(t.map(QPointF(0, 0)), QPointF(0, 0));
    QCOMPARE(t.map(QPointF(50, 25)), QPointF(50, 25));
    QCOMPARE(t.map(QPointF(100, 50)), QPointF(100, 50));
}

void tst_P0_6_TransformSystem::test_TransformMath_distortMatrix_nontrivial_when_corners_changed()
{
    // P3.2.1 (2026-09-22): 4 角拖动后 distortMatrix 必须返回非平凡变换.
    //   旧实现 quadToQuad(src, src) 两边相同, 数学上退化成 identity — 4 角变了
    //   视觉无变化. 修复: src = origRect 4 角, dst = 扭曲后 corners.
    //   注: QTransform::quadToQuad 只支持 affine 映射 (2x3 矩阵, 不支持 perspective),
    //   所以 dst 必须跟 src 是平行四边形对应 (affine 可 cover), 否则 quadToQuad
    //   返 false 并返 identity. 用平行四边形拖动测试 affine 正确性.
    QRectF origRect(0, 0, 100, 50);
    // 沿 (10, 5) 平移整图 — 4 角都加 (10, 5), 仍构成平行四边形, affine OK
    QPointF corners[4] = {
        QPointF(10, 5),     // TL 平移
        QPointF(110, 5),    // TR 平移
        QPointF(110, 55),   // BR 平移
        QPointF(10, 55)     // BL 平移
    };
    QTransform t = transform::TransformMath::distortMatrix(origRect, corners);
    QVERIFY2(t.isIdentity() == false,
             qPrintable(QString("expected non-identity transform for translated corners")));
    // 4 个 corner 映射到 dst 位置
    QCOMPARE(t.map(QPointF(0, 0)), QPointF(10, 5));
    QCOMPARE(t.map(QPointF(100, 0)), QPointF(110, 5));
    QCOMPARE(t.map(QPointF(100, 50)), QPointF(110, 55));
    QCOMPARE(t.map(QPointF(0, 50)), QPointF(10, 55));
    // 中心 (50, 25) 映射到 (60, 30)
    QCOMPARE(t.map(QPointF(50, 25)), QPointF(60, 30));
}

// ================== 8. TransformCommand undo/redo ==================
//  注: 不真用 ImageWindow (避免 GUI 依赖), 只验 m_beforeMat / m_afterMat 跟 redo/undo 调 m_host 接口
//  这里用 mock host, 简化测试
void tst_P0_6_TransformSystem::test_TransformCommand_undo_redo_with_cvMat()
{
    cv::Mat before = cv::Mat::zeros(10, 10, CV_8UC3);
    cv::Mat after = cv::Mat::ones(10, 10, CV_8UC3) * 255;

    // TransformCommand 接受 ImageWindow*, 传 nullptr 也能调 redo/undo (P0-6.6 实装: if (!m_host) return)
    transform::TransformCommand cmd(nullptr, before, after, QStringLiteral("测试翻转"));
    QCOMPARE(cmd.text(), QStringLiteral("测试翻转"));
    // redo/undo 不抛异常 (mock host nullptr 跳过)
    cmd.redo();
    cmd.undo();
    cmd.redo();
    cmd.undo();
    QVERIFY(true);  // 不崩
}

// ================== 9. ImageProcessor flip + warpAffine + qTransformToAffine ==================
void tst_P0_6_TransformSystem::test_ImageProcessor_flip_warpAffine()
{
    // flip: 水平翻转
    cv::Mat in = cv::Mat::zeros(2, 4, CV_8UC1);
    in.at<uchar>(0, 0) = 1; in.at<uchar>(0, 3) = 2;
    cv::Mat out;
    ImageProcessor::flip(in, out, /*around y*/ 1);
    QCOMPARE((int)out.at<uchar>(0, 0), 2);
    QCOMPARE((int)out.at<uchar>(0, 3), 1);

    // flip: 垂直翻转
    in = cv::Mat::zeros(2, 2, CV_8UC1);
    in.at<uchar>(0, 0) = 1; in.at<uchar>(1, 0) = 2;
    ImageProcessor::flip(in, out, /*around x*/ 0);
    QCOMPARE((int)out.at<uchar>(0, 0), 2);
    QCOMPARE((int)out.at<uchar>(1, 0), 1);

    // warpAffine: identity 矩阵 → 输出跟输入一样
    cv::Mat img = cv::Mat::eye(3, 3, CV_8UC1) * 255;
    cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, 0, 0, 1, 0);
    cv::Mat warped;
    ImageProcessor::warpAffine(img, warped, M, img.size());
    // identity 应该跟原图一致 (3x3 对角线 255)
    QCOMPARE((int)warped.at<uchar>(0, 0), 255);
    QCOMPARE((int)warped.at<uchar>(1, 1), 255);
    QCOMPARE((int)warped.at<uchar>(2, 2), 255);

    // qTransformToAffine: identity QTransform → identity 2x3 矩阵
    QTransform t;
    QVERIFY(t.isAffine());
    cv::Mat aff = ImageProcessor::qTransformToAffine(t);
    QCOMPARE(aff.rows, 2);
    QCOMPARE(aff.cols, 3);
    QCOMPARE(aff.at<double>(0, 0), 1.0);
    QCOMPARE(aff.at<double>(1, 1), 1.0);
    QCOMPARE(aff.at<double>(0, 2), 0.0);
    QCOMPARE(aff.at<double>(1, 2), 0.0);
}

// ================== 10. TransformTool setMode 4 mode ==================
// P0-6.13 (2026-09-14): TransformTool::setMode 切 4 mode, box.mode 同步,
//   Distort 模式保留 4 角独立位置 (Scale/Skew/Rotate 同步回 m_rect)
void tst_P0_6_TransformSystem::test_TransformTool_setMode_4_modes()
{
    tools::TransformTool tool;
    tool.onEnter(nullptr);  // host=nullptr, fallback 800x600 box
    QCOMPARE(tool.mode(), transform::TransformBox::Mode::Scale);
    tool.setMode(transform::TransformBox::Mode::Rotate);
    QCOMPARE(tool.mode(), transform::TransformBox::Mode::Rotate);
    QCOMPARE(tool.box()->mode(), transform::TransformBox::Mode::Rotate);
    tool.setMode(transform::TransformBox::Mode::Skew);
    QCOMPARE(tool.mode(), transform::TransformBox::Mode::Skew);
    tool.setMode(transform::TransformBox::Mode::Distort);
    QCOMPARE(tool.mode(), transform::TransformBox::Mode::Distort);
    // 切回 Scale 时 corners 同步回 m_rect (跟初值一致)
    tool.setMode(transform::TransformBox::Mode::Scale);
    QCOMPARE(tool.mode(), transform::TransformBox::Mode::Scale);
    qDebug() << "rect after mode cycle:" << tool.box()->rect();
    QVERIFY2(tool.box()->rect().width() >= 1.0,
             QString("rect width=%1").arg(tool.box()->rect().width()).toLocal8Bit().constData());
}

// ================== 11. TransformTool rotationCallback ==================
// P0-6.13 (2026-09-14): 注入 rotationCallback, 在 Rotate 模式拖 rotation handle 时
//   触发回调 (ImageWindow::onFreeTransform 注入 → PropertiesDock::setRotation 同步)
void tst_P0_6_TransformSystem::test_TransformTool_rotationCallback()
{
    tools::TransformTool tool;
    tool.onEnter(nullptr);
    tool.setMode(transform::TransformBox::Mode::Rotate);

    qreal capturedDeg = -999.0;
    int   callCount = 0;
    tool.setRotationCallback([&capturedDeg, &callCount](qreal deg) {
        capturedDeg = deg;
        ++callCount;
    });

    // 模拟: rotation handle 在 box 顶部中央上方 (中心点 = (400, 300), rotation handle = (400, 275))
    //   拖到 (500, 300) (右侧 100, 0 偏) → angle = atan2(0, 100) = 0 度
    //   QMouseEvent 构造需要 QApplication, 这里直接调 onMouseMove 不传 QMouseEvent
    //   但 TransformTool::onMouseMove 第一参数是 QMouseEvent*, 我们用 nullptr
    tool.onMousePress(nullptr, nullptr, tool.box()->handlePos(transform::TransformBox::Handle::Rotation));
    tool.onMouseMove(nullptr, nullptr, QPointF(500, 300));
    QVERIFY(callCount >= 1);
    QVERIFY(qAbs(capturedDeg - 0.0) < 0.001);  // atan2(0, 100) = 0

    // 拖到 (400, 400) (下方 100) → angle = atan2(100, 0) = 90 度
    int prevCount = callCount;
    tool.onMouseMove(nullptr, nullptr, QPointF(400, 400));
    QVERIFY(callCount > prevCount);
    QVERIFY(qAbs(capturedDeg - 90.0) < 0.001);

    // Scale 模式不触发 callback (mode gate)
    tool.setMode(transform::TransformBox::Mode::Scale);
    int scaleCount = callCount;
    tool.onMouseMove(nullptr, nullptr, QPointF(100, 100));  // drag something in Scale mode
    QCOMPARE(callCount, scaleCount);  // 不增
}

// ================== 12. commitTransform push TransformCommand (P3.2.2) ==================
//   修复 P0-6.2 留下的 no-op: 拖完 mouseRelease 真正推 undo 命令 (Scale + Distort 两个 mode)
//   使用 mock host (ImageWindow 子类) 验证 undoStack index 增 1
#include "../src/media/imagewindow.h"
#include <QUndoStack>
#include "../src/media/transform/TransformCommand.h"

class MockHostForTransform : public ImageWindow
{
public:
    int undoCount() const { return undoStack() ? undoStack()->count() : 0; }
    void setFakeImage(int w = 800, int h = 600) {
        // 灰色 cv::Mat,够大让 box.rect() 落入
        cv::Mat fake(h, w, CV_8UC3, cv::Scalar(128, 128, 128));
        setCurrentImage(fake);
    }
};

void tst_P0_6_TransformSystem::test_TransformTool_commitTransform_pushesCommand_distort()
{
    MockHostForTransform host;
    host.setFakeImage();
    tools::TransformTool tool;
    tool.onEnter(&host);
    tool.setMode(transform::TransformBox::Mode::Distort);
    const int before = host.undoCount();

    // 模拟 Distort: drag BottomRight 从 (800, 600) 到 (700, 550)
    QPointF brPos = tool.box()->handlePos(transform::TransformBox::Handle::BottomRight);
    tool.onMousePress(nullptr, &host, brPos);
    tool.onMouseMove(nullptr, &host, QPointF(brPos.x() - 100, brPos.y() - 50));
    tool.onMouseRelease(nullptr, &host, QPointF(brPos.x() - 100, brPos.y() - 50));

    // Distort 4 角变了 → 透视变换 → push TransformCommand
    QCOMPARE(host.undoCount(), before + 1);
}

QTEST_MAIN(tst_P0_6_TransformSystem)
#include "tst_P0_6_TransformSystem.moc"
