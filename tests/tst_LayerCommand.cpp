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
    // ---- 调整图层: LUT 由渲染管线套用, 底图像素不被改写 ----
    void test_adjustmentLayer_isNonDestructive();
    // ---- undo / redo 语义回归 ----
    void test_undo_emits_layerChanged();
    void test_redo_restores_new_value();
    void test_remove_redo_same_layer();
    // ---- 工具编辑写图层 (04-02) ----
    void test_activeLayerIndex();
    void test_toolEdit_survivesLayerOps();
    void test_setLayerImage_undoRedo();



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
    // 调用方先加图层, 再 push command; 撤销删掉它, 重做放回原位
    LayerStack stack;
    QUndoStack undoStack;
    stack.addLayer(QStringLiteral("X"), makeMat());
    QCOMPARE(stack.count(), 1);

    auto *cmd = new LayerCommand(&stack, LayerCommand::Add, Layer(QStringLiteral("X"), makeMat()));
    undoStack.push(cmd);
    QCOMPARE(stack.count(), 1);   // 首个 redo 不重复添加

    undoStack.undo();
    QCOMPARE(stack.count(), 0);   // 撤销删掉这一层

    undoStack.redo();
    QCOMPARE(stack.count(), 1);   // 重做把它放回来
    QCOMPARE(stack.at(0)->name, QStringLiteral("X"));
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
    // undo 还原移动, redo 重新执行移动
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeMoveUp(&stack, 0));   // A 上移
    QCOMPARE(stack.at(0)->name, QStringLiteral("B"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
    undoStack.undo();                                       // 还原 → A,B
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("B"));
    undoStack.redo();                                       // 重做 → B,A
    QCOMPARE(stack.at(0)->name, QStringLiteral("B"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
}
void tst_LayerCommand::test_opacity()
{
    // 调用约定: 先改图层, 再 push command (command 构造时读图层上的新值)
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    QUndoStack undoStack;
    QVERIFY(stack.setOpacity(0, 0.5f));
    auto *cmd = new LayerCommand(&stack, LayerCommand::Opacity, 0, 1.0f);  // 存 old=1.0
    undoStack.push(cmd);
    // push 触发的首个 redo 不能把新值改回旧值
    QCOMPARE(stack.at(0)->opacity, 0.5f);
}
void tst_LayerCommand::test_opacityUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    QVERIFY(stack.setOpacity(0, 0.5f));
    auto *cmd = new LayerCommand(&stack, LayerCommand::Opacity, 0, 1.0f);  // 存 old=1.0
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->opacity, 0.5f);

    // undo 还原旧值
    undoStack.undo();
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    // redo 写回新值 (不是空操作)
    undoStack.redo();
    QCOMPARE(stack.at(0)->opacity, 0.5f);
}
void tst_LayerCommand::test_visible()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QVERIFY(stack.at(0)->visible);

    QUndoStack undoStack;
    QVERIFY(stack.setVisible(0, false));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Visible, 0, true));
    QVERIFY(!stack.at(0)->visible);

    undoStack.undo();
    QVERIFY(stack.at(0)->visible);
}
void tst_LayerCommand::test_visibleUndo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    QVERIFY(stack.setVisible(0, false));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Visible, 0, true));

    undoStack.undo();
    QVERIFY(stack.at(0)->visible);

    undoStack.redo();
    QVERIFY(!stack.at(0)->visible);
}
void tst_LayerCommand::test_locked()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    QVERIFY(stack.setLocked(0, true));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Locked, 0, false));
    QVERIFY(stack.at(0)->locked);

    undoStack.undo();
    QVERIFY(!stack.at(0)->locked);

    undoStack.redo();
    QVERIFY(stack.at(0)->locked);
}
void tst_LayerCommand::test_linked()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QUndoStack undoStack;
    QVERIFY(stack.setLinked(0, true));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Linked, 0, false));
    QVERIFY(stack.at(0)->isLinked);

    undoStack.undo();
    QVERIFY(!stack.at(0)->isLinked);

    undoStack.redo();
    QVERIFY(stack.at(0)->isLinked);
}
void tst_LayerCommand::test_blend()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->blend, Layer::Normal);

    QUndoStack undoStack;
    QVERIFY(stack.setBlend(0, Layer::Multiply));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Blend, 0,
                                    static_cast<int>(Layer::Normal)));
    QCOMPARE(stack.at(0)->blend, Layer::Multiply);

    undoStack.undo();
    QCOMPARE(stack.at(0)->blend, Layer::Normal);

    undoStack.redo();
    QCOMPARE(stack.at(0)->blend, Layer::Multiply);
}
void tst_LayerCommand::test_rename()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Old"), makeMat());
    QUndoStack undoStack;
    QVERIFY(stack.rename(0, QStringLiteral("New")));
    auto *cmd = new LayerCommand(&stack, LayerCommand::Rename, 0, QStringLiteral("Old"));
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->name, QStringLiteral("New"));

    undoStack.undo();
    QCOMPARE(stack.at(0)->name, QStringLiteral("Old"));

    undoStack.redo();
    QCOMPARE(stack.at(0)->name, QStringLiteral("New"));
}
void tst_LayerCommand::test_visible_callerPreApply_bugReproducer()
{
    // 调用方先改图层再 push:
    //   oldVisible = l->visible;
    //   setVisible(newVisible);
    //   push(new LayerCommand(Visible, idx, oldVisible));
    // push 触发的首个 redo 不能把新值改回旧值, 之后的 redo 才写回新值.
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->visible, true);

    QUndoStack undoStack;
    const bool oldVisible = stack.at(0)->visible;             // true
    QVERIFY(stack.setVisible(0, false));                       // 调用方先应用
    QCOMPARE(stack.at(0)->visible, false);
    undoStack.push(new LayerCommand(
        &stack, LayerCommand::Visible, 0, oldVisible));       // m_boolVal = old
    QCOMPARE(stack.at(0)->visible, false);                     // 首个 redo 没改回去
    undoStack.undo();
    QCOMPARE(stack.at(0)->visible, true);
    undoStack.redo();
    QCOMPARE(stack.at(0)->visible, false);
}
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
    // Group via makeMergeIntoGroup.
    //   Factory 自己完成 mergeIntoGroup; undo 解散, redo 重新编组.
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

    // Redo -> 重新编成组
    undoStack.redo();
    QCOMPARE(stack.count(), 2);
    QCOMPARE(int(stack.at(stack.count() - 1)->kind), int(Layer::Group));
    const auto &kids2 = stack.groupChildrenOf(stack.idOf(stack.count() - 1));
    QCOMPARE(int(kids2.size()), 2);
    QCOMPARE(kids2[0]->name, QStringLiteral("B"));
    QCOMPARE(kids2[1]->name, QStringLiteral("C"));
}
void tst_LayerCommand::test_ungroup()
{
    // Ungroup via makeFlattenGroup.
    //   Factory 自己完成 flattenGroup; undo 重新立组, redo 再次解散.
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

    // Redo -> 再次解散
    undoStack.redo();
    QCOMPARE(stack.count(), 3);
    for (int i = 0; i < stack.count(); ++i) {
        QCOMPARE(int(stack.at(i)->kind), int(Layer::Bitmap));
    }
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
    QVERIFY(stack.setText(0, QStringLiteral("new")));
    auto *cmd = LayerCommand::makeSetText(&stack, 0, QStringLiteral("old"));
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));

    undoStack.redo();
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));
}
void tst_LayerCommand::test_setTextUndo()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("old"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    QVERIFY(stack.setText(0, QStringLiteral("new")));
    auto *cmd = LayerCommand::makeSetText(&stack, 0, QStringLiteral("old"));
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));
    undoStack.undo();
    QCOMPARE(stack.at(0)->text, QStringLiteral("old"));
    undoStack.redo();
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));
}
void tst_LayerCommand::test_setTextFont()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("x"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    QVERIFY(stack.setTextFont(0, QStringLiteral("Times"), 48, Qt::yellow));
    auto *cmd = LayerCommand::makeSetTextFont(&stack, 0,
                                              QStringLiteral("Arial"), 24, QColor(Qt::white));
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Times"));
    QCOMPARE(stack.at(0)->fontSize, 48);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::yellow));
    undoStack.undo();
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Arial"));
    QCOMPARE(stack.at(0)->fontSize, 24);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::white));
    undoStack.redo();
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Times"));
    QCOMPARE(stack.at(0)->fontSize, 48);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::yellow));
}
void tst_LayerCommand::test_setTextFontUndo()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("x"), 24, Qt::white, QStringLiteral("Arial"));
    QUndoStack undoStack;
    QVERIFY(stack.setTextFont(0, QStringLiteral("Consolas"), 32, Qt::red));
    auto *cmd = LayerCommand::makeSetTextFont(&stack, 0,
                                              QStringLiteral("Arial"), 24, QColor(Qt::white));
    undoStack.push(cmd);
    undoStack.undo();
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Arial"));
    QCOMPARE(stack.at(0)->fontSize, 24);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::white));
    undoStack.redo();
    QCOMPARE(stack.at(0)->fontFamily, QStringLiteral("Consolas"));
    QCOMPARE(stack.at(0)->fontSize, 32);
    QCOMPARE(stack.at(0)->textColor, QColor(Qt::red));
}
void tst_LayerCommand::test_setSmartObject()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/old/path.png"), false);
    QUndoStack undoStack;
    QVERIFY(stack.setSmartObjectSource(0, QStringLiteral("/new/path.png"), true));
    auto *cmd = LayerCommand::makeSetSmartObject(&stack, 0, QStringLiteral("/old/path.png"), false);
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/new/path.png"));
    QVERIFY(stack.at(0)->sourceEmbedded);
    undoStack.undo();
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/old/path.png"));
    QVERIFY(!stack.at(0)->sourceEmbedded);
    undoStack.redo();
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/new/path.png"));
    QVERIFY(stack.at(0)->sourceEmbedded);
}
void tst_LayerCommand::test_setSmartObjectUndo()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/p.png"), false);
    QUndoStack undoStack;
    QVERIFY(stack.setSmartObjectSource(0, QStringLiteral("/q.png"), true));
    auto *cmd = LayerCommand::makeSetSmartObject(&stack, 0, QStringLiteral("/p.png"), false);
    undoStack.push(cmd);
    undoStack.undo();
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/p.png"));
    QVERIFY(!stack.at(0)->sourceEmbedded);
    undoStack.redo();
    QCOMPARE(stack.at(0)->sourceFilePath, QStringLiteral("/q.png"));
    QVERIFY(stack.at(0)->sourceEmbedded);
}
void tst_LayerCommand::test_setAdjustmentType()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    QUndoStack undoStack;
    QVERIFY(stack.setAdjustmentType(0, QStringLiteral("levels")));
    auto *cmd = LayerCommand::makeSetAdjustmentType(&stack, 0, QStringLiteral("curves"));
    undoStack.push(cmd);
    QCOMPARE(stack.at(0)->adjustmentType, QStringLiteral("levels"));
    undoStack.undo();
    QCOMPARE(stack.at(0)->adjustmentType, QStringLiteral("curves"));
    undoStack.redo();
    QCOMPARE(stack.at(0)->adjustmentType, QStringLiteral("levels"));
}
void tst_LayerCommand::test_setAdjustmentLut()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    auto oldLut = stack.at(0)->adjustmentLut.clone();
    // new LUT: invert
    cv::Mat newLut(256, 1, CV_8U);
    uchar *p = newLut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(255 - i);
    QVERIFY(stack.setAdjustmentLut(0, newLut));

    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldLut);
    undoStack.push(cmd);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(0)), 255);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(255)), 0);
    undoStack.undo();
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(0)), 0);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(255)), 255);
    undoStack.redo();
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(0)), 255);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(255)), 0);
}
void tst_LayerCommand::test_setAdjustmentLutUndo()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    auto oldLut = stack.at(0)->adjustmentLut.clone();
    cv::Mat newLut(256, 1, CV_8U);
    uchar *p = newLut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = 128;
    QVERIFY(stack.setAdjustmentLut(0, newLut));

    QUndoStack undoStack;
    auto *cmd = LayerCommand::makeSetAdjustmentLut(&stack, 0, oldLut);
    undoStack.push(cmd);
    // 现在是 all-128
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(50)), 128);
    undoStack.undo();
    // 还原 identity
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(50)), 50);
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(200)), 200);
    undoStack.redo();
    QCOMPARE(static_cast<int>(stack.at(0)->adjustmentLut.at<uchar>(50)), 128);
}
//  调整图层 (LayerKind::Adjustment) 的 LUT 由渲染管线在合成时套用.
//  底图像素永远不被改写 —— 连续拖滑块不会在已调整的结果上再调整一次,
//  之前的画笔/马赛克涂抹也不会被调整覆盖.
void tst_LayerCommand::test_adjustmentLayer_isNonDestructive()
{
    LayerStack stack;
    cv::Mat base(4, 4, CV_8UC3, cv::Scalar(10, 20, 30));
    stack.setBaseLayer(base);

    // 反相 LUT
    cv::Mat lut(256, 1, CV_8U);
    uchar *p = lut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(255 - i);

    const int adjIdx = stack.addAdjustmentLayer(QStringLiteral("Inv"),
                                                    QStringLiteral("curves"), lut);
    QVERIFY(adjIdx >= 0);
    QCOMPARE(static_cast<int>(stack.at(adjIdx)->kind),
             static_cast<int>(layers::Layer::Adjustment));

    // 底图像素没有被改写
    QCOMPARE(static_cast<int>(stack.baseLayer()->image.at<cv::Vec3b>(0, 0)[0]), 10);

    // 渲染结果套用了 LUT
    const cv::Mat rendered = stack.render();
    QVERIFY(!rendered.empty());
    QCOMPARE(static_cast<int>(rendered.at<cv::Vec3b>(0, 0)[0]), 255 - 10);

    // 撤销命令恢复旧 LUT, 渲染结果跟着回到未调整状态
    auto oldLut = stack.at(adjIdx)->adjustmentLut.clone();
    cv::Mat identity(256, 1, CV_8U);
    uchar *ip = identity.ptr<uchar>();
    for (int i = 0; i < 256; ++i) ip[i] = static_cast<uchar>(i);
    QVERIFY(stack.setAdjustmentLut(adjIdx, identity));
    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeSetAdjustmentLut(&stack, adjIdx, oldLut));
    QCOMPARE(static_cast<int>(stack.at(adjIdx)->adjustmentLut.at<uchar>(10)), 10);
    undoStack.undo();
    QCOMPARE(static_cast<int>(stack.at(adjIdx)->adjustmentLut.at<uchar>(10)), 245);
    const cv::Mat rendered2 = stack.render();
    QVERIFY(!rendered2.empty());
    QCOMPARE(static_cast<int>(rendered2.at<cv::Vec3b>(0, 0)[0]), 255 - 10);
}

// =====================================================================
//  undo / redo 语义回归
// =====================================================================

//  撤销/重做必须发 layerChanged.
//    没有这个信号, 图层面板和画布不知道图层内容变了, 用户看到的是旧帧.
void tst_LayerCommand::test_undo_emits_layerChanged()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QSignalSpy spy(&stack, &LayerStack::layerChanged);

    QUndoStack undoStack;
    QVERIFY(stack.setOpacity(0, 0.5f));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Opacity, 0, 1.0f));
    spy.clear();

    undoStack.undo();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    spy.clear();
    undoStack.redo();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(stack.at(0)->opacity, 0.5f);
}

//  撤销 → 重做 必须回到"新值", 不能是空操作.
void tst_LayerCommand::test_redo_restores_new_value()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Old"), makeMat());
    QUndoStack undoStack;

    QVERIFY(stack.setOpacity(0, 0.25f));
    QVERIFY(stack.setVisible(0, false));
    QVERIFY(stack.setBlend(0, Layer::Multiply));
    QVERIFY(stack.rename(0, QStringLiteral("New")));

    undoStack.push(new LayerCommand(&stack, LayerCommand::Opacity, 0, 1.0f));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Visible, 0, true));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Blend, 0,
                                    static_cast<int>(Layer::Normal)));
    undoStack.push(new LayerCommand(&stack, LayerCommand::Rename, 0,
                                    QStringLiteral("Old")));

    undoStack.undo();   // rename
    undoStack.undo();   // blend
    undoStack.undo();   // visible
    undoStack.undo();   // opacity
    QCOMPARE(stack.at(0)->name, QStringLiteral("Old"));
    QCOMPARE(stack.at(0)->blend, Layer::Normal);
    QVERIFY(stack.at(0)->visible);
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    undoStack.redo();   // rename
    undoStack.redo();   // blend
    undoStack.redo();   // visible
    undoStack.redo();   // opacity
    QCOMPARE(stack.at(0)->name, QStringLiteral("New"));
    QCOMPARE(stack.at(0)->blend, Layer::Multiply);
    QVERIFY(!stack.at(0)->visible);
    QCOMPARE(stack.at(0)->opacity, 0.25f);
}

//  删中间一层 → 撤销 → 重做: 删掉的必须是同一层, 而且要放回原来的位置.
void tst_LayerCommand::test_remove_redo_same_layer()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());

    QUndoStack undoStack;
    undoStack.push(LayerCommand::makeRemove(&stack, 1));
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("C"));

    undoStack.undo();
    QCOMPARE(stack.count(), 3);
    QCOMPARE(stack.at(1)->name, QStringLiteral("B"));

    undoStack.redo();
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("C"));
}

// =====================================================================
//  工具编辑写图层 (04-02)
// =====================================================================

//  活动图层的选取规则: 有选中用选中, 没选中取最上面的 Bitmap 图层.
void tst_LayerCommand::test_activeLayerIndex()
{
    LayerStack stack;
    QCOMPARE(stack.activeLayerIndex(), -1);       // 空栈没有活动图层

    stack.addLayer(QStringLiteral("Base"), makeMat(32, 32));
    QCOMPARE(stack.activeLayerIndex(), 0);        // 没选中 → 最上面的 Bitmap

    stack.addLayer(QStringLiteral("Top"), makeMat(32, 32));
    QCOMPARE(stack.activeLayerIndex(), 1);

    stack.setSelection(0);
    QCOMPARE(stack.activeLayerIndex(), 0);        // 有选中 → 用选中的

    stack.setSelection(1);
    QCOMPARE(stack.activeLayerIndex(), 1);

    cv::Mat *img = stack.activeLayerImage();
    QVERIFY(img != nullptr);
    QCOMPARE(img->cols, 32);
}

//  工具编辑写进图层: 改图层属性 / 切可见性之后, 笔画不能消失.
//    以前笔画只写合成缓存 m_current, 任何 invalidate 都会把它冲掉.
void tst_LayerCommand::test_toolEdit_survivesLayerOps()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeMat(32, 32));

    cv::Mat *target = stack.activeLayerImage();
    QVERIFY(target != nullptr);
    // 模拟画笔: 在图层像素上写一笔
    target->at<cv::Vec3b>(4, 4) = cv::Vec3b(0, 0, 255);

    // 切可见性 (以前会 invalidate 缓存, 把笔画冲掉)
    stack.setVisible(0, false);
    stack.setVisible(0, true);
    // 改属性
    stack.setOpacity(0, 0.5f);
    stack.setOpacity(0, 1.0f);

    // 重新合成: 笔画还在
    const cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(int(out.at<cv::Vec3b>(4, 4)[2]), 255);
}

//  图层像素编辑的撤销/重做: undo 写回编辑前, redo 写回编辑后.
void tst_LayerCommand::test_setLayerImage_undoRedo()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeMat(32, 32));
    QUndoStack undoStack;

    cv::Mat before = stack.at(0)->image.clone();
    cv::Mat after = before.clone();
    after.at<cv::Vec3b>(4, 4) = cv::Vec3b(0, 0, 255);
    // 调用方先应用新像素, 再 push 命令
    stack.at(0)->image = after.clone();

    undoStack.push(LayerCommand::makeSetLayerImage(
        &stack, 0, before, after, QStringLiteral("stroke")));
    QCOMPARE(int(stack.at(0)->image.at<cv::Vec3b>(4, 4)[2]), 255);

    undoStack.undo();
    QCOMPARE(int(stack.at(0)->image.at<cv::Vec3b>(4, 4)[2]), 200);   // 原色 R=200

    undoStack.redo();
    QCOMPARE(int(stack.at(0)->image.at<cv::Vec3b>(4, 4)[2]), 255);
}

QTEST_MAIN(tst_LayerCommand)
#include "tst_LayerCommand.moc"
