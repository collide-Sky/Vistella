// SPDX-License-Identifier: MIT
//
// tst_P0_8_System - P0-8.4 (2026-09-15)
//
// 10 test 覆盖 P0-8 历史/导出/色彩空间:
//   HistoryDock:
//     1. setUndoStack 后 rowCount = stack->count()
//     2. textAt(i) = stack->text(i) (PS 同款历史 label)
//     3. onStackIndexChanged -> rebuild, currentRow = index
//     4. clicked(item) -> stack->setIndex (跳转)
//   ExportDialog:
//     5. 默认 PNG, formatName() == "PNG"
//     6. JPEG quality slider -> jpegQuality()
//     7. PNG compression 0-9 -> pngCompression()
//     8. resize combo -> resizePercent()
//   ICC:
//     9. Profile::createSRgb() 非空, toByteArray() 非空, description 非空
//    10. Profile::load 不存在文件 -> 返回 null + err
//
#include <QTest>
#include <QGuiApplication>
#include <QTemporaryFile>
#include <QUndoStack>
#include <QUndoCommand>
#include <QListView>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>

#include "../src/media/docks/HistoryDock.h"
#include "../src/media/dialogs/ExportDialog.h"
#include "../src/media/icc/IccProfile.h"

class tst_P0_8_System : public QObject
{
    Q_OBJECT
private slots:
    void test_HistoryDock_rowCount_matches_stack();
    void test_HistoryDock_textAt_matches_stack_text();
    void test_HistoryDock_currentRow_syncs_with_indexChanged();
    void test_HistoryDock_clicked_item_jumps_stack_index();
    void test_ExportDialog_default_format_PNG();
    void test_ExportDialog_jpegQuality_via_slider();
    void test_ExportDialog_pngCompression_set();
    void test_ExportDialog_resizePercent_via_combo();
    void test_IccProfile_createSRgb_basic();
    void test_IccProfile_load_nonexistent_returns_null();
};

// ================== 1. rowCount ==================
void tst_P0_8_System::test_HistoryDock_rowCount_matches_stack()
{
    docks::HistoryDock dock;
    QCOMPARE(dock.rowCount(), 0);
    QUndoStack stack;
    stack.push(new QUndoCommand("op1"));
    stack.push(new QUndoCommand("op2"));
    stack.push(new QUndoCommand("op3"));
    dock.setUndoStack(&stack);
    QCOMPARE(dock.rowCount(), 3);
    dock.setUndoStack(nullptr);
    QCOMPARE(dock.rowCount(), 0);
}

// ================== 2. textAt ==================
void tst_P0_8_System::test_HistoryDock_textAt_matches_stack_text()
{
    docks::HistoryDock dock;
    QUndoStack stack;
    stack.push(new QUndoCommand("添加文字"));
    stack.push(new QUndoCommand("自由变换"));
    stack.push(new QUndoCommand("滤镜: Blur"));
    dock.setUndoStack(&stack);
    QCOMPARE(dock.textAt(0), QStringLiteral("添加文字"));
    QCOMPARE(dock.textAt(1), QStringLiteral("自由变换"));
    QCOMPARE(dock.textAt(2), QStringLiteral("滤镜: Blur"));
}

// ================== 3. currentRow 跟 indexChanged 同步 ==================
void tst_P0_8_System::test_HistoryDock_currentRow_syncs_with_indexChanged()
{
    // QUndoStack::index 语义: count of done operations (push 后 index = count)
    //   push 3 次 -> index=3, count=3 (all done)
    //   dock.currentRow() 用 min(index, count-1) 避免越界
    //   undo 一次 -> index=2 (top 2 done, top 1 undone)
    //   undo 第二次 -> index=1
    //   redo 一次 -> index=2
    docks::HistoryDock dock;
    QUndoStack stack;
    stack.push(new QUndoCommand("op1"));   // index=1
    stack.push(new QUndoCommand("op2"));   // index=2
    stack.push(new QUndoCommand("op3"));   // index=3
    dock.setUndoStack(&stack);
    QCOMPARE(dock.currentRow(), 2);        // min(3, 2) = 2
    stack.undo();                            // index=2
    QCOMPARE(dock.currentRow(), 2);
    stack.undo();                            // index=1
    QCOMPARE(dock.currentRow(), 1);
    stack.redo();                            // index=2
    QCOMPARE(dock.currentRow(), 2);
}

// ================== 4. clicked -> setIndex ==================
void tst_P0_8_System::test_HistoryDock_clicked_item_jumps_stack_index()
{
    docks::HistoryDock dock;
    QUndoStack stack;
    stack.push(new QUndoCommand("op1"));
    stack.push(new QUndoCommand("op2"));
    stack.push(new QUndoCommand("op3"));
    dock.setUndoStack(&stack);

    // current = 3 (after 3 push)
    QCOMPARE(stack.index(), 3);

    // 点击 row=0 -> stack->setIndex(0) -> undo 3 次
    auto* listView = dock.findChild<QListView*>();
    QVERIFY(listView != nullptr);
    const QModelIndex idx0 = listView->model()->index(0, 0);
    QVERIFY(idx0.isValid());
    listView->activated(idx0);

    QCOMPARE(stack.index(), 0);  // clicked row 0 -> setIndex(0)
}

// ================== 5. ExportDialog 默认 PNG ==================
void tst_P0_8_System::test_ExportDialog_default_format_PNG()
{
    dialogs::ExportDialog dlg;
    QCOMPARE(dlg.formatName(), QStringLiteral("PNG"));
    QCOMPARE(static_cast<int>(dlg.options().format),
             static_cast<int>(dialogs::ExportDialog::Format::PNG));
}

// ================== 6. JPEG quality via slider ==================
void tst_P0_8_System::test_ExportDialog_jpegQuality_via_slider()
{
    dialogs::ExportDialog dlg;
    auto* slider = dlg.findChild<QSlider*>(QStringLiteral("jpegSlider"));
    QVERIFY(slider != nullptr);
    QVERIFY(slider->objectName() == QStringLiteral("jpegSlider"));
    // 切到 JPEG (idx=1)
    auto* combo = dlg.findChild<QComboBox*>();
    QVERIFY(combo != nullptr);
    combo->setCurrentIndex(1);
    QCOMPARE(dlg.formatName(), QStringLiteral("JPEG"));
    // 改 slider
    slider->setValue(75);
    QCOMPARE(dlg.jpegQuality(), 75);
    Q_UNUSED(combo);
}

// ================== 7. PNG compression ==================
void tst_P0_8_System::test_ExportDialog_pngCompression_set()
{
    dialogs::ExportDialog dlg;
    // 找 pngSpin 通过 directChildren + name 限制
    QSpinBox* pngSpin = dlg.findChild<QSpinBox*>(QStringLiteral("pngSpin"));
    if (pngSpin == nullptr) {
        // fallback: find first QSpinBox (jpegSpin 是默认 page 中的, pngSpin 在 PNG page)
        auto spins = dlg.findChildren<QSpinBox*>();
        QVERIFY(!spins.isEmpty());
        pngSpin = spins.first();
    }
    QVERIFY(pngSpin != nullptr);
    pngSpin->setValue(9);
    QCOMPARE(dlg.pngCompression(), 9);
    QCOMPARE(dlg.options().pngCompression, 9);
}

// ================== 8. resizePercent via combo ==================
void tst_P0_8_System::test_ExportDialog_resizePercent_via_combo()
{
    dialogs::ExportDialog dlg;
    QList<QComboBox*> combos = dlg.findChildren<QComboBox*>();
    QVERIFY(!combos.isEmpty());
    // resize combo 是最后 1 个 QComboBox (format 在前)
    QComboBox* resizeCombo = combos.last();
    QVERIFY(resizeCombo != nullptr);
    resizeCombo->setCurrentIndex(2);   // 50%
    QCOMPARE(dlg.resizePercent(), 50);
}

// ================== 9. ICC Profile sRGB ==================
void tst_P0_8_System::test_IccProfile_createSRgb_basic()
{
    auto p = media::icc::Profile::createSRgb();
    QVERIFY(!p.isNull());
    QVERIFY(!p->description().isEmpty());
    QVERIFY(!p->toByteArray().isEmpty());
    // sRGB profile 实际 size 由 LCMS2 决定 (一般 < 4KB)
    //   不要 hard-code 大小, 只要非空就行
    QVERIFY(p->toByteArray().size() > 0);
    QVERIFY(p->toByteArray().size() < 64 * 1024);
}

// ================== 10. ICC Profile load 不存在文件 ==================
void tst_P0_8_System::test_IccProfile_load_nonexistent_returns_null()
{
    QString err;
    auto p = media::icc::Profile::load(QStringLiteral("Z:/nonexistent.icc"), &err);
    QVERIFY(p.isNull());
    QVERIFY(!err.isEmpty());
}

QTEST_MAIN(tst_P0_8_System)
#include "tst_P0_8_System.moc"