// =============================================================================
//  tst_LayerPanelIntegration - P1.4.3 (2026-09-17) LayerPanel 实例化 + 信号接通
//                              P0 leftover 2 (2026-09-21) Group children 显示
//
//  覆盖 P1.4.3 的 3 个核心路径:
//    1. LayerPanel 创建不崩 (P1.4.2 类定义完整但没 new; P1.4.3 改成 QWidget base)
//    2. selectionChangedFromPanel signal (P1.4.3 接通到 LayerStack::setSelection)
//    3. m_kindProps 当前 index = 3 (SmartObject 页面), 选 SmartObject layer 同步
//
//  P0 leftover 2 (2026-09-21) 新增:
//    4. QTreeWidget 取代 QListWidget (Group children 用 child item 表示)
//    5. Group items expanded by default, children indented under parent
//    6. selectionChangedFromPanelChild(LayerId, int) 路由 group child clicks
//    7. Top-level items keep emitting selectionChangedFromPanel(int) for
//       backward compatibility
//
//  注: 不构造 ImageWindow (单测不能 new ImageWindow - 需要 QApplication +
//  大量 UI 依赖, 见 tst_LayerCommand.cpp:554). 这里直接验证 LayerPanel 的 UI 行为,
//  LayerPanel 本身只依赖 LayerStack + 标准 Qt widgets, 测试可以独立构造.
// =============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QStackedWidget>

#include <opencv2/core.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerPanel.h"

using namespace layers;

class tst_LayerPanelIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // P1.4.3: LayerPanel 实例化 (P1.4.2 类定义完整但没 new, 现在改成 QWidget 可嵌入 tab)
    void test_layerPanel_creation_doesnt_crash();
    // P1.4.3: 列表点击 -> selectionChangedFromPanel signal -> LayerStack::setSelection
    void test_layerPanel_selection_signal();
    // P1.4.3: SmartObject layer -> m_kindProps 当前 index = 3
    void test_layerPanel_kindPropsSmartObject();
    // P0 leftover 2 (2026-09-21): Group items appear in tree, expanded by
    //   default with their children rendered as child items under the parent.
    void test_layerPanel_group_appears_with_children();
    // P0 leftover 2 (2026-09-21): clicking a Group child item emits
    //   selectionChangedFromPanelChild with the encoded (groupLayerId,
    //   childIdx); top-level clicks still emit selectionChangedFromPanel(int).
    void test_layerPanel_child_selection_signal();
    // P0 leftover 2 (2026-09-21): Group survives multiple rebuildList calls
    //   (e.g. on layerAdded signal); children count and order are stable.
    void test_layerPanel_group_survives_rebuild();

private:
    // 通过 friend 或访问器暴露内部 widget 给测试
    //   LayerPanel 没暴露 list/kindProps 访问器, 这里用 findChild 找
};

void tst_LayerPanelIntegration::initTestCase() {}
void tst_LayerPanelIntegration::cleanupTestCase() {}

// =====================================================================
//  LayerPanel 创建 + 1 个 Bitmap layer -> m_list->count() == 1
//   P1.4.3 base class 从 QDockWidget 改成 QWidget, 仍走同一 ctor 流程
//   不需要 parent dock, 直接构造测试创建是否崩
// =====================================================================

void tst_LayerPanelIntegration::test_layerPanel_creation_doesnt_crash()
{
    LayerStack stack;
    cv::Mat img(24, 24, CV_8UC3, cv::Scalar(120, 80, 200));
    stack.addLayer(QStringLiteral("Layer1"), img);
    QCOMPARE(stack.count(), 1);

    // LayerPanel ctor (P1.4.3 改 QWidget 后): 不再调 setWidget(content), 改用 outerLayout 包 content
    // P0 leftover 2 (2026-09-21): m_list (QListWidget) -> m_tree (QTreeWidget).
    LayerPanel panel(&stack);
    QVERIFY(panel.findChild<QTreeWidget*>() != nullptr);
    QVERIFY(panel.findChild<QStackedWidget*>() != nullptr);

    auto* tree = panel.findChild<QTreeWidget*>();
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toInt(), 0);
}

// =====================================================================
//  selectionChangedFromPanel signal
//   流程: 调 list->setCurrentRow 模拟用户点列表
//         -> LayerPanel::onListCurrentRowChanged
//         -> emit selectionChangedFromPanel(index)
//   P1.4.3: 该 signal 由 ImageWindow 接通到 LayerStack::setSelection
//
//   注: LayerPanel::rebuildList 按 zOrder desc 排 (顶层在 row 0), item->data(UserRole) 存
//       layer index (不是 list row). 所以 row 0 -> index N-1, row N-1 -> index 0.
// =====================================================================

void tst_LayerPanelIntegration::test_layerPanel_selection_signal()
{
    LayerStack stack;
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(40, 80, 120));
    stack.addLayer(QStringLiteral("A"), img);
    stack.addLayer(QStringLiteral("B"), img);
    QCOMPARE(stack.count(), 2);

    LayerPanel panel(&stack);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 2);

    // Confirm tree is in zOrder-desc order: top-level 0 -> index 1 ("B"),
    // top-level 1 -> index 0 ("A"). UserRole stores layer index (not row).
    QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toInt(), 1);
    QCOMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole).toInt(), 0);

    // QSignalSpy listens to the legacy int-index signal (still emitted for
    // top-level clicks; child clicks land on selectionChangedFromPanelChild).
    QSignalSpy spy(&panel, &LayerPanel::selectionChangedFromPanel);

    // setCurrentItem(topItemForIndex0) -> emit signal(0) (bottom layer "A").
    // Use the item that stores UserRole=0 (which is top-level position 1).
    tree->setCurrentItem(tree->topLevelItem(1));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);

    // Re-select top-level position 0 (UserRole=1, top layer "B").
    tree->setCurrentItem(tree->topLevelItem(0));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 1);
}

// =====================================================================
//  m_kindProps 切到 SmartObject 页面 (index 3)
//   流程: 添加 1 个 SmartObject layer -> setSelection(0) -> LayerStack::selectionChanged
//         -> LayerPanel::onSelectionChanged -> syncKindProps -> m_kindProps->setCurrentIndex(3)
//   注: 刚 addSmartObjectLayer 后 m_selection = -1, syncKindProps(-1) 走 page 0 (empty),
//       所以必须 setSelection(0) 才会切到 page 3.
// =====================================================================

void tst_LayerPanelIntegration::test_layerPanel_kindPropsSmartObject()
{
    LayerStack stack;
    const QString srcPath = QStringLiteral("/tmp/test_layerPanel_smartobj.png");
    stack.addSmartObjectLayer(QStringLiteral("SO"), srcPath, /*embed=*/false);
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    LayerPanel panel(&stack);
    auto* kindProps = panel.findChild<QStackedWidget*>();
    QVERIFY(kindProps != nullptr);
    QCOMPARE(kindProps->count(), 5);   // empty/Text/Vector/SmartObject/Adjustment

    // 默认 selection = -1 -> kindProps 显示 page 0 (empty)
    QCOMPARE(kindProps->currentIndex(), 0);

    // setSelection(0) 触发 selectionChanged(0) -> syncKindProps(0) -> page 3 (SmartObject)
    stack.setSelection(0);
    QCOMPARE(kindProps->currentIndex(), 3);
}

// =====================================================================
//  P0 leftover 2 (2026-09-21): Group tree rendering
//   - Group layer renders as a top-level tree item with an "F" icon
//   - Group's children render as QTreeWidgetItem children under the parent
//   - Group items default to expanded (PS-style)
//   - rebuildList() called by layerAdded signal must re-render correctly
// =====================================================================

void tst_LayerPanelIntegration::test_layerPanel_group_appears_with_children()
{
    LayerStack stack;
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(40, 80, 120));
    stack.addLayer(QStringLiteral("A"), img);   // idx 0
    stack.addLayer(QStringLiteral("B"), img);   // idx 1
    stack.addLayer(QStringLiteral("C"), img);   // idx 2
    const int gIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(gIdx >= 0);
    QCOMPARE(stack.count(), 2);   // [A, Group(B,C)]

    LayerPanel panel(&stack);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 2);   // A + Group(B,C)

    // Tree is zOrder desc: top-level 0 = Group(B,C) (newer), top-level 1 = A.
    auto* topGroup = tree->topLevelItem(0);
    auto* topA     = tree->topLevelItem(1);
    QCOMPARE(topGroup->data(0, Qt::UserRole).toInt(), gIdx);
    QCOMPARE(topA->data(0, Qt::UserRole).toInt(), 0);
    QCOMPARE(topGroup->childCount(), 2);   // B + C as children
    QCOMPARE(topGroup->isExpanded(), true);   // PS: default-expanded

    // Verify children names are preserved in order.
    QCOMPARE(topGroup->child(0)->text(0).contains(QStringLiteral("B")), true);
    QCOMPARE(topGroup->child(1)->text(0).contains(QStringLiteral("C")), true);
    QCOMPARE(topA->childCount(), 0);   // A is not a group, no children
}

void tst_LayerPanelIntegration::test_layerPanel_child_selection_signal()
{
    LayerStack stack;
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(40, 80, 120));
    stack.addLayer(QStringLiteral("A"), img);
    stack.addLayer(QStringLiteral("B"), img);
    stack.addLayer(QStringLiteral("C"), img);
    const int gIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(gIdx >= 0);
    const LayerId gId = stack.idOf(gIdx);
    QVERIFY(gId != 0);

    LayerPanel panel(&stack);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree != nullptr);

    QSignalSpy spyTop(&panel, &LayerPanel::selectionChangedFromPanel);
    QSignalSpy spyChild(&panel, &LayerPanel::selectionChangedFromPanelChild);

    // Activate top-level group item -> selectionChangedFromPanel(gIdx)
    tree->setCurrentItem(tree->topLevelItem(0));
    QCOMPARE(spyTop.count(), 1);
    QCOMPARE(spyTop.at(0).at(0).toInt(), gIdx);
    QCOMPARE(spyChild.count(), 0);   // top-level must NOT emit child signal

    // Activate group child item 0 (B) -> selectionChangedFromPanelChild(gId, 0)
    auto* childB = tree->topLevelItem(0)->child(0);
    tree->setCurrentItem(childB);
    QCOMPARE(spyChild.count(), 1);
    QCOMPARE(spyChild.at(0).at(0).value<LayerId>(), gId);
    QCOMPARE(spyChild.at(0).at(1).toInt(), 0);
    QCOMPARE(spyTop.count(), 1);   // child must NOT emit int-index signal

    // Activate group child item 1 (C) -> selectionChangedFromPanelChild(gId, 1)
    tree->setCurrentItem(tree->topLevelItem(0)->child(1));
    QCOMPARE(spyChild.count(), 2);
    QCOMPARE(spyChild.at(1).at(0).value<LayerId>(), gId);
    QCOMPARE(spyChild.at(1).at(1).toInt(), 1);
}

void tst_LayerPanelIntegration::test_layerPanel_group_survives_rebuild()
{
    LayerStack stack;
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(40, 80, 120));
    stack.addLayer(QStringLiteral("A"), img);
    stack.addLayer(QStringLiteral("B"), img);
    stack.addLayer(QStringLiteral("C"), img);
    const int gIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(gIdx >= 0);

    LayerPanel panel(&stack);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);

    // Trigger a rebuild by adding another layer (LayerStack::layerAdded ->
    // LayerPanel::onLayerAdded -> rebuildList).
    stack.addLayer(QStringLiteral("D"), img);
    QCOMPARE(stack.count(), 3);   // [A, Group(B,C), D]
    QCOMPARE(tree->topLevelItemCount(), 3);

    // After rebuild the Group should still be present and expanded with 2 children.
    // Find the top-level item that holds gIdx in UserRole (positions shifted
    // because D was added; zOrder-desc puts D at top-level 0).
    QTreeWidgetItem *groupItem = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toInt() == gIdx) {
            groupItem = tree->topLevelItem(i);
            break;
        }
    }
    QVERIFY(groupItem != nullptr);
    QCOMPARE(groupItem->childCount(), 2);
    QCOMPARE(groupItem->isExpanded(), true);
}

QTEST_MAIN(tst_LayerPanelIntegration)
#include "tst_LayerPanelIntegration.moc"