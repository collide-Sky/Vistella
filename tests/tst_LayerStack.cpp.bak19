// =============================================================================
//  tst_LayerStack - Layer + LayerStack 单元测试 (阶段 1 W4.4, 2026-09-04)
//
//  覆盖:
//    - Layer 构造 / isValid / width / height
//    - LayerStack add / remove / count
//    - LayerStack moveUp / moveDown
//    - LayerStack setVisible / setOpacity / setLocked / rename
//    - LayerStack render 混合 (opacity blend)
//    - LayerStack setBaseLayer
//    - signals: layerAdded / layerRemoved / layerChanged / countChanged
// =============================================================================

#include <QTest>
#include <QSignalSpy>

#include <opencv2/core.hpp>
#include <cmath>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"

using namespace layers;

class tst_LayerStack : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- Layer ----
    void test_layer_construct();
    void test_layer_isValid();
    void test_layer_widthHeight();

    // ---- LayerStack 基础 ----
    void test_empty();
    void test_addLayer();
    void test_addLayerByName();
    void test_removeLayer();
    void test_clear();

    // ---- LayerStack 修改 ----
    void test_moveUp();
    void test_moveDown();
    void test_setVisible();
    void test_setOpacity();
    void test_setOpacityClamp();
    void test_setLocked();
    void test_rename();

    // ---- render ----
    void test_render_empty();
    void test_render_single();
    void test_render_blend();
    void test_render_invisible();

    // ---- signals ----
    void test_signals_layerAdded();
    void test_signals_layerRemoved();
    void test_signals_layerChanged();
    void test_signals_countChanged();

    // ---- 高级 ----
    void test_setBaseLayer_initial();
    void test_setBaseLayer_replace();
    void test_renderOne();

    // ---- Phase 1 新增 ----
    void test_duplicateLayer();
    void test_setBlend();
    void test_setLinked();
    void test_linkedSelection();
    void test_setSelection();
    void test_selectionSignals();
    void test_mergeDown();
    void test_flattenVisible();

private:
    // 造测试用 cv::Mat (单色 BGR)
    cv::Mat makeMat(int w = 8, int h = 8, cv::Scalar bgr = cv::Scalar(50, 100, 200))
    {
        return cv::Mat(h, w, CV_8UC3, bgr);
    }
};

void tst_LayerStack::initTestCase()
{
}

void tst_LayerStack::cleanupTestCase()
{
}

// =============================================================================
//  Layer
// =============================================================================

void tst_LayerStack::test_layer_construct()
{
    cv::Mat img = makeMat();
    Layer l(QStringLiteral("Test"), img);
    QCOMPARE(l.name, QStringLiteral("Test"));
    QVERIFY(!l.image.empty());
    QCOMPARE(l.width(), 8);
    QCOMPARE(l.height(), 8);
    QVERIFY(l.visible);
    QCOMPARE(l.opacity, 1.0f);
    QCOMPARE(l.zOrder, 0);
    QVERIFY(!l.locked);
}

void tst_LayerStack::test_layer_isValid()
{
    cv::Mat img = makeMat();
    Layer l(QStringLiteral("Valid"), img);
    QVERIFY(l.isValid());

    // 空 image → invalid
    cv::Mat empty;
    Layer l2(QStringLiteral("Empty"), empty);
    QVERIFY(!l2.isValid());

    // 1 通道 → invalid (Layer 要求 3 通道 BGR)
    cv::Mat gray = cv::Mat(4, 4, CV_8UC1, cv::Scalar(100));
    Layer l3(QStringLiteral("Gray"), gray);
    QVERIFY(!l3.isValid());

    // 16 位 → invalid (要求 8U)
    cv::Mat s16 = cv::Mat(4, 4, CV_16SC3, cv::Scalar(0, 0, 0));
    Layer l4(QStringLiteral("S16"), s16);
    QVERIFY(!l4.isValid());
}

void tst_LayerStack::test_layer_widthHeight()
{
    cv::Mat img = makeMat(32, 24);
    Layer l(QStringLiteral("Size"), img);
    QCOMPARE(l.width(), 32);
    QCOMPARE(l.height(), 24);
}

// =============================================================================
//  LayerStack 基础
// =============================================================================

void tst_LayerStack::test_empty()
{
    LayerStack stack;
    QCOMPARE(stack.count(), 0);
    QVERIFY(stack.isEmpty());
    QCOMPARE(stack.baseLayer(), nullptr);
    QCOMPARE(stack.topLayer(), nullptr);
}

void tst_LayerStack::test_addLayer()
{
    LayerStack stack;
    int idx = stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(idx, 0);
    QCOMPARE(stack.count(), 1);
    QVERIFY(!stack.isEmpty());
    QVERIFY(stack.baseLayer() != nullptr);
    QCOMPARE(stack.baseLayer()->name, QStringLiteral("L1"));

    int idx2 = stack.addLayer(QStringLiteral("L2"), makeMat());
    QCOMPARE(idx2, 1);
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.topLayer()->name, QStringLiteral("L2"));
}

void tst_LayerStack::test_addLayerByName()
{
    LayerStack stack;
    int idx = stack.addLayer(Layer(QStringLiteral("Custom"), makeMat()));
    QCOMPARE(idx, 0);
    QCOMPARE(stack.at(0)->name, QStringLiteral("Custom"));
}

void tst_LayerStack::test_removeLayer()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());
    QCOMPARE(stack.count(), 3);

    // 删中间的 B (index 1)
    QVERIFY(stack.removeLayer(1));
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("C"));

    // 越界
    QVERIFY(!stack.removeLayer(-1));
    QVERIFY(!stack.removeLayer(99));
    QCOMPARE(stack.count(), 2);
}

void tst_LayerStack::test_clear()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    QCOMPARE(stack.count(), 2);

    stack.clear();
    QCOMPARE(stack.count(), 0);
    QVERIFY(stack.isEmpty());
}

// =============================================================================
//  LayerStack 修改
// =============================================================================

void tst_LayerStack::test_moveUp()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());
    // A=0, B=1, C=2

    // 移动 A (index 0) 上 → 变 1
    QVERIFY(stack.moveUp(0));
    QCOMPARE(stack.at(0)->name, QStringLiteral("B"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(2)->name, QStringLiteral("C"));

    // 已经在最顶 (C index 2) → 失败
    QVERIFY(!stack.moveUp(2));
}

void tst_LayerStack::test_moveDown()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());

    // 移动 C (index 2) 下 → 变 1
    QVERIFY(stack.moveDown(2));
    QCOMPARE(stack.at(0)->name, QStringLiteral("A"));
    QCOMPARE(stack.at(1)->name, QStringLiteral("C"));
    QCOMPARE(stack.at(2)->name, QStringLiteral("B"));

    // 已经在最底 (A index 0) → 失败
    QVERIFY(!stack.moveDown(0));
}

void tst_LayerStack::test_setVisible()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QVERIFY(stack.at(0)->visible);

    QVERIFY(stack.setVisible(0, false));
    QVERIFY(!stack.at(0)->visible);

    // 设同值 → 失败
    QVERIFY(!stack.setVisible(0, false));

    QVERIFY(stack.setVisible(0, true));
    QVERIFY(stack.at(0)->visible);
}

void tst_LayerStack::test_setOpacity()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    QVERIFY(stack.setOpacity(0, 0.5f));
    QCOMPARE(stack.at(0)->opacity, 0.5f);

    // 同值 → 失败
    QVERIFY(!stack.setOpacity(0, 0.5f));
}

void tst_LayerStack::test_setOpacityClamp()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());

    stack.setOpacity(0, 1.5f);   // clamp 到 1.0
    QCOMPARE(stack.at(0)->opacity, 1.0f);

    stack.setOpacity(0, -0.3f);  // clamp 到 0.0
    QCOMPARE(stack.at(0)->opacity, 0.0f);
}

void tst_LayerStack::test_setLocked()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QVERIFY(!stack.at(0)->locked);

    QVERIFY(stack.setLocked(0, true));
    QVERIFY(stack.at(0)->locked);
}

void tst_LayerStack::test_rename()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Old"), makeMat());

    QVERIFY(stack.rename(0, QStringLiteral("New")));
    QCOMPARE(stack.at(0)->name, QStringLiteral("New"));

    // 同名 → 失败
    QVERIFY(!stack.rename(0, QStringLiteral("New")));

    // 空名字 → 失败
    QVERIFY(!stack.rename(0, QString()));
}

// =============================================================================
//  render
// =============================================================================

void tst_LayerStack::test_render_empty()
{
    LayerStack stack;
    QVERIFY(stack.render().empty());
}

void tst_LayerStack::test_render_single()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat(8, 8, cv::Scalar(100, 150, 200)));
    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.cols, 8);
    QCOMPARE(out.rows, 8);
    // opacity=1.0 → 原值
    QCOMPARE(int(out.at<cv::Vec3b>(4, 4)[0]), 100);
    QCOMPARE(int(out.at<cv::Vec3b>(4, 4)[1]), 150);
    QCOMPARE(int(out.at<cv::Vec3b>(4, 4)[2]), 200);
}

void tst_LayerStack::test_render_blend()
{
    // L1 红色 (BGR 0,0,255), opacity 0.5
    // L2 蓝色 (BGR 255,0,0), opacity 0.5
    // render 算法:
    //   1) canvas = L1 (BGR 0,0,255) → opacity 0.5 blend 黑色 → canvas = (0, 0, 127)
    //   2) L2 (BGR 255,0,0) opacity 0.5 blend canvas → 0.5 * canvas + 0.5 * L2
    //      = 0.5 * (0,0,127) + 0.5 * (255,0,0) = (127, 0, 64)  (B=127 半蓝, R=64 浅红)
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat(8, 8, cv::Scalar(0, 0, 255)));
    stack.setOpacity(0, 0.5f);
    stack.addLayer(QStringLiteral("L2"), makeMat(8, 8, cv::Scalar(255, 0, 0)));
    stack.setOpacity(1, 0.5f);

    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    const cv::Vec3b pix = out.at<cv::Vec3b>(4, 4);
    // 期望 (BGR 127, 0, 64) — 偏紫但 R 比 B 弱
    QVERIFY(std::abs(int(pix[0]) - 127) < 5);
    QVERIFY(std::abs(int(pix[1]) - 0) < 5);
    QVERIFY(std::abs(int(pix[2]) - 64) < 5);
}

void tst_LayerStack::test_render_invisible()
{
    // L1 红色, visible
    // L2 蓝色, invisible → render 只看 L1
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat(8, 8, cv::Scalar(0, 0, 255)));
    stack.addLayer(QStringLiteral("L2"), makeMat(8, 8, cv::Scalar(255, 0, 0)));
    stack.setVisible(1, false);

    cv::Mat out = stack.render();
    QCOMPARE(int(out.at<cv::Vec3b>(4, 4)[2]), 255);   // R = 255 (L1)
    QCOMPARE(int(out.at<cv::Vec3b>(4, 4)[0]), 0);     // B = 0 (L1)
}

// =============================================================================
//  signals
// =============================================================================

void tst_LayerStack::test_signals_layerAdded()
{
    LayerStack stack;
    QSignalSpy spy(&stack, &LayerStack::layerAdded);
    QSignalSpy spyCount(&stack, &LayerStack::countChanged);

    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
    QCOMPARE(spyCount.count(), 1);
}

void tst_LayerStack::test_signals_layerRemoved()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QSignalSpy spy(&stack, &LayerStack::layerRemoved);
    QSignalSpy spyCount(&stack, &LayerStack::countChanged);

    stack.removeLayer(0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spyCount.count(), 1);
}

void tst_LayerStack::test_signals_layerChanged()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QSignalSpy spy(&stack, &LayerStack::layerChanged);

    stack.setVisible(0, false);
    QCOMPARE(spy.count(), 1);

    stack.setOpacity(0, 0.5f);
    QCOMPARE(spy.count(), 2);

    stack.rename(0, QStringLiteral("New"));
    QCOMPARE(spy.count(), 3);
}

void tst_LayerStack::test_signals_countChanged()
{
    LayerStack stack;
    QSignalSpy spy(&stack, &LayerStack::countChanged);

    stack.addLayer(QStringLiteral("A"), makeMat());
    QCOMPARE(spy.count(), 1);

    stack.addLayer(QStringLiteral("B"), makeMat());
    QCOMPARE(spy.count(), 2);

    stack.removeLayer(0);
    QCOMPARE(spy.count(), 3);

    stack.clear();
    QCOMPARE(spy.count(), 4);

    // 重复 clear 不 emit
    stack.clear();
    QCOMPARE(spy.count(), 4);
}

// =============================================================================
//  高级
// =============================================================================

void tst_LayerStack::test_setBaseLayer_initial()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(16, 16, cv::Scalar(255, 0, 0)));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.baseLayer()->name, QStringLiteral("Background"));
    QCOMPARE(stack.baseLayer()->image.cols, 16);
    QCOMPARE(stack.baseLayer()->image.rows, 16);
}

void tst_LayerStack::test_setBaseLayer_replace()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(8, 8, cv::Scalar(0, 0, 255)));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.baseLayer()->image.cols, 8);

    // 替换底图
    stack.setBaseLayer(makeMat(16, 16, cv::Scalar(255, 0, 0)));
    QCOMPARE(stack.count(), 1);   // 仍然 1 个
    QCOMPARE(stack.baseLayer()->image.cols, 16);   // 尺寸变了
    QCOMPARE(stack.baseLayer()->image.rows, 16);
}

void tst_LayerStack::test_renderOne()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat(8, 8, cv::Scalar(100, 150, 200)));
    stack.setOpacity(0, 0.5f);

    cv::Mat out = stack.renderOne(0);
    QVERIFY(!out.empty());
    // opacity 0.5 → 100 * 0.5 = 50
    QVERIFY(std::abs(int(out.at<cv::Vec3b>(0, 0)[0]) - 50) < 5);
}

// =====================================================================
//  Phase 1 新增: 复制 / 合并 / 链接 / 选中
// =====================================================================

void tst_LayerStack::test_duplicateLayer()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Original"), makeMat(8, 8, cv::Scalar(50, 100, 200)));
    QCOMPARE(stack.count(), 1);

    int newIdx = stack.duplicateLayer(0);
    QVERIFY(newIdx >= 0);
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.at(newIdx)->name, QStringLiteral("Original copy"));
    // 图像内容相同
    QCOMPARE(stack.at(newIdx)->image.cols, 8);
    QCOMPARE(int(stack.at(newIdx)->image.at<cv::Vec3b>(4, 4)[0]), 50);
}

void tst_LayerStack::test_setBlend()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("L1"), makeMat());
    QCOMPARE(stack.at(0)->blend, Layer::Normal);

    QVERIFY(stack.setBlend(0, Layer::Multiply));
    QCOMPARE(stack.at(0)->blend, Layer::Multiply);

    QVERIFY(stack.setBlend(0, Layer::Screen));
    QCOMPARE(stack.at(0)->blend, Layer::Screen);

    // 同值不 emit
    QVERIFY(!stack.setBlend(0, Layer::Screen));
}

void tst_LayerStack::test_setLinked()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    QVERIFY(!stack.at(0)->isLinked);

    QVERIFY(stack.setLinked(0, true));
    QVERIFY(stack.at(0)->isLinked);
    QVERIFY(!stack.setLinked(0, true));   // 同值
}

void tst_LayerStack::test_linkedSelection()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    stack.addLayer(QStringLiteral("C"), makeMat());

    // 都不 link → linkedSelection(B) 只返 [B]
    QList<int> sel1 = stack.linkedSelection(1);
    QCOMPARE(sel1.size(), 1);
    QCOMPARE(sel1.at(0), 1);

    // link A 和 C (跨 link, 但 B 不 link)
    stack.setLinked(0, true);
    stack.setLinked(2, true);
    // A link: linkedSelection(A) 应该返 [A, C] (所有 link 的)
    QList<int> sel2 = stack.linkedSelection(0);
    QCOMPARE(sel2.size(), 2);
    QVERIFY(sel2.contains(0));
    QVERIFY(sel2.contains(2));
}

void tst_LayerStack::test_setSelection()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());

    QCOMPARE(stack.selection(), -1);   // 默认无选中
    stack.setSelection(1);
    QCOMPARE(stack.selection(), 1);
    QList<int> sel = stack.selectedIndices();
    QCOMPARE(sel.size(), 1);
    QCOMPARE(sel.at(0), 1);
}

void tst_LayerStack::test_selectionSignals()
{
    LayerStack stack;
    QSignalSpy spy(&stack, &LayerStack::selectionChanged);

    stack.addLayer(QStringLiteral("A"), makeMat());
    stack.addLayer(QStringLiteral("B"), makeMat());
    QCOMPARE(spy.count(), 0);

    stack.setSelection(0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);

    stack.setSelection(1);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 1);

    // 同值不 emit
    stack.setSelection(1);
    QCOMPARE(spy.count(), 2);

    // clearSelection
    stack.clearSelection();
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toInt(), -1);
}

void tst_LayerStack::test_mergeDown()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeMat(8, 8, cv::Scalar(0, 0, 255)));   // 红
    stack.addLayer(QStringLiteral("Top"),  makeMat(8, 8, cv::Scalar(255, 0, 0)));   // 蓝
    // 把 Top opacity 调 0.5, mergeDown 后 Base = 0.5 * 红 + 0.5 * 蓝
    stack.setOpacity(1, 0.5f);

    QVERIFY(stack.mergeDown(1));   // Top merge 到 Base
    QCOMPARE(stack.count(), 1);
    // Base 颜色: 0.5 * 红 + 0.5 * 蓝 = (BGR 127, 0, 127)
    const cv::Vec3b pix = stack.at(0)->image.at<cv::Vec3b>(4, 4);
    QVERIFY(std::abs(int(pix[0]) - 127) < 5);
    QVERIFY(std::abs(int(pix[2]) - 127) < 5);

    // 越界 mergeDown 应该失败
    QVERIFY(!stack.mergeDown(0));
}

void tst_LayerStack::test_flattenVisible()
{
    LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeMat(8, 8, cv::Scalar(100, 100, 100)));
    stack.addLayer(QStringLiteral("Top"),  makeMat(8, 8, cv::Scalar(50, 50, 50)));
    QCOMPARE(stack.count(), 2);

    cv::Mat flat = stack.flattenVisible();
    QVERIFY(!flat.empty());
    // flattenVisible 内部会 clear + add 1 个 base layer
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.at(0)->name, QStringLiteral("Background"));
}

QTEST_MAIN(tst_LayerStack)
#include "tst_LayerStack.moc"
