// =============================================================================
//  tst_LayerIdentity - M1.2 (2026-10-27) 图层稳定身份 + 编组按选区
//
//  覆盖审计 §9 M1 第 2 条:
//    - LayerId 改造 (LYR-06 / LYR-12 / LYR-16)
//    - m_groups 改 id key
//    - 编组按钮按选区 (LYR-05)
//
//  用例:
//    1. stableId 入栈即分配, 且互不相同
//    2. stableId 跟着撤销还原走 (删掉再撤销, 拿回同一个身份)
//    3. duplicateLayer 拿到的是**新**身份
//    4. Remove 命令的 redo 按 id 定位, 不会删错层 (LYR-06)
//    5. Add 命令的 undo 按 id 定位, 不会删错层 (LYR-06)
//    6. clear() 把编组子层一起清掉 (LYR-12)
//    7. 嵌套编组时内组的子层不丢
//    8. 智能滤镜链按 id: 删掉下层的图层, 链还指着滤镜 (LYR-16)
//    9. 智能滤镜的父层记的是 id 不是下标 (LYR-16)
//   10. 底图被编进组里以后画布不退化成 800x600 (LYR-05)
//   11. 编组选区不吞底图 (LYR-05)
//   12. 图层面板的编组按钮按选区发信号 (LYR-05)
// =============================================================================

#include <QTest>
#include <QUndoStack>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolButton>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"
#include "../src/media/imageworker/layers/LayerPanel.h"

using namespace layers;

namespace {

cv::Mat solid(int w, int h, cv::Scalar c)
{
    return cv::Mat(h, w, CV_8UC3, c);
}

}   // namespace

class tst_LayerIdentity : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- 稳定身份 ----
    void test_stableId_assignedAndUnique();
    void test_stableId_survivesUndoRestore();
    void test_duplicateLayer_getsNewId();

    // ---- LYR-06: 撤销命令按 id 定位 ----
    void test_removeCommand_redoTargetsSameLayer();
    void test_addCommand_undoRemovesCorrectLayer();

    // ---- LYR-12: 编组子层的生命周期 ----
    void test_clear_dropsGroupChildren();
    void test_nestedGroup_keepsInnerChildren();

    // ---- LYR-16: 智能滤镜链按 id ----
    void test_smartFilterChain_survivesLayerDeletion();
    void test_smartFilter_parentIsId();

    // ---- LYR-05: 编组按选区 + 文档级画布 ----
    void test_canvasSize_survivesBaseGroupedAway();
    void test_groupingSelection_keepsBaseOnTop();
    void test_panel_groupButton_usesSelection();

private:
    QString makeTempPng(int w, int h);
};

void tst_LayerIdentity::initTestCase() {}

void tst_LayerIdentity::cleanupTestCase()
{
    LayerStack::cleanupSmartObjectCache();
}

QString tst_LayerIdentity::makeTempPng(int w, int h)
{
    QTemporaryFile tmp(QDir::tempPath()
                       + QStringLiteral("/tst_layeridentity_XXXXXX.png"));
    tmp.setAutoRemove(false);
    if (!tmp.open()) return QString();
    const QString path = tmp.fileName();
    tmp.close();
    cv::imwrite(path.toStdString(), solid(w, h, cv::Scalar(30, 60, 90)));
    return path;
}

// =====================================================================
//  1. stableId 入栈即分配, 且互不相同
// =====================================================================

void tst_LayerIdentity::test_stableId_assignedAndUnique()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(8, 8, cv::Scalar(4, 5, 6)));
    s.addLayer(QStringLiteral("C"), solid(8, 8, cv::Scalar(7, 8, 9)));

    // 每个图层都要有身份, 且互不相同
    QVERIFY(s.idOf(0) != 0);
    QVERIFY(s.idOf(1) != 0);
    QVERIFY(s.idOf(2) != 0);
    QVERIFY(s.idOf(0) != s.idOf(1));
    QVERIFY(s.idOf(1) != s.idOf(2));
    QVERIFY(s.idOf(0) != s.idOf(2));

    // id 必须能反查回图层
    QCOMPARE(s.indexById(s.idOf(1)), 1);
    QVERIFY(s.byId(s.idOf(2)));
    QCOMPARE(s.byId(s.idOf(2))->name, QStringLiteral("C"));
    QCOMPARE(s.indexById(0), -1);
}

// =====================================================================
//  2. stableId 跟着撤销还原走
// =====================================================================

void tst_LayerIdentity::test_stableId_survivesUndoRestore()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(8, 8, cv::Scalar(4, 5, 6)));
    const LayerId bId = s.idOf(1);

    QUndoStack undo;
    undo.push(LayerCommand::makeRemove(&s, 1));
    QCOMPARE(s.count(), 1);
    QCOMPARE(s.indexById(bId), -1);

    undo.undo();
    QCOMPARE(s.count(), 2);
    // 撤销拿回的是同一个身份
    QCOMPARE(s.indexById(bId), 1);
    QVERIFY(s.byId(bId));
    QCOMPARE(s.byId(bId)->name, QStringLiteral("B"));
}

// =====================================================================
//  3. duplicateLayer 拿到的是新身份
// =====================================================================

void tst_LayerIdentity::test_duplicateLayer_getsNewId()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    const LayerId aId = s.idOf(0);

    QVERIFY(s.duplicateLayer(0));
    QCOMPARE(s.count(), 2);

    const LayerId copyId = s.idOf(1);
    QVERIFY(copyId != 0);
    QVERIFY(copyId != aId);       // 复制出来的是另一个图层
    QVERIFY(s.indexById(aId) >= 0);
    QVERIFY(s.indexById(copyId) >= 0);
}

// =====================================================================
//  4. Remove 命令的 redo 按 id 定位 (LYR-06)
//     旧实现按旧下标删: 下标被别的操作挤走以后会删掉另一个图层
// =====================================================================

void tst_LayerIdentity::test_removeCommand_redoTargetsSameLayer()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(8, 8, cv::Scalar(4, 5, 6)));
    s.addLayer(QStringLiteral("C"), solid(8, 8, cv::Scalar(7, 8, 9)));

    QUndoStack undo;
    undo.push(LayerCommand::makeRemove(&s, 1));   // 删 B
    QCOMPARE(s.count(), 2);

    undo.undo();                                   // B 回到 index 1
    QCOMPARE(s.count(), 3);
    QCOMPARE(s.at(1)->name, QStringLiteral("B"));

    // 把 B 挤走: 删掉 A 之后 B 变成 index 0
    QVERIFY(s.removeLayer(0));
    QCOMPARE(s.count(), 2);
    QCOMPARE(s.at(0)->name, QStringLiteral("B"));

    // redo: 必须删掉 B (按 id), 不能删掉 index 1 上的 C
    undo.redo();
    QCOMPARE(s.count(), 1);
    QCOMPARE(s.at(0)->name, QStringLiteral("C"));
}

// =====================================================================
//  5. Add 命令的 undo 按 id 定位 (LYR-06)
// =====================================================================

void tst_LayerIdentity::test_addCommand_undoRemovesCorrectLayer()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(8, 8, cv::Scalar(4, 5, 6)));
    const LayerId bId = s.idOf(1);

    QUndoStack undo;
    // Add 命令的约定: 调用方先加层, 再 push
    undo.push(new LayerCommand(&s, LayerCommand::Add, *s.at(1)));
    QCOMPARE(s.count(), 2);

    // 把 B 挤到 index 0
    QVERIFY(s.removeLayer(0));
    QCOMPARE(s.count(), 1);
    QCOMPARE(s.at(0)->name, QStringLiteral("B"));

    // undo: 必须删掉 B (按 id), 而不是"index 1 上那层"
    undo.undo();
    QCOMPARE(s.count(), 0);
}

// =====================================================================
//  6. clear() 把编组子层一起清掉 (LYR-12)
// =====================================================================

void tst_LayerIdentity::test_clear_dropsGroupChildren()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(8, 8, cv::Scalar(4, 5, 6)));
    const int gIdx = s.mergeIntoGroup({0, 1});
    QVERIFY(gIdx >= 0);
    const LayerId oldGroupId = s.idOf(gIdx);
    QVERIFY(!s.groupChildrenOf(oldGroupId).empty());

    s.clear();
    QCOMPARE(s.count(), 0);

    // 旧组的子层条目不能跨文档残留 (LYR-12)
    QVERIFY2(s.groupChildrenOf(oldGroupId).empty(),
             "clear() 之后旧编组的子层还挂在 m_groups 里");
}

// =====================================================================
//  7. 嵌套编组时内组的子层不丢
// =====================================================================

void tst_LayerIdentity::test_nestedGroup_keepsInnerChildren()
{
    LayerStack s;
    s.addLayer(QStringLiteral("A"), solid(8, 8, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(8, 8, cv::Scalar(4, 5, 6)));
    s.addLayer(QStringLiteral("C"), solid(8, 8, cv::Scalar(7, 8, 9)));
    s.addLayer(QStringLiteral("D"), solid(8, 8, cv::Scalar(10, 11, 12)));

    const int inner = s.mergeIntoGroup({1, 2});     // B + C -> 内组
    QVERIFY(inner >= 0);
    const LayerId innerId = s.idOf(inner);
    QCOMPARE(int(s.groupChildrenOf(innerId).size()), 2);

    // 再把内组跟 D 编一次 (嵌套)
    const int outer = s.mergeIntoGroup({inner - 1, inner});
    QVERIFY(outer >= 0);

    // 内组的子层必须还在 (编组过程中不能把 m_groups 条目释放掉)
    QCOMPARE(int(s.groupChildrenOf(innerId).size()), 2);
    QCOMPARE(s.groupChildrenOf(innerId)[0]->name, QStringLiteral("B"));
    QCOMPARE(s.groupChildrenOf(innerId)[1]->name, QStringLiteral("C"));
}

// =====================================================================
//  8. 智能滤镜链按 id: 删掉下层的图层, 链还指着滤镜 (LYR-16)
// =====================================================================

void tst_LayerIdentity::test_smartFilterChain_survivesLayerDeletion()
{
    LayerStack s;
    s.addLayer(QStringLiteral("base"), solid(64, 64, cv::Scalar(10, 20, 30)));

    const QString png = makeTempPng(64, 64);
    QVERIFY(!png.isEmpty());
    const int soIdx = s.addSmartObjectLayer(QStringLiteral("so"), png, false);
    QVERIFY(soIdx >= 0);
    const LayerId soId = s.idOf(soIdx);

    const cv::Mat base = s.rasterizeForRender(soIdx);
    QVERIFY(!base.empty());
    const int fIdx = s.appendSmartFilter(soIdx, QStringLiteral("GaussianBlur"),
                                          1.0, base);
    QVERIFY(fIdx >= 0);
    const LayerId filterId = s.idOf(fIdx);
    QCOMPARE(s.smartFilterCount(soIdx), 1);

    // 删掉滤镜**下面**的一层 -> 滤镜和 SmartObject 的下标都会漂移,
    // 链必须还指着原来那层滤镜.
    QVERIFY(s.removeLayer(0));

    const int soIdxNow = s.indexById(soId);
    QVERIFY(soIdxNow >= 0);
    QCOMPARE(s.smartFilterCount(soIdxNow), 1);
    QCOMPARE(s.smartFiltersFor(soIdxNow).size(), 1);
    QCOMPARE(s.smartFiltersFor(soIdxNow).first(), s.indexById(filterId));
}

// =====================================================================
//  9. 智能滤镜的父层记的是 id 不是下标 (LYR-16)
// =====================================================================

void tst_LayerIdentity::test_smartFilter_parentIsId()
{
    LayerStack s;
    const QString png = makeTempPng(64, 64);
    QVERIFY(!png.isEmpty());
    const int soIdx = s.addSmartObjectLayer(QStringLiteral("so"), png, false);
    QVERIFY(soIdx >= 0);
    const LayerId soId = s.idOf(soIdx);

    const cv::Mat base = s.rasterizeForRender(soIdx);
    QVERIFY(!base.empty());
    const int fIdx = s.appendSmartFilter(soIdx, QStringLiteral("Sharpen"),
                                          1.0, base);
    QVERIFY(fIdx >= 0);

    auto fL = s.at(fIdx);
    QVERIFY(fL);
    QCOMPARE(fL->kind, Layer::SmartFilter);
    QCOMPARE(fL->parentSmartId, soId);
    QVERIFY(fL->parentSmartId != 0);
    QVERIFY(fL->isValid());
}

// =====================================================================
//  10. 底图被编进组里以后画布不退化成 800x600 (LYR-05)
// =====================================================================

void tst_LayerIdentity::test_canvasSize_survivesBaseGroupedAway()
{
    LayerStack s;
    s.setBaseLayer(solid(192, 108, cv::Scalar(10, 20, 30)));
    QCOMPARE(s.canvasSize(), cv::Size(192, 108));

    s.addLayer(QStringLiteral("A"), solid(192, 108, cv::Scalar(1, 2, 3)));

    // 把底图也编进组里 -> index 0 变成没有 image 的 Group
    const int g = s.mergeIntoGroup({0, 1});
    QVERIFY(g >= 0);

    // 画布必须还是文档尺寸, 不能退化成 800x600 白底
    QCOMPARE(s.canvasSize(), cv::Size(192, 108));
}

// =====================================================================
//  11. 编组选区不吞底图 (LYR-05)
// =====================================================================

void tst_LayerIdentity::test_groupingSelection_keepsBaseOnTop()
{
    LayerStack s;
    s.setBaseLayer(solid(64, 64, cv::Scalar(10, 20, 30)));
    s.addLayer(QStringLiteral("A"), solid(64, 64, cv::Scalar(1, 2, 3)));
    s.addLayer(QStringLiteral("B"), solid(64, 64, cv::Scalar(4, 5, 6)));
    s.addLayer(QStringLiteral("C"), solid(64, 64, cv::Scalar(7, 8, 9)));

    // 只编组选中的两层 (A, B), 底图必须留在栈顶
    const int g = s.mergeIntoGroup({1, 2});
    QVERIFY(g >= 0);
    QCOMPARE(s.count(), 3);
    QCOMPARE(s.at(0)->name, QStringLiteral("Background"));
    QCOMPARE(s.canvasSize(), cv::Size(64, 64));
}

// =====================================================================
//  12. 图层面板的编组按钮按选区发信号 (LYR-05)
// =====================================================================

void tst_LayerIdentity::test_panel_groupButton_usesSelection()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), solid(16, 16, cv::Scalar(1, 2, 3)));
    stack.addLayer(QStringLiteral("B"), solid(16, 16, cv::Scalar(4, 5, 6)));
    stack.addLayer(QStringLiteral("C"), solid(16, 16, cv::Scalar(7, 8, 9)));

    LayerPanel panel(&stack);
    auto *tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 3);

    // 选中两个顶层 item (UserRole 存的是图层下标)
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        item->setSelected(item->data(0, Qt::UserRole).toInt() != 2);
    }
    QCOMPARE(tree->selectedItems().size(), 2);

    QSignalSpy spy(&panel, &LayerPanel::groupRequested);

    // 找编组按钮 (text = "group")
    QToolButton *groupBtn = nullptr;
    const auto buttons = panel.findChildren<QToolButton*>();
    for (QToolButton *b : buttons) {
        if (b->text() == QStringLiteral("group")) { groupBtn = b; break; }
    }
    QVERIFY(groupBtn != nullptr);

    groupBtn->click();
    QCOMPARE(spy.count(), 1);

    // 信号必须带的是**选中的图层下标**, 不是整个栈
    const QList<int> emitted = spy.at(0).at(0).value<QList<int>>();
    QCOMPARE(emitted.size(), 2);
    QVERIFY(emitted.contains(0));
    QVERIFY(emitted.contains(1));
    QVERIFY(!emitted.contains(2));
}

QTEST_MAIN(tst_LayerIdentity)
#include "tst_LayerIdentity.moc"
