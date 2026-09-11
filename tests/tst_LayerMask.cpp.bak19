// =============================================================================
//  tst_LayerMask - 阶段 1 W4.4 Phase 4 蒙版单元测试 (2026-09-04)
//
//  覆盖:
//    - addMask / clearMask / enableMask
//    - applyMask helper (mask 0/255 像素置黑/保留)
//    - render() 集成 mask (masked 像素被涂黑)
//    - renderOne() 应用 mask
//    - LayerCommand 蒙版 Op 撤销 (AddMask/ClearMask/EnableMask)
// =============================================================================

#include <QTest>
#include <QUndoStack>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"

using namespace layers;

class tst_LayerMask : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- Setter ----
    void test_addMask();
    void test_clearMask();
    void test_enableMask();
    void test_enableMask_noMask();

    // ---- Render ----
    void test_render_mask_hidesArea();
    void test_render_mask_disabled_keepsAll();
    void test_renderOne_appliesMask();
    void test_render_mask_resize();

    // ---- LayerCommand 撤销 ----
    void test_addMaskUndo();
    void test_clearMaskUndo();
    void test_enableMaskUndo();

private:
    cv::Mat makeMat(int w = 32, int h = 32, cv::Scalar bgr = cv::Scalar(255, 255, 255))
    {
        return cv::Mat(h, w, CV_8UC3, bgr);
    }
    // 造一个 mask: 左半 255 (保留), 右半 0 (涂黑)
    cv::Mat makeHalfMask(int w = 32, int h = 32)
    {
        cv::Mat m(h, w, CV_8U, cv::Scalar(0));
        m.colRange(0, w / 2) = 255;
        return m;
    }
};

void tst_LayerMask::initTestCase() {}
void tst_LayerMask::cleanupTestCase() {}

// =====================================================================
//  Setter
// =====================================================================

void tst_LayerMask::test_addMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    cv::Mat mask = makeHalfMask();
    QVERIFY(stack.addMask(0, mask));
    QCOMPARE(stack.at(0)->maskEnabled, true);
    QVERIFY(!stack.at(0)->layerMask.empty());
    QCOMPARE(stack.at(0)->layerMask.size(), cv::Size(32, 32));
    // 越界
    QVERIFY(!stack.addMask(99, mask));
    // 空 mask
    QVERIFY(!stack.addMask(0, cv::Mat()));
}

void tst_LayerMask::test_clearMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addMask(0, makeHalfMask());
    QVERIFY(stack.clearMask(0));
    QVERIFY(stack.at(0)->layerMask.empty());
    QCOMPARE(stack.at(0)->maskEnabled, false);
    // 二次 clear: 返 false (没改)
    QVERIFY(!stack.clearMask(0));
    // 无蒙版时 clear: 返 false
    stack.addLayer(QStringLiteral("L2"), makeMat());
    QVERIFY(!stack.clearMask(1));
}

void tst_LayerMask::test_enableMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addMask(0, makeHalfMask());
    // 已 enabled, 改 false
    QVERIFY(stack.enableMask(0, false));
    QCOMPARE(stack.at(0)->maskEnabled, false);
    // 再 false, 返 false
    QVERIFY(!stack.enableMask(0, false));
    // 改 true
    QVERIFY(stack.enableMask(0, true));
    QCOMPARE(stack.at(0)->maskEnabled, true);
}

void tst_LayerMask::test_enableMask_noMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    // 没蒙版, enable 返 false
    QVERIFY(!stack.enableMask(0, true));
}

// =====================================================================
//  Render
// =====================================================================

void tst_LayerMask::test_render_mask_hidesArea()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));  // 黑底
    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32, cv::Scalar(255, 255, 255)));  // 白顶层
    // 加半透 mask
    stack.addMask(1, makeHalfMask(32, 32));
    // 默认 opacity 1.0, mask 默认 enabled
    cv::Mat out = stack.render();
    QCOMPARE(out.size(), cv::Size(32, 32));
    // 左半 (mask=255) 应保留白色 (255,255,255)
    const cv::Vec3b leftPx = out.at<cv::Vec3b>(16, 4);
    QCOMPARE(static_cast<int>(leftPx[0]), 255);
    QCOMPARE(static_cast<int>(leftPx[1]), 255);
    QCOMPARE(static_cast<int>(leftPx[2]), 255);
    // 右半 (mask=0) 应变黑色 (跟 base 一样)
    const cv::Vec3b rightPx = out.at<cv::Vec3b>(16, 28);
    QCOMPARE(static_cast<int>(rightPx[0]), 0);
    QCOMPARE(static_cast<int>(rightPx[1]), 0);
    QCOMPARE(static_cast<int>(rightPx[2]), 0);
}

void tst_LayerMask::test_render_mask_disabled_keepsAll()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));
    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    stack.addMask(1, makeHalfMask(32, 32));
    stack.enableMask(1, false);
    cv::Mat out = stack.render();
    // 全白 (mask 不应用)
    const cv::Vec3b leftPx = out.at<cv::Vec3b>(16, 4);
    const cv::Vec3b rightPx = out.at<cv::Vec3b>(16, 28);
    QCOMPARE(static_cast<int>(leftPx[0]), 255);
    QCOMPARE(static_cast<int>(rightPx[0]), 255);
}

void tst_LayerMask::test_renderOne_appliesMask()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    stack.addMask(0, makeHalfMask(32, 32));
    cv::Mat out = stack.renderOne(0);
    QCOMPARE(out.size(), cv::Size(32, 32));
    // 左半 (255) 保留, 右半 (0) 涂黑
    const cv::Vec3b leftPx = out.at<cv::Vec3b>(16, 4);
    const cv::Vec3b rightPx = out.at<cv::Vec3b>(16, 28);
    QCOMPARE(static_cast<int>(leftPx[0]), 255);
    QCOMPARE(static_cast<int>(rightPx[0]), 0);
}

void tst_LayerMask::test_render_mask_resize()
{
    // mask 16x16, layer 32x32 — 应 resize mask 到 layer 尺寸
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));
    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32, cv::Scalar(255, 255, 255)));
    cv::Mat smallMask(16, 16, CV_8U, cv::Scalar(255));
    stack.addMask(1, smallMask);
    cv::Mat out = stack.render();
    // mask 全 255 → 全白 (跟没 mask 一样)
    const cv::Vec3b px = out.at<cv::Vec3b>(16, 16);
    QCOMPARE(static_cast<int>(px[0]), 255);
}

// =====================================================================
//  LayerCommand 撤销
// =====================================================================

void tst_LayerMask::test_addMaskUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undo;
    auto oldMask = stack.at(0)->layerMask.clone();
    bool oldEnabled = stack.at(0)->maskEnabled;
    auto *cmd = LayerCommand::makeAddMask(&stack, 0, oldMask, oldEnabled);
    undo.push(cmd);
    // push 即 redo — no-op, mainwindow 会调 addMask
    cv::Mat newMask = makeHalfMask();
    stack.addMask(0, newMask);
    QVERIFY(stack.at(0)->maskEnabled);
    QVERIFY(!stack.at(0)->layerMask.empty());
    // undo: 还原
    undo.undo();
    QVERIFY(stack.at(0)->layerMask.empty());
    QCOMPARE(stack.at(0)->maskEnabled, false);
}

void tst_LayerMask::test_clearMaskUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addMask(0, makeHalfMask());
    QUndoStack undo;
    auto oldMask = stack.at(0)->layerMask.clone();
    bool oldEnabled = stack.at(0)->maskEnabled;
    auto *cmd = LayerCommand::makeClearMask(&stack, 0, oldMask, oldEnabled);
    undo.push(cmd);
    stack.clearMask(0);
    QVERIFY(stack.at(0)->layerMask.empty());
    undo.undo();
    QVERIFY(!stack.at(0)->layerMask.empty());
    QCOMPARE(stack.at(0)->maskEnabled, true);
}

void tst_LayerMask::test_enableMaskUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    stack.addMask(0, makeHalfMask());
    QUndoStack undo;
    auto *cmd = LayerCommand::makeEnableMask(&stack, 0, true);  // 旧 enabled=true
    undo.push(cmd);
    stack.enableMask(0, false);
    QCOMPARE(stack.at(0)->maskEnabled, false);
    undo.undo();
    QCOMPARE(stack.at(0)->maskEnabled, true);
}

QTEST_MAIN(tst_LayerMask)
#include "tst_LayerMask.moc"
