// =============================================================================
//  tst_SmartFilter - P1.4.4 (2026-09-17) SmartFilter sub-layer + chain render
//  + undo stack unit tests (8 cases).
//
//  Coverage:
//    - appendSmartFilter_basic (returns idx, kind=SmartFilter, parentSmartIndex correct)
//    - smartFilterChain_order (3 filters returned in insertion order)
//    - removeSmartFilter (chain + stack count both decrease)
//    - moveSmartFilter (slot reorder, filterSlotIndex renumbered)
//    - setSmartFilterEnabled (opacity 0 vs 1, layer remains in chain)
//    - render_smartObject_withFilter (SmartObject + Blur, render non-empty, pixels differ)
//    - smartFilterUndo_append (append + undo removes filter)
//    - smartFilterUndo_remove (append + remove + undo restores filter)
// =============================================================================

#include <QTest>
#include <QUndoStack>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"

using namespace layers;

class tst_SmartFilter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_appendSmartFilter_basic();
    void test_smartFilterChain_order();
    void test_removeSmartFilter();
    void test_moveSmartFilter();
    void test_setSmartFilterEnabled();
    void test_render_smartObject_withFilter();
    void test_smartFilterUndo_append();
    void test_smartFilterUndo_remove();

private:
    QString makeTempPng(int w, int h, const QString &tag);
};

void tst_SmartFilter::initTestCase() {}
void tst_SmartFilter::cleanupTestCase()
{
    LayerStack::cleanupSmartObjectCache();
}

// ---- helpers ----

static int addNoiseBaseSmartObject(LayerStack &stack)
{
    // Use a temp file with a noise pattern so GaussianBlur visibly changes it.
    QTemporaryFile tmp(QDir::tempPath()
                       + QStringLiteral("/tst_smartfilter_noise_XXXXXX.png"));
    tmp.setAutoRemove(false);
    if (!tmp.open()) return -1;
    const QString tmpPath = tmp.fileName();
    tmp.close();
    const int W = 64, H = 64;
    cv::Mat img(H, W, CV_8UC3);
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            const uchar b = static_cast<uchar>((r * 4 + c * 3) % 256);
            const uchar g = static_cast<uchar>((r * 7 + c * 5 + 50) % 256);
            const uchar red = static_cast<uchar>((r * 11 + c * 13 + 100) % 256);
            img.at<cv::Vec3b>(r, c) = cv::Vec3b(b, g, red);
        }
    }
    cv::imwrite(tmpPath.toStdString(), img);
    return stack.addSmartObjectLayer(QStringLiteral("BaseSO"), tmpPath, false);
}

static cv::Mat makeCheckerboard(int w, int h)
{
    cv::Mat img(h, w, CV_8UC3);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            const bool on = ((r / 4) + (c / 4)) % 2 == 0;
            img.at<cv::Vec3b>(r, c) = on
                ? cv::Vec3b(255, 255, 255)
                : cv::Vec3b(0, 0, 0);
        }
    }
    return img;
}

// =====================================================================
//  1. appendSmartFilter_basic
// =====================================================================

void tst_SmartFilter::test_appendSmartFilter_basic()
{
    LayerStack stack;
    const int soIdx = addNoiseBaseSmartObject(stack);
    QCOMPARE(soIdx, 0);
    QCOMPARE(stack.at(soIdx)->kind, Layer::SmartObject);

    const cv::Mat base = stack.rasterizeForRender(soIdx);
    QVERIFY(!base.empty());

    const int fIdx = stack.appendSmartFilter(
        soIdx, QStringLiteral("GaussianBlur"), 1.0, base);
    QVERIFY(fIdx >= 0);
    QCOMPARE(stack.count(), 2);

    auto fL = stack.at(fIdx);
    QVERIFY(fL);
    QCOMPARE(fL->kind, Layer::SmartFilter);
    QCOMPARE(fL->filterType, QStringLiteral("GaussianBlur"));
    QCOMPARE(fL->parentSmartIndex, soIdx);
    QVERIFY(!fL->image.empty());
    QVERIFY(fL->isValid());

    QCOMPARE(stack.smartFilterCount(soIdx), 1);
    QCOMPARE(stack.smartFiltersFor(soIdx).size(), 1);
    QCOMPARE(stack.smartFiltersFor(soIdx).first(), fIdx);
}

// =====================================================================
//  2. smartFilterChain_order
// =====================================================================

void tst_SmartFilter::test_smartFilterChain_order()
{
    LayerStack stack;
    const int soIdx = addNoiseBaseSmartObject(stack);
    const cv::Mat base = stack.rasterizeForRender(soIdx);
    QVERIFY(!base.empty());

    const int f0 = stack.appendSmartFilter(
        soIdx, QStringLiteral("GaussianBlur"), 1.0, base);
    const int f1 = stack.appendSmartFilter(
        soIdx, QStringLiteral("Sharpen"), 1.0, base);
    const int f2 = stack.appendSmartFilter(
        soIdx, QStringLiteral("Brightness"), 1.0, base);
    QVERIFY(f0 >= 0 && f1 >= 0 && f2 >= 0);
    QCOMPARE(stack.count(), 4);

    const auto &chain = stack.smartFiltersFor(soIdx);
    QCOMPARE(chain.size(), 3);
    QCOMPARE(chain[0], f0);
    QCOMPARE(chain[1], f1);
    QCOMPARE(chain[2], f2);
    QCOMPARE(stack.smartFilterCount(soIdx), 3);

    // Slot indices are 0-based and match chain position.
    QCOMPARE(stack.at(f0)->filterSlotIndex, 0);
    QCOMPARE(stack.at(f1)->filterSlotIndex, 1);
    QCOMPARE(stack.at(f2)->filterSlotIndex, 2);
}

// =====================================================================
//  3. removeSmartFilter
// =====================================================================

void tst_SmartFilter::test_removeSmartFilter()
{
    LayerStack stack;
    const int soIdx = addNoiseBaseSmartObject(stack);
    const cv::Mat base = stack.rasterizeForRender(soIdx);
    const int f0 = stack.appendSmartFilter(
        soIdx, QStringLiteral("GaussianBlur"), 1.0, base);
    const int f1 = stack.appendSmartFilter(
        soIdx, QStringLiteral("Sharpen"), 1.0, base);
    QVERIFY(f0 >= 0 && f1 >= 0);
    QCOMPARE(stack.count(), 3);
    QCOMPARE(stack.smartFilterCount(soIdx), 2);

    // Capture the f1 layer pointer to verify it survives the f0 removal.
    LayerPtr f1Before = stack.at(f1);

    QVERIFY(stack.removeSmartFilter(soIdx, f0));
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.smartFilterCount(soIdx), 1);
    // f1 was at index 2; after removing f0 (index 1) f1 shifts down to 1.
    QCOMPARE(stack.smartFiltersFor(soIdx).size(), 1);
    QCOMPARE(stack.smartFiltersFor(soIdx).first(), f1 - 1);
    QCOMPARE(stack.at(f1 - 1), f1Before);  // same shared_ptr object
    QCOMPARE(f1Before->filterSlotIndex, 0);
    QCOMPARE(f1Before->kind, Layer::SmartFilter);

    // Bad removal: wrong index / wrong parent
    QVERIFY(!stack.removeSmartFilter(soIdx, 999));
    QVERIFY(!stack.removeSmartFilter(999, f1 - 1));
}

// =====================================================================
//  4. moveSmartFilter
// =====================================================================

void tst_SmartFilter::test_moveSmartFilter()
{
    LayerStack stack;
    const int soIdx = addNoiseBaseSmartObject(stack);
    const cv::Mat base = stack.rasterizeForRender(soIdx);
    const int f0 = stack.appendSmartFilter(
        soIdx, QStringLiteral("GaussianBlur"), 1.0, base);
    const int f1 = stack.appendSmartFilter(
        soIdx, QStringLiteral("Sharpen"), 1.0, base);
    const int f2 = stack.appendSmartFilter(
        soIdx, QStringLiteral("Brightness"), 1.0, base);
    QVERIFY(f0 >= 0 && f1 >= 0 && f2 >= 0);

    // Move first -> last
    QVERIFY(stack.moveSmartFilter(soIdx, f0, 2));
    const auto &chain = stack.smartFiltersFor(soIdx);
    QCOMPARE(chain[0], f1);
    QCOMPARE(chain[1], f2);
    QCOMPARE(chain[2], f0);
    QCOMPARE(stack.at(f1)->filterSlotIndex, 0);
    QCOMPARE(stack.at(f2)->filterSlotIndex, 1);
    QCOMPARE(stack.at(f0)->filterSlotIndex, 2);

    // Move last -> first
    QVERIFY(stack.moveSmartFilter(soIdx, f0, 0));
    QCOMPARE(stack.smartFiltersFor(soIdx)[0], f0);
    QCOMPARE(stack.smartFiltersFor(soIdx)[1], f1);
    QCOMPARE(stack.smartFiltersFor(soIdx)[2], f2);
    QCOMPARE(stack.at(f0)->filterSlotIndex, 0);

    // Out of range / no-op
    QVERIFY(!stack.moveSmartFilter(soIdx, f0, 5));
    QVERIFY(!stack.moveSmartFilter(soIdx, 999, 0));
}

// =====================================================================
//  5. setSmartFilterEnabled
// =====================================================================

void tst_SmartFilter::test_setSmartFilterEnabled()
{
    LayerStack stack;
    const int soIdx = addNoiseBaseSmartObject(stack);
    const cv::Mat base = stack.rasterizeForRender(soIdx);
    const int fIdx = stack.appendSmartFilter(
        soIdx, QStringLiteral("GaussianBlur"), 1.0, base);
    QVERIFY(fIdx >= 0);

    QVERIFY(stack.setSmartFilterEnabled(soIdx, fIdx, false));
    QCOMPARE(stack.at(fIdx)->opacity, 0.0f);
    // Layer still in chain
    QCOMPARE(stack.smartFilterCount(soIdx), 1);

    QVERIFY(stack.setSmartFilterEnabled(soIdx, fIdx, true));
    QCOMPARE(stack.at(fIdx)->opacity, 1.0f);
    QCOMPARE(stack.smartFilterCount(soIdx), 1);

    // Bad: wrong parent
    QVERIFY(!stack.setSmartFilterEnabled(999, fIdx, false));
    // Bad: wrong kind
    QVERIFY(!stack.setSmartFilterEnabled(soIdx, soIdx, false));
}

// =====================================================================
//  6. render_smartObject_withFilter
// =====================================================================

void tst_SmartFilter::test_render_smartObject_withFilter()
{
    LayerStack stack;
    // Add a base bitmap first so canvas size is deterministic; SO added on top.
    stack.setBaseLayer(makeCheckerboard(64, 64));   // index 0
    const int soIdx = addNoiseBaseSmartObject(stack); // index 1
    QCOMPARE(soIdx, 1);

    const cv::Mat soBase = stack.rasterizeForRender(soIdx);
    QVERIFY(!soBase.empty());
    QCOMPARE(soBase.size(), cv::Size(64, 64));

    // Render without filter (SO composites onto base bitmap)
    cv::Mat before = stack.render();
    QVERIFY(!before.empty());
    QCOMPARE(before.size(), cv::Size(64, 64));

    // Append GaussianBlur
    const int fIdx = stack.appendSmartFilter(
        soIdx, QStringLiteral("GaussianBlur"), 2.0, soBase);
    QVERIFY(fIdx >= 0);

    // Render with filter
    cv::Mat after = stack.render();
    QVERIFY(!after.empty());
    QCOMPARE(after.size(), cv::Size(64, 64));
    QCOMPARE(after.type(), before.type());

    // Blur should soften high-frequency content; check that mean absolute
    // difference between before and after > 0 (at least some pixels changed).
    cv::Mat diff;
    cv::absdiff(before, after, diff);
    const double sum = cv::sum(diff)[0];
    QVERIFY2(sum > 0.0,
             "Render with GaussianBlur should differ from base render");
}

// =====================================================================
//  7. smartFilterUndo_append
// =====================================================================

void tst_SmartFilter::test_smartFilterUndo_append()
{
    LayerStack stack;
    QUndoStack undo;
    const int soIdx = addNoiseBaseSmartObject(stack);
    const cv::Mat base = stack.rasterizeForRender(soIdx);

    const int fIdx = stack.appendSmartFilter(
        soIdx, QStringLiteral("Sharpen"), 1.0, base);
    QVERIFY(fIdx >= 0);
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.smartFilterCount(soIdx), 1);

    auto *cmd = LayerCommand::makeAppendSmartFilter(
        &stack, fIdx, QStringLiteral("Sharpen"), 1.0);
    QVERIFY(cmd);
    undo.push(cmd);

    // Undo should drop the filter from chain and stack
    undo.undo();
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.smartFilterCount(soIdx), 0);

    // Redo no-op (factory pattern), manually re-add for forward consistency
    stack.appendSmartFilter(soIdx, QStringLiteral("Sharpen"), 1.0, base);
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.smartFilterCount(soIdx), 1);
}

// =====================================================================
//  8. smartFilterUndo_remove
// =====================================================================

void tst_SmartFilter::test_smartFilterUndo_remove()
{
    LayerStack stack;
    QUndoStack undo;
    const int soIdx = addNoiseBaseSmartObject(stack);
    const cv::Mat base = stack.rasterizeForRender(soIdx);

    const int fIdx = stack.appendSmartFilter(
        soIdx, QStringLiteral("Brightness"), 0.7, base);
    QVERIFY(fIdx >= 0);
    QCOMPARE(stack.smartFilterCount(soIdx), 1);

    // Capture filter state for undo
    Layer oldFilter = *stack.at(fIdx);
    QCOMPARE(oldFilter.filterSlotIndex, 0);
    QCOMPARE(oldFilter.parentSmartIndex, soIdx);

    QVERIFY(stack.removeSmartFilter(soIdx, fIdx));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.smartFilterCount(soIdx), 0);

    auto *cmd = LayerCommand::makeRemoveSmartFilter(&stack, fIdx, oldFilter);
    QVERIFY(cmd);
    undo.push(cmd);

    undo.undo();
    // Filter should be back in chain and stack
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.smartFilterCount(soIdx), 1);
    const auto &chain = stack.smartFiltersFor(soIdx);
    QCOMPARE(chain.size(), 1);
    const int newFIdx = chain.first();
    auto restored = stack.at(newFIdx);
    QVERIFY(restored);
    QCOMPARE(restored->kind, Layer::SmartFilter);
    QCOMPARE(restored->filterType, QStringLiteral("Brightness"));
    QCOMPARE(restored->filterStrength, 0.7);
    QCOMPARE(restored->parentSmartIndex, soIdx);
    QCOMPARE(restored->filterSlotIndex, 0);
}

QTEST_MAIN(tst_SmartFilter)
#include "tst_SmartFilter.moc"
