// =============================================================================
//  tst_LayerPanelIntegration - P1.4.3 (2026-09-17) LayerPanel 实例化 + 信号接通
//
//  覆盖 P1.4.3 的 3 个核心路径:
//    1. LayerPanel 创建不崩 (P1.4.2 类定义完整但没 new; P1.4.3 改成 QWidget base)
//    2. selectionChangedFromPanel signal (P1.4.3 接通到 LayerStack::setSelection)
//    3. m_kindProps 当前 index = 3 (SmartObject 页面), 选 SmartObject layer 同步
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
    LayerPanel panel(&stack);
    QVERIFY(panel.findChild<QListWidget*>() != nullptr);
    QVERIFY(panel.findChild<QStackedWidget*>() != nullptr);

    // m_list 应有 1 项
    auto* list = panel.findChild<QListWidget*>();
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 0);
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
    auto* list = panel.findChild<QListWidget*>();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);

    // 确认列表是倒序 (zOrder desc): row 0 -> index 1 (top layer "B"), row 1 -> index 0 (bottom "A")
    QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 1);
    QCOMPARE(list->item(1)->data(Qt::UserRole).toInt(), 0);

    // QSignalSpy 监听 signal
    QSignalSpy spy(&panel, &LayerPanel::selectionChangedFromPanel);

    // setCurrentRow(1) -> row 1 -> data = 0 -> emit signal(0) (底层 layer A)
    list->setCurrentRow(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);

    // 再选 row 0 -> data = 1 -> emit signal(1) (顶层 layer B)
    list->setCurrentRow(0);
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

QTEST_MAIN(tst_LayerPanelIntegration)
#include "tst_LayerPanelIntegration.moc"