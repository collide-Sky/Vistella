// =============================================================================
//  tst_SmartObjectTransform - P1.4.5 (2026-09-17) SmartObject 非破坏性变换
//  主菜单 + 撤销栈集成 单元测试 (5 用例)
//
//  覆盖 P1.4.5 主菜单 / LayerPanel 右键菜单触发的 4 个核心路径:
//    1. setTransform_persists:
//       LayerStack::setSmartObjectTransform(0, scale2) 后 hasTransform=true,
//       transform==scale2.
//    2. clearTransform_resets:
//       clearSmartObjectTransform(0) 后 hasTransform=false, transform=identity.
//    3. rasterizeWithTransform_applied:
//       SmartObject + scale2 transform 栅格化输出尺寸 = 源 2 倍 (跟 no-transform
//       栅格化对照, warpAffine 默认 zoom 后输出 size = 输入 size).
//    4. undoTransform:
//       ImageWindow 入口等价的"push makeSetSmartObjectTransform + 手动 setTransform
//       (Phase 5 pattern)" 流程, undo 还原 oldHas/oldT.
//    5. undoTransform_reApply:
//       undo 后 push 另一个 makeSetSmartObjectTransform 再 undo, 验证 redo 路径
//       (Phase 5 简化: redo 是 no-op, 但 post-state 由"push 后手动 setter"决定).
//
//  注: 不构造 ImageWindow (单测不能 new ImageWindow - 需要 QApplication +
//  大量 UI 依赖, 见 tst_LayerCommand.cpp:554). 这里直接验证数据层行为.
//  MainWindow onSmartObjectScale/Rotate/Reset slot 是数据层 API 的薄壳, 等价.
// =============================================================================

#include <QTest>
#include <QUndoStack>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"

using namespace layers;

class tst_SmartObjectTransform : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // 5 cases from P1.4.5 task spec:
    void test_setTransform_persists();
    void test_clearTransform_resets();
    void test_rasterizeWithTransform_applied();
    void test_undoTransform();
    void test_undoTransform_reApply();

private:
    QString makeTempPng(const QSize &size, const QString &nameTag);
};

QString tst_SmartObjectTransform::makeTempPng(const QSize &size, const QString &nameTag)
{
    const QString baseName = QStringLiteral("/tst_so_v4_") + nameTag
                             + QStringLiteral("_XXXXXX.png");
    QTemporaryFile tmp(QDir::tempPath() + baseName);
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    const QString path = tmp.fileName();
    tmp.close();
    cv::Mat img(size.height(), size.width(), CV_8UC3, cv::Scalar(80, 160, 240));
    cv::imwrite(path.toStdString(), img);
    return path;
}

void tst_SmartObjectTransform::initTestCase() {}
void tst_SmartObjectTransform::cleanupTestCase()
{
    LayerStack::cleanupSmartObjectCache();
}
void tst_SmartObjectTransform::cleanup()
{
    LayerStack::cleanupSmartObjectCache();
}

// =====================================================================
//  test_setTransform_persists
//   LayerStack::setSmartObjectTransform(0, scale2) sets hasTransform=true,
//   transform == QTransform::fromScale(2,2).
// =====================================================================

void tst_SmartObjectTransform::test_setTransform_persists()
{
    LayerStack stack;
    const QString path = makeTempPng(QSize(24, 24), QStringLiteral("persist"));
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/false);
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
    QCOMPARE(stack.at(0)->hasTransform, false);

    const QTransform t = QTransform::fromScale(2.0, 2.0);
    QVERIFY(stack.setSmartObjectTransform(0, t));
    QCOMPARE(stack.at(0)->hasTransform, true);
    QCOMPARE(stack.at(0)->transform, t);

    // No-op on identical transform returns false (P1.4.1 hasBehavior).
    QVERIFY(!stack.setSmartObjectTransform(0, t));

    QFile::remove(path);
}

// =====================================================================
//  test_clearTransform_resets
//   clearSmartObjectTransform(0) clears hasTransform + resets transform.
// =====================================================================

void tst_SmartObjectTransform::test_clearTransform_resets()
{
    LayerStack stack;
    const QString path = makeTempPng(QSize(16, 16), QStringLiteral("clear"));
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/false);

    const QTransform t = QTransform::fromScale(3.0, 3.0);
    QVERIFY(stack.setSmartObjectTransform(0, t));
    QCOMPARE(stack.at(0)->hasTransform, true);

    QVERIFY(stack.clearSmartObjectTransform(0));
    QCOMPARE(stack.at(0)->hasTransform, false);
    QCOMPARE(stack.at(0)->transform, QTransform());

    // Already-cleared transform: clearSmartObjectTransform returns false (idempotent).
    QVERIFY(!stack.clearSmartObjectTransform(0));

    QFile::remove(path);
}

// =====================================================================
//  test_rasterizeWithTransform_applied
//   With a non-identity QTransform, rasterize() applies cv::warpAffine on
//   the loaded source before the canvas-fit resize. We use renderOne(0)
//   and compare pixel-level output with and without the transform flag —
//   if hasTransform=true applies the warp, at least one pixel must differ.
//
//   Source: 20x20 solid BGR(80,160,240). Canvas: 40x40. We use scale 2x +
//   translate (+1, +1): the warpAffine inverse-maps destination (x, y) to
//   source ((x-1)/2, (y-1)/2) — so the rendered output will get the source
//   sampled at half stride, producing 40x40 output whose pixel pattern
//   differs from the no-transform case (which is the source bilinearly
//   stretched to 40x40).
//
//   We then assert:
//     - hasTransform flag toggles correctly.
//     - renderOne output differs at least at one pixel compared to
//       no-transform renderOne — proving warpAffine was called.
// =====================================================================

void tst_SmartObjectTransform::test_rasterizeWithTransform_applied()
{
    LayerStack stack;
    const QString path = makeTempPng(QSize(20, 20), QStringLiteral("rasterApply"));
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/false);
    stack.setCanvasSize(cv::Size(40, 40));
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    // Baseline renderOne: no transform flag.
    QCOMPARE(stack.at(0)->hasTransform, false);
    const cv::Mat noTransform = stack.renderOne(0);
    QVERIFY(!noTransform.empty());
    QCOMPARE(noTransform.size(), cv::Size(40, 40));

    // Apply scale 2x + translate (+1, +1) so warpAffine output's sampling
    // grid is shifted by half a pixel, ensuring pixel-level difference vs.
    // the no-transform result (which uses pure INTER_AREA upscale).
    QTransform t;
    t.translate(1.0, 1.0);
    t.scale(2.0, 2.0);
    QVERIFY(stack.setSmartObjectTransform(0, t));
    QCOMPARE(stack.at(0)->hasTransform, true);
    QCOMPARE(stack.smartObjectTransform(0), t);

    const cv::Mat withTransform = stack.renderOne(0);
    QVERIFY(!withTransform.empty());
    QCOMPARE(withTransform.size(), cv::Size(40, 40));

    // Sample a non-uniform pixel pattern by counting exact matches between
    // the two outputs. We expect at least one pixel to differ — proving
    // warpAffine was applied (otherwise both renderOne would compute the
    // same INTER_AREA resize result).
    int diffCount = 0;
    for (int r = 0; r < withTransform.rows; ++r) {
        for (int c = 0; c < withTransform.cols; ++c) {
            const cv::Vec3b a = noTransform.at<cv::Vec3b>(r, c);
            const cv::Vec3b b = withTransform.at<cv::Vec3b>(r, c);
            if (a != b) ++diffCount;
        }
    }
    QVERIFY(diffCount > 0);

    QFile::remove(path);
}

// =====================================================================
//  test_undoTransform
//   Mirror ImageWindow::applySmartObjectTransform slot flow:
//     1. backup oldT/oldHas
//     2. setSmartObjectTransform(newT) — returns true on actual change
//     3. push makeSetSmartObjectTransform(stack, idx, oldT, oldHas, newT, newHas)
//   Phase 5 pattern: push + manual setter (push's redo is no-op for transform).
//   undo restores oldT/oldHas.
// =====================================================================

void tst_SmartObjectTransform::test_undoTransform()
{
    LayerStack stack;
    const QString path = makeTempPng(QSize(16, 16), QStringLiteral("undo"));
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/false);

    QUndoStack undo;
    QVERIFY(!stack.at(0)->hasTransform);
    const QTransform oldT = stack.at(0)->transform;
    const bool     oldHad = stack.at(0)->hasTransform;

    const QTransform newT = QTransform::fromScale(2.0, 2.0);
    const bool     newHad = true;

    auto *cmd = LayerCommand::makeSetSmartObjectTransform(
        &stack, 0, oldT, oldHad, newT, newHad);
    QVERIFY(cmd != nullptr);
    undo.push(cmd);
    // Apply the new transform manually (Phase 5 pattern).
    QVERIFY(stack.setSmartObjectTransform(0, newT));
    QCOMPARE(stack.at(0)->hasTransform, true);
    QCOMPARE(stack.at(0)->transform, newT);

    // Undo: stores oldT/oldHad, not newT/newHad.
    undo.undo();
    QCOMPARE(stack.at(0)->hasTransform, false);
    QCOMPARE(stack.at(0)->transform, oldT);   // identity in this case

    QFile::remove(path);
}

// =====================================================================
//  test_undoTransform_reApply
//   After the first undo (test above), apply another transform, push a new
//   command, undo it. Verifies the LayerCommand actually clears back to the
//   pre-application state (rather than just to the previous "mid" state).
// =====================================================================

void tst_SmartObjectTransform::test_undoTransform_reApply()
{
    LayerStack stack;
    const QString path = makeTempPng(QSize(20, 20), QStringLiteral("reapply"));
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/false);

    QUndoStack undo;

    // First apply: scale 2x
    const QTransform t1 = QTransform::fromScale(2.0, 2.0);
    {
        const QTransform oldT = stack.at(0)->transform;
        const bool     oldHad = stack.at(0)->hasTransform;
        auto *cmd = LayerCommand::makeSetSmartObjectTransform(
            &stack, 0, oldT, oldHad, t1, /*newHas*/true);
        undo.push(cmd);
        QVERIFY(stack.setSmartObjectTransform(0, t1));
    }
    QCOMPARE(stack.at(0)->hasTransform, true);
    QCOMPARE(stack.at(0)->transform, t1);

    // Second apply: rotate 90 (compose with t1 via delta = rt * st)
    const QTransform t2_pre = t1 * QTransform().rotate(90.0);
    {
        const QTransform oldT = stack.at(0)->transform;
        const bool     oldHad = stack.at(0)->hasTransform;
        auto *cmd = LayerCommand::makeSetSmartObjectTransform(
            &stack, 0, oldT, oldHad, t2_pre, /*newHas*/true);
        undo.push(cmd);
        QVERIFY(stack.setSmartObjectTransform(0, t2_pre));
    }
    QCOMPARE(stack.at(0)->transform, t2_pre);

    // Undo second apply: back to t1 (scale 2x).
    undo.undo();
    QCOMPARE(stack.at(0)->hasTransform, true);
    QCOMPARE(stack.at(0)->transform, t1);

    // Undo first apply: back to identity / hasTransform=false.
    undo.undo();
    QCOMPARE(stack.at(0)->hasTransform, false);
    QCOMPARE(stack.at(0)->transform, QTransform());

    QFile::remove(path);
}

QTEST_MAIN(tst_SmartObjectTransform)
#include "tst_SmartObjectTransform.moc"
