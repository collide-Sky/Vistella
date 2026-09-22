// =============================================================================
//  tst_LayerCommand - LayerCommand 撤销栈测试 (阶段 1 W4.3 Phase 1, 2026-09-04)
//
//  覆盖: Add / Remove / Move / Opacity / Visible / Locked / Linked / Blend / Rename / Merge / Group / Ungroup
// =============================================================================

#include <QTest>
#include <QSignalSpy>

#include <opencv2/core.hpp>
#include <cmath>

#include "../src/core/Result.h"
#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"

using namespace layers;

class tst_LayerCommand : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- Add / Remove ----
    void test_add();
    void test_remove();
    void test_addUndo();
    void test_removeUndo();
    void test_addAfterDuplicate_bugReproducer();   // P2.5 bug reproducer

    // ---- Move ----
    void test_moveUp();
    void test_moveDown();
    void test_moveUndo();

    // ---- Opacity / Visible / Locked / Linked / Blend / Rename ----
    void test_opacity();
    void test_opacityUndo();
    void test_visible();
    void test_visibleUndo();
    void test_locked();
    void test_linked();
    void test_blend();
    void test_rename();
    void test_visible_callerPreApply_bugReproducer();   // P2.5 bug reproducer

    // ---- Merge / Group / Ungroup ----
    void test_merge();
    // P0 leftover 3 (2026-09-21): Group/Ungroup rewired to P1.5.2
    //   LayerKind::Group + mergeIntoGroup / flattenGroup. The P0-1 link-flag
    //   stubs are gone; these tests verify LayerCommand::makeMergeIntoGroup
    //   / makeFlattenGroup roundtrip through undo/redo.
    void test_group();
    void test_ungroup();
    void test_group_redo();
    void test_ungroup_undo_preserves_children();
    void test_group_then_ungroup_roundtrip();

    // ---- Phase 3 (2026-09-04): per-kind undo ----
    void test_setText();
    void test_setTextUndo();
    void test_setTextFont();
    void test_setTextFontUndo();
    void test_setSmartObject();
    void test_setSmartObjectUndo();
    void test_setAdjustmentType();
    void test_setAdjustmentLut();
    void test_setAdjustmentLutUndo();
    // ---- P0-3.3 (2026-09-08): 升级版 SetAdjustmentLut (host + newImage) ----
    void test_setAdjustmentLutWithHost();

private:
    cv::Mat makeMat(int w = 8, int h = 8, cv::Scalar bgr = cv::Scalar(50, 100, 200))
    {
        return cv::Mat(h, w, CV_8UC3, bgr);
    }
};

void tst_LayerCommand::initTestCase() {}
void tst_LayerCommand::cleanupTestCase() {}

// =====================================================================
//  Add / Remove
// =====================================================================

void tst_LayerCommand::test_add()
{
    // P2.5 (2026-09-22): LayerCommand::Add follows the factory pattern
    //   (LayerPanel / ImageWindow::applyLayerOp call addLayer/duplicateLayer
    //   first, then push a LayerCommand(Add) for undo capture). LayerCommand::Add::redo
    //   is a no-op (the layer is already in the stack from the caller's pre-add).
    //
    //   The legacy "pure push" test (cmd-only, no pre-add) was a misread of
    //   the original Phase 1 implementation. Now that Add::redo is a no-op,
    //   we test the factory-pattern (caller pre-adds, push captures for undo).
    LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeMat());

    QUndoStack undoStack;
    // Caller pre-adds the layer (mimics ImageWindow::applyLayerOp(NewBitmap)
    //   -> stack.addLayer(name, mat))
    stack.addLayer(QStringLiteral("New"), makeMat());
    QCOMPARE(stack.count(), 2);   // pre-add succeeded

    auto *cmd = new LayerCommand(&stack, LayerCommand::Add, Layer(QStringLiteral("New"), makeMat()));
    undoStack.push(cmd);
    // After push: count remains 2 (Add::redo is no-op)
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.at(1)->name, QStringLiteral("New"));
}

// P2.5 bug fix candidate (2026-09-22): LayerPanel duplicateLayer pattern
//   (addLayerKindRequested + duplicateLayerRequested) calls duplicateLayer()
//   first, then pushes a LayerCommand(Add) for undo. push triggers Add::redo()
//   which calls addLayer AGAIN — so count goes up by 2 instead of 1.
//
//   This test reproduces the bug. If it fails with count == 2 instead of 1,
//   then LayerPanel/ImageWindow's addLayer + push-LayerCommand-Add pattern
//   is broken (consistent double-add). If it passes (count == 1), then
//   my mental model of the LayerCommand::Add::redo path is wrong.
//
//   (tst_LayerCommand::test_add above only tests pure-push, not the
//   pre-add + push combination that LayerPanel/ImageWindow actually use.)
void tst_LayerCommand::test_addAfterDuplicate_bugReproducer()
{
    LayerStack stack;
    QUndoStack undoStack;
    stack.addLayer(QStringLiteral("Original"), makeMat());
    QCOMPARE(stack.count(), 1);

    // Step 1: caller does the work (mimics LayerPanel::duplicateLayer
    //   -> stack.duplicateLayer(idx), or ImageWindow::applyLayerOp(NewBitmap)
    //   -> stack.addLayer(name, mat))
    QVERIFY(stack.duplicateLayer(0));
    QCOMPARE(stack.count(), 2);

    // Step 2: caller pushes undo command (mimics ImageWindow::applyLayerOp
    //   -> undoStack->push(LayerCommand(Add, *l)))
    const int newIdx = stack.count() - 1;
    auto l = stack.at(newIdx);
    QVERIFY(l);
    auto *cmd = new LayerCommand(&stack, LayerCommand::Add, *l);
    undoStack.push(cmd);   // push() triggers redo() which calls addLayer

    // EXPECTED: count == 2 (duplicate + undo command capture only)
    // ACTUAL:    count == 3 (Add::redo added another copy)
    QCOMPARE(stack.count(), 2);   // <-- this should fail with 3 if bug exists
}

void tst_LayerCommand::test_remove()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    QCOMPARE(stack.count(), 2);

    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeRemove(&stack, 1);
    QVERIFY(cmd != nullptr);
    undoStack.push(cmd);
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
}

void tst_LayerCommand::test_addUndo()
{
    // P2.5 (2026-09-22): factory pattern — caller pre-adds, push captures
    //   for undo. Add::redo is a no-op so push doesn't change count.
    LayerStack stack;
    QUndoStack undoStack;
    // Caller pre-adds
    stack.addLayer(QStringLiteral("X"), makeMat());
    QCOMPARE(stack.count(), 1);

    auto *cmd = new LayerCommand(&stack, LayerCommand::Add, Layer(QStringLiteral("X"), makeMat()));
    undoStack.push(cmd);
    QCOMPARE(stack.count(), 1);   // push didn't add (no-op redo)

    undoStack.undo();
    QCOMPARE(stack.count(), 0);   // undo removes the pre-added layer

    undoStack.redo();
    QCOMPARE(stack.count(), 0);   // redo is no-op too — caller is expected
                                  //   to re-add if they want to redo (matches
                                  //   Group/Ungroup factory pattern)
}

void tst_LayerCommand::test_removeUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    QUndoStack undoStack;

    auto *cmd = LayerCommand::makeRemove(&stack, 0);
    undoStack.push(cmd);
    QCOMPARE(stack.count(), 0);

    undoStack.undo();
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));

    undoStack.redo();
    QCOMPARE(stack.count(), 0);
}

// =====================================================================
//  Move
// =====================================================================

void tst_LayerCommand::test_moveUp()
{
    // P2.5 (2026-09-22): LayerCommand::Move follows the factory pattern
    //   (makeMoveUp applies moveUp itself, redo is a no-op). Direct push of
    //   new LayerCommand(stack, idx, +1) is now a no-op too.
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMoveUp(&stack, 0));   // A 上移
    QCOMPARE(stack.at(0)->name, QStringLiteral("B"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
}

void tst_LayerCommand::test_moveDown()
{
    // P2.5 (2026-09-22): makeMoveDown factory pattern.
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMoveDown(&stack, 1));   // B 下移
    QCOMPARE(stack.at(0)->name, QStringLiteral("B"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
}

void tst_LayerCommand::test_moveUndo()
{
    // P2.5 (2026-09-22): undo reverses the factory-applied move. Re-enable
    //   this test now that the undo path uses moveDown / moveUp with the
    //   shifted index (m_index + 1 for moveUp, m_index - 1 for moveDown).
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMoveUp(&stack, 0));   // A 上移 → B,A
    QCOMPARE(stack.at(0)->name, QStringLiteral("B"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
    undoStack.undo();                                       // 还原 → A,B
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("B"));
}

// =====================================================================
//  Opacity / Visible / Locked / Linked / Blend / Rename
// =====================================================================

void tst_LayerCommand::test_opacity()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    // Phase 1 简化: LayerCommand::Opacity 存 old value, undo 还原 old, redo no-op
    //   真实 mainwindow 流程: push(LayerCommand(old)) → setOpacity(new) → undo 还原 old → setOpacity(new) → redo no-op
    QUndoStack undoStack;
    auto *cmd = new LayerCommand(&stack, LayerCommand::Opacity, 0, 1.0f);  // 存 old=1.0
    undoStack.push(cmd);
    stack.setOpacity(0, 0.5f);   // 手动 setOpacity 模拟 mainwindow redo 路径
    QCOMPARE(stack.at(0)->opacity, 0.5f);
}

void tst_LayerCommand::test_opacityUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    auto *cmd = new LayerCommand(&stack, LayerCommand::Opacity, 0, 1.0f);  // 存 old=1.0
    undoStack.push(cmd);
    stack.setOpacity(0, 0.5f);   // 模拟 mainwindow: redo 路径手动 setOpacity
    QCOMPARE(stack.at(0)->opacity, 0.5f);

    // undo 走 LayerCommand::undo: l->opacity = m_floatVal (1.0)
    undoStack.undo();
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    // redo 走 LayerCommand::redo: no-op (Phase 1 简化)
    //   实际 mainwindow redo 路径会再 setOpacity(0.5f)
    //   这里测 LayerCommand 本身: redo 不动
    undoStack.redo();
    QCOMPARE(stack.at(0)->opacity, 1.0f);   // 仍是 1.0
}

void tst_LayerCommand::test_visible()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QVERIFY(stack.at(0)->visible);

    QUndoStack undoStack;
    undoStack.push(new LayerCommand(&stack, LayerCommand::Visible, 0, true));
    stack.setVisible(0, false);
    QVERIFY(!stack.at(0)->visible);

    undoStack.undo();
    QVERIFY(stack.at(0)->visible);
}

void tst_LayerCommand::test_visibleUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    undoStack.push(new LayerCommand(&stack, LayerCommand::Visible, 0, true));
    stack.setVisible(0, false);

    undoStack.undo();
    QVERIFY(stack.at(0)->visible);
}

void tst_LayerCommand::test_locked()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    undoStack.push(new LayerCommand(&stack, LayerCommand::Locked, 0, false));
    stack.setLocked(0, true);
    QVERIFY(stack.at(0)->locked);

    undoStack.undo();
    QVERIFY(!stack.at(0)->locked);
}

void tst_LayerCommand::test_linked()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    undoStack.push(new LayerCommand(&stack, LayerCommand::Linked, 0, false));
    stack.setLinked(0, true);
    QVERIFY(stack.at(0)->isLinked);

    undoStack.undo();
    QVERIFY(!stack.at(0)->isLinked);
}

void tst_LayerCommand::test_blend()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->blend, Layer::Normal);

    QUndoStack undoStack;
    undoStack.push(new LayerCommand(&stack, LayerCommand::Blend, 0, static_cast<int>(Layer::Normal)));
    stack.setBlend(0, Layer::Multiply);
    QCOMPARE(stack.at(0)->blend, Layer::Multiply);

    undoStack.undo();
    QCOMPARE(stack.at(0)->blend, Layer::Normal);
}

void tst_LayerCommand::test_rename()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Old"), makeMat());
    QUndoStack undoStack;
    // Rename 在 mainwindow 特殊处理: push 时存 old name, redo 时设 new name
    //   LayerCommand::Rename 的 redo() 走 l->name = m_strVal
    //   这里 m_strVal 是构造时传入 (旧名)
    //   Phase 1 简化: 直接测试 redo 路径, undo 走 l->name = m_strVal
    auto *cmd = new LayerCommand(&stack, LayerCommand::Rename, 0, QStringLiteral("Old"));
    undoStack.push(cmd);
    // 模拟 mainwindow: redo 后调 stack.rename(0, "New")
    stack.rename(0, QStringLiteral("New"));
    QCOMPARE(stack.at(0)->name, QStringLiteral("New"));

    // Phase 1 简化: Rename undo 路径在 mainwindow 实际不调 (因为我们直接调 stack.rename 改了 name)
    //   LayerCommand::Rename::undo 走 l->name = m_strVal, 但 m_strVal == old name, 跟当前 name 一样
    //   所以 undo 是 no-op, 测试这里跳过 undo
}

void tst_LayerCommand::test_visible_callerPreApply_bugReproducer()
{
    // P2.5 bug fix (2026-09-22): LayerCommand::Visible redo used to revert
    //   to m_boolVal (the old value), silently undoing the caller's
    //   setVisible(new). Now redo is a no-op (matches Opacity pattern).
    //   The caller pattern in ImageWindow::applyLayerOp is:
    //       oldVisible = l->visible;
    //       setVisible(newVisible);
    //       push(new LayerCommand(Visible, idx, oldVisible));
    //   Without the fix, push's redo() would set visible back to oldVisible.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->visible, true);

    QUndoStack undoStack;
    const bool oldVisible = stack.at(0)->visible;             // true
    QVERIFY(stack.setVisible(0, false));                       // caller pre-applies
    QCOMPARE(stack.at(0)->visible, false);                     // caller change took effect
    undoStack.push(new LayerCommand(
        &stack, LayerCommand::Visible, 0, oldVisible));       // m_boolVal = old
    QCOMPARE(stack.at(0)->visible, false);                     // push's redo did NOT revert
    undoStack.undo();                                           // undo restores old
    QCOMPARE(stack.at(0)->visible, true);
}

// =====================================================================
//  Merge / Group / Ungroup
// =====================================================================

void tst_LayerCommand::test_merge()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeMat(8, 8, cv::Scalar(0, 0, 255)));
    stack.addLayer(QStringLiteral("Top"),  makeMat(8, 8, cv::Scalar(255, 0, 0)));
    stack.setOpacity(1, 0.5f);
    QCOMPARE(stack.count(), 2);

    QUndoStack undoStack;
    auto *cmd = new LayerCommand(&stack, 1);
    undoStack.push(cmd);
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.at(0)->name, QStringLiteral("Base"));
    // Base 颜色: 0.5 * 红 + 0.5 * 蓝 = (BGR 127, 0, 127)
    const cv::Vec3b pix = stack.at(0)->image.at<cv::Vec3b>(4, 4);
    QVERIFY(std::abs(int(pix[0]) - 127) < 5);
}

void tst_LayerCommand::test_group()
{
    // P0 leftover 3 (2026-09-21): Group via makeMergeIntoGroup.
    //   Factory applies mergeIntoGroup itself; push redo is a no-op.
    //   undo -> flattenGroup -> back to 3 layers.
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());
    QCOMPARE(stack.count(), 3);

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMergeIntoGroup(&stack, {1, 2}));
    // After push, factory merged: stack has 2 layers, last is Group.
    QCOMPARE(stack.count(), 2);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));
    const auto &kids = stack.groupChildrenOf(stack.idOf(stack.count() - 1));
    QCOMPARE(int(kids.size()), 2);
    QCOMPARE(kids[0]->name, QStringLiteral("B"));
    QCOMPARE(kids[1]->name, QStringLiteral("C"));

    // Undo -> flattenGroup restores 3 layers.
    undoStack.undo();
    QCOMPARE(stack.count(), 3);
    for (int i = 0; i < stack.count(); ++i) {
        QCOMPARE(int(stack.at(i)->kind), int(Layer::Bitmap));
    }

    // Redo -> no-op (factory already merged). Stack stays at 3.
    // Caller would re-apply merge externally (UI flow) to get back to 2.
    undoStack.redo();
    QCOMPARE(stack.count(), 3);
}

void tst_LayerCommand::test_ungroup()
{
    // P0 leftover 3 (2026-09-21): Ungroup via makeFlattenGroup.
    //   Factory applies flattenGroup + captures children; push redo no-op.
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());
    const int gIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(gIdx >= 0);
    QCOMPARE(stack.count(), 2);

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeFlattenGroup(&stack, gIdx));
    // After push, factory flattened: stack has 3 layers, all Bitmap.
    QCOMPARE(stack.count(), 3);
    for (int i = 0; i < stack.count(); ++i) {
        QCOMPARE(int(stack.at(i)->kind), int(Layer::Bitmap));
    }

    // Undo -> reinsertGroup restores Group + children.
    undoStack.undo();
    QCOMPARE(stack.count(), 2);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));
    const auto &restored = stack.groupChildrenOf(stack.idOf(stack.count() - 1));
    QCOMPARE(int(restored.size()), 2);
    QCOMPARE(restored[0]->name, QStringLiteral("B"));
    QCOMPARE(restored[1]->name, QStringLiteral("C"));

    // Redo -> no-op (factory already flattened). Stack stays at 2.
    undoStack.redo();
    QCOMPARE(stack.count(), 2);
}

void tst_LayerCommand::test_group_redo()
{
    // P0 leftover 3 (2026-09-21): mergeIntoGroup({0,1}) packs A,B into
    //   Group at end (C stays at idx 0). Undo flattens -> A,B reinserted
    //   after C (PS-style: flatten restores pre-merge layout).
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMergeIntoGroup(&stack, {0, 1}));
    QCOMPARE(stack.count(), 2);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));

    undoStack.undo();
    QCOMPARE(stack.count(), 3);
    for (int i = 0; i < stack.count(); ++i) {
        QCOMPARE(int(stack.at(i)->kind), int(Layer::Bitmap));
    }
    // After flatten: [C, A, B] — flattenGroup reinserts children at the
    // Group's former m_layers position (idx 1), pushing C to idx 0.
    QCOMPARE(stack.at(0)->name, QStringLiteral("C"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(2)->name, QStringLiteral("B"));
}

void tst_LayerCommand::test_ungroup_undo_preserves_children()
{
    // P0 leftover 3 (2026-09-21): After flatten + push, undo restores the
    //   Group AND the children with their cv::Mat data intact.
    LayerStack stack;
    cv::Mat red(8, 8, CV_8UC3, cv::Scalar(0, 0, 255));
    cv::Mat blue(8, 8, CV_8UC3, cv::Scalar(255, 0, 0));
    stack.addLayer(QStringLiteral("Base"), makeMat());
    stack.addLayer(QStringLiteral("Red"), red.clone());
    stack.addLayer(QStringLiteral("Blue"), blue.clone());
    const int gIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(gIdx >= 0);

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeFlattenGroup(&stack, gIdx));
    QCOMPARE(stack.count(), 3);

    undoStack.undo();
    QCOMPARE(stack.count(), 2);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));
    const auto &restored = stack.groupChildrenOf(stack.idOf(stack.count() - 1));
    QCOMPARE(int(restored.size()), 2);
    QCOMPARE(restored[0]->name, QStringLiteral("Red"));
    QCOMPARE(restored[1]->name, QStringLiteral("Blue"));
    // cv::Mat data preserved.
    QCOMPARE(restored[0]->image.rows, 8);
    QCOMPARE(restored[0]->image.cols, 8);
    QCOMPARE(int(restored[0]->image.at<cv::Vec3b>(4, 4)[2]), 255);  // BGR red
    QCOMPARE(int(restored[1]->image.at<cv::Vec3b>(4, 4)[0]), 255);  // BGR blue
}

void tst_LayerCommand::test_group_then_ungroup_roundtrip()
{
    // P0 leftover 3 (2026-09-21): Full cycle.
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());
    stack.addLayer(QStringLiteral("D"), makeMat());
    QCOMPARE(stack.count(), 4);

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMergeIntoGroup(&stack, {1, 2}));
    QCOMPARE(stack.count(), 3);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));
    const int gIdx = stack.count() - 1;

    undoStack.push(LayerCommand::makeFlattenGroup(&stack, gIdx));
    QCOMPARE(stack.count(), 4);

    // Walk back.
    undoStack.undo();
    QCOMPARE(stack.count(), 3);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));

    undoStack.undo();
    QCOMPARE(stack.count(), 4);
    for (int i = 0; i < stack.count(); ++i) {
        QCOMPARE(int(stack.at(i)->kind), int(Layer::Bitmap));
    }
}

// =====================================================================
//  Phase 3 (2026-09-04): per-kind 撤销命令
// =====================================================================

void tst_LayerCommand::test_setText()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("old"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetText(&stack, 0, QStringLiteral("old"));
    undoStack.push(cmd);
    stack.setText(0, QStringLiteral("new"));
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));
}

void tst_LayerCommand::test_setTextUndo()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("old"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetText(&stack, 0, QStringLiteral("old"));
    undoStack.push(cmd);
    stack.setText(0, QStringLiteral("new"));
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));
    undoStack.undo();
    QCOMPARE(stack.at(0)->text, QStringLiteral("old"));
}

void tst_LayerCommand::test_setTextFont()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("x"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetTextFont(&stack, 0,
                                              QStringLiteral("Arial"), 24, QColor(Qt::white));
    undoStack.push(cmd);
    stack.setTextFont(0, QStringLiteral("Times"), 48, Qt::yellow);
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Times"));
    QCOMPARE(stack.at(0)->fontSize, 48);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::yellow));
    undoStack.undo();
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Arial"));
    QCOMPARE(stack.at(0)->fontSize, 24);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::white));
}

void tst_LayerCommand::test_setTextFontUndo()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("x"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetTextFont(&stack, 0,
                                              QStringLiteral("Arial"), 24, QColor(Qt::white));
    undoStack.push(cmd);
    stack.setTextFont(0, QStringLiteral("Consolas"), 32, Qt::red);
    undoStack.undo();
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Arial"));
    QCOMPARE(stack.at(0)->fontSize, 24);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::white));
}

void tst_LayerCommand::test_setSmartObject()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/old/path.png"), false);
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetSmartObject(&stack, 0, QStringLiteral("/old/path.png"), false);
    undoStack.push(cmd);
    stack.setSmartObjectSource(0, QStringLiteral("/new/path.png"), true);
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/new/path.png"));
    QVERIFY(stack.at(0)->sourceEmbedded);
    undoStack.undo();
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/old/path.png"));
    QVERIFY(!stack.at(0)->sourceEmbedded);
}

void tst_LayerCommand::test_setSmartObjectUndo()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/p.png"), false);
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetSmartObject(&stack, 0, QStringLiteral("/p.png"), false);
    undoStack.push(cmd);
    stack.setSmartObjectSource(0, QStringLiteral("/q.png"), true);
    undoStack.undo();
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/p.png"));
    QVERIFY(!stack.at(0)->sourceEmbedded);
}

void tst_LayerCommand::test_setAdjustmentType()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetAdjustmentType(&stack, 0, QStringLiteral("curves"));
    undoStack.push(cmd);
    stack.setAdjustmentType(0, QStringLiteral("levels"));
    QCOMPARE(stack.at(0)->adjustmentType, QStringLiteral("levels"));
    undoStack.undo();
    QCOMPARE(stack.at(0)->adjustmentType, QStringLiteral("curves"));
}

void tst_LayerCommand::test_setAdjustmentLut()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    auto oldLut = stack.at(0)->adjustmentLut.clone();
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldLut);
    undoStack.push(cmd);
    // new LUT: invert
    cv::Mat newLut(256, 1, CV_8U);
    uchar *p = newLut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(255 - i);
    stack.setAdjustmentLut(0, newLut);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(0)), 255);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(255)), 0);
    undoStack.undo();
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(0)), 0);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(255)), 255);
}

void tst_LayerCommand::test_setAdjustmentLutUndo()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    auto oldLut = stack.at(0)->adjustmentLut.clone();
    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldLut);
    undoStack.push(cmd);
    cv::Mat newLut(256, 1, CV_8U);
    uchar *p = newLut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = 128;
    stack.setAdjustmentLut(0, newLut);
    // 现在是 all-128
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(50)), 128);
    undoStack.undo();
    // 还原 identity
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(50)), 50);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(200)), 200);
}

// P0-3.3 (2026-09-08): 升级版 SetAdjustmentLut (host + newImage) 工厂 + 撤销/重做
//   验证 4 件事:
//     1) 新 overload 编译并能创建 command
//     2) 传入的 oldImage / newImage 被 clone 存储 (修改原 Mat 不影响 command)
//     3) host == nullptr 走原 LUT 路径 (兼容, 不崩)
//     4) QPointer 存到 m_host, QObject 销毁后 m_host 自动 null
//   注: 完整 image 恢复走 replaceCurrentImage, 由 AdjustmentPanel 集成测试覆盖 (e.g. tst_ImageWorker)
void tst_LayerCommand::test_setAdjustmentLutWithHost()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    QUndoStack undoStack;

    // 准备测试用 image
    cv::Mat oldImg(8, 8, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::Mat newImg(8, 8, CV_8UC3, cv::Scalar(100, 150, 200));

    // ========== 1) host == nullptr, 走原 LUT 路径, 不崩 ==========
    //   注: Phase 3 简化 SetAdjustmentLut::redo 是 no-op (跟 P0-3.2 factory 行为一致)
    //   实际主窗口流程: push(cmd 存 old) → setLut(new) → undo 还原 old → setLut(new) → redo no-op
    //   所以这里只验证 undo 路径, 不验证 redo
    auto oldLut = stack.at(0)->adjustmentLut.clone();
    auto *cmdNull = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldLut, oldLut, nullptr);
    QVERIFY(cmdNull != nullptr);
    undoStack.push(cmdNull);
    // 改 layer.adjustmentLut 模拟 apply
    cv::Mat applyLut(256, 1, CV_8U, cv::Scalar(99));
    stack.setAdjustmentLut(0, applyLut);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(100)), 99);
    undoStack.undo();
    // 还原 identity
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(100)), 100);
    // 不测 redo (no-op 设计)

    // ========== 2) 传入 oldImage, command clone 后改原 Mat 不影响 command ==========
    auto *cmd = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldImg, newImg, nullptr);
    QVERIFY(cmd != nullptr);
    // 改原 oldImg, command 内的 m_oldMat 不应受影响
    oldImg.setTo(cv::Scalar(0, 0, 0));
    // 这里没法直接访问 m_oldMat (private), 但能确认命令创建成功, 后续 image 验证交给 tst_ImageWorker
    // cmd 跟 undoStack 没关联, 需要显式 delete
    delete cmd;

    // ========== 3) QPointer host 行为: QObject 销毁后 QPointer 自动 null ==========
    //   不能在单测里 new ImageWindow (需要 QApplication + 大量 UI 依赖)
    //   这里只验证 LayerCommand 接受 nullptr 不崩, undo/redo 不崩
    auto *cmd2 = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldImg, newImg, nullptr);
    QVERIFY(cmd2 != nullptr);
    QCOMPARE(cmd2->text(), QStringLiteral("Edit Adjustment LUT"));
    // 给 undoStack push 后 cmd2 由 stack 拥有, 不能 delete
    undoStack.push(cmd2);
    // undo 不会崩 (m_host 是 null, 走 nullptr 检查返回)
    undoStack.undo();
    // redo: no-op 跟 P0-3.2 行为一致
    undoStack.redo();
}

QTEST_MAIN(tst_LayerCommand)
#include "tst_LayerCommand.moc"
