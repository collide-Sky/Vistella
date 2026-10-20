// tst_P8_1_1.cpp
//
// Unit test for P8.1.1 (2026-10-19): LayerStack -> DAG adapter
// (LayerBlendNode + LayerMaskNode + LayerStackAdapter).

#include <QTest>
#include <QApplication>

#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/imgproc.hpp>

using cv::Vec3b;

#include "../src/media/compositor/TileGrid.h"
#include "../src/media/compositor/CompositorNode.h"
#include "../src/media/compositor/CompositorGraph.h"
#include "../src/media/compositor/RenderGraph.h"

#include "../src/media/compositor/nodes/LayerBitmapNode.h"
#include "../src/media/compositor/nodes/LayerBlendNode.h"
#include "../src/media/compositor/nodes/LayerMaskNode.h"
#include "../src/media/compositor/nodes/GroupNode.h"
#include "../src/media/compositor/nodes/LayerStackAdapter.h"

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/BlendMode.h"

using layers::Layer;
using layers::LayerPtr;
using layers::LayerStack;
using compositor::TileGrid;
using compositor::CompositorGraph;
using compositor::CompositorNode;
using compositor::RenderGraph;
using compositor::LayerBitmapNode;
using compositor::LayerBlendNode;
using compositor::LayerMaskNode;
using compositor::GroupNode;

namespace {
constexpr int kW = 128;
constexpr int kH = 128;

cv::Mat solidTile(int w, int h, cv::Scalar bgr) {
    return cv::Mat(h, w, CV_8UC3, bgr);
}

Layer makeBitmapLayer(const QString& name, const cv::Mat& image) {
    Layer l;
    l.name = name;
    l.kind = Layer::Bitmap;
    l.image = image;
    return l;
}

// Render a 3-node DAG: bitmap(base) -> blend <- bitmap(top).
//   mode is the blend mode; opacity scales the top layer.
static cv::Mat renderBlend(const Layer& baseLayer, const Layer& topLayer,
                           Layer::BlendMode mode, float opacity) {
    TileGrid grid(kW, kH);
    LayerBitmapNode baseNode(&baseLayer);
    LayerBitmapNode topNode(&topLayer);
    LayerBlendNode blend(baseNode.id(), topNode.id(), mode, opacity);
    CompositorGraph g;
    g.addNode(&baseNode);
    g.addNode(&topNode);
    g.addNode(&blend);
    g.addEdge(baseNode.id(), blend.id());
    g.addEdge(topNode.id(),  blend.id());
    RenderGraph rg(&g, blend.id(), &grid);
    return rg.execute();
}

// Render a 2-node DAG: bitmap(base) -> maskNode <- bitmap(mask).
static cv::Mat renderMask(const Layer& baseLayer, const Layer& maskLayer) {
    TileGrid grid(kW, kH);
    LayerBitmapNode baseNode(&baseLayer);
    LayerBitmapNode maskNode(&maskLayer);
    LayerMaskNode mn(baseNode.id(), maskNode.id());
    CompositorGraph g;
    g.addNode(&baseNode);
    g.addNode(&maskNode);
    g.addNode(&mn);
    g.addEdge(baseNode.id(), mn.id());
    g.addEdge(maskNode.id(), mn.id());
    RenderGraph rg(&g, mn.id(), &grid);
    return rg.execute();
}
}   // namespace

class tst_P8_1_1 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // LayerBlendNode
    void blendNode_normal_fullOpacity();
    void blendNode_normal_halfOpacity();
    void blendNode_multiply();
    void blendNode_screen();
    void blendNode_darken();
    void blendNode_lighten();
    void blendNode_difference();
    void blendNode_missingBase_returnsEmpty();
    void blendNode_missingTop_returnsBase();

    // P8.1.1 Phase B: LayerBlendNode delegates to layers::applyBlend,
    // so all 15 modes must match the legacy BlendMode.cpp path.
    void blendNode_allModes_matchLegacyApplyBlend();
    void blendNode_edgeTileSizeMismatch_normalizesGeometry();

    // GroupNode (P8.1.1 Phase B)
    void groupNode_fullOpacity_passesContent();
    void groupNode_halfOpacity_blendsOverBase();
    void groupNode_missingContent_passesBase();
    void groupNode_rootGroup_noBase();

    // LayerMaskNode
    void maskNode_fullMask_passesBase();
    void maskNode_halfMask_attenuates();
    void maskNode_emptyMask_passesBase();
    void maskNode_multichannelMask_takesBChannel();

    // LayerStackAdapter
    void adapter_singleLayer_rendersIdentical();
    void adapter_twoLayerNormal_blendsCorrectly();
    void adapter_invisibleLayer_skipped();
    void adapter_maskedLayer_appliesMask();

    // P8.1.1 Phase B: group support
    void adapter_group_fullOpacity_matchesLegacy();
    void adapter_group_halfOpacity_matchesLegacy();
    void adapter_groupWithBase_matchesLegacy();
};

void tst_P8_1_1::initTestCase() {}
void tst_P8_1_1::cleanupTestCase() {}

// ---------------------------------------------------------------------------
// LayerBlendNode
// ---------------------------------------------------------------------------
void tst_P8_1_1::blendNode_normal_fullOpacity()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(50, 100, 200)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 150, 50)));
    cv::Mat canvas = renderBlend(base, top, Layer::Normal, 1.0f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 100);
    QCOMPARE((int)p[1], 150);
    QCOMPARE((int)p[2], 50);
}

void tst_P8_1_1::blendNode_normal_halfOpacity()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(0, 0, 0)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    cv::Mat canvas = renderBlend(base, top, Layer::Normal, 0.5f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 100);
    QCOMPARE((int)p[1], 100);
    QCOMPARE((int)p[2], 100);
}

void tst_P8_1_1::blendNode_multiply()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    cv::Mat canvas = renderBlend(base, top, Layer::Multiply, 1.0f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QVERIFY(qAbs((int)p[0] - 78) <= 1);
    QVERIFY(qAbs((int)p[1] - 78) <= 1);
    QVERIFY(qAbs((int)p[2] - 78) <= 1);
}

void tst_P8_1_1::blendNode_screen()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    cv::Mat canvas = renderBlend(base, top, Layer::Screen, 1.0f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QVERIFY(qAbs((int)p[0] - 161) <= 1);
}

void tst_P8_1_1::blendNode_darken()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    cv::Mat canvas = renderBlend(base, top, Layer::Darken, 1.0f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 100);
}

void tst_P8_1_1::blendNode_lighten()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    cv::Mat canvas = renderBlend(base, top, Layer::Lighten, 1.0f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 200);
}

void tst_P8_1_1::blendNode_difference()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(80, 80, 80)));
    cv::Mat canvas = renderBlend(base, top, Layer::Difference, 1.0f);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 120);
}

void tst_P8_1_1::blendNode_missingBase_returnsEmpty()
{
    TileGrid grid(kW, kH);
    Layer top = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    LayerBitmapNode topNode(&top);
    LayerBlendNode blend(CompositorNode::NodeId{9999},
                         topNode.id(), Layer::Normal, 1.0f);
    CompositorGraph g;
    g.addNode(&topNode);
    g.addNode(&blend);
    g.addEdge(topNode.id(), blend.id());
    RenderGraph rg(&g, blend.id(), &grid);
    cv::Mat canvas = rg.execute();
    QCOMPARE(canvas.size(), cv::Size(kW, kH));
}

void tst_P8_1_1::blendNode_missingTop_returnsBase()
{
    TileGrid grid(kW, kH);
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(50, 50, 50)));
    LayerBitmapNode baseNode(&base);
    LayerBlendNode blend(baseNode.id(), CompositorNode::NodeId{9999},
                         Layer::Normal, 1.0f);
    CompositorGraph g;
    g.addNode(&baseNode);
    g.addNode(&blend);
    g.addEdge(baseNode.id(), blend.id());
    RenderGraph rg(&g, blend.id(), &grid);
    cv::Mat canvas = rg.execute();
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 50);
}

// ---------------------------------------------------------------------------
// P8.1.1 Phase B: blend-mode parity with layers::applyBlend
// ---------------------------------------------------------------------------
void tst_P8_1_1::blendNode_allModes_matchLegacyApplyBlend()
{
    // LayerBlendNode delegates to layers::applyBlend, so for every
    // mode the compositor output must match the legacy BlendMode.cpp
    // path pixel-for-pixel. This is the regression guard that would
    // catch a future divergence between the two renderers.
    const Layer::BlendMode modes[] = {
        Layer::Normal, Layer::Multiply, Layer::Screen,
        Layer::Overlay, Layer::SoftLight, Layer::HardLight,
        Layer::ColorDodge, Layer::ColorBurn,
        Layer::Darken, Layer::Lighten,
        Layer::Difference, Layer::Exclusion,
        Layer::Hue, Layer::Saturation, Layer::Color, Layer::Luminosity,
    };
    const float opacities[] = { 0.0f, 0.35f, 0.5f, 1.0f };

    for (Layer::BlendMode mode : modes) {
        for (float op : opacities) {
            // Use a non-uniform image so blend math is observable.
            Layer base = makeBitmapLayer(QString("base"),
                cv::Mat(kH, kW, CV_8UC3));
            Layer top = makeBitmapLayer(QString("top"),
                cv::Mat(kH, kW, CV_8UC3));
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    base.image.at<Vec3b>(y, x) = Vec3b(
                        static_cast<uchar>((x * 3 + y) % 256),
                        static_cast<uchar>((y * 5 + 13) % 256),
                        static_cast<uchar>((x * 7 + y * 2) % 256));
                    top.image.at<Vec3b>(y, x) = Vec3b(
                        static_cast<uchar>((255 - (x * 2)) % 256),
                        static_cast<uchar>((255 - y * 3) % 256),
                        static_cast<uchar>((x + y * 11) % 256));
                }
            }

            cv::Mat got = renderBlend(base, top, mode, op);
            cv::Mat want = layers::applyBlend(mode, base.image, top.image, op);
            QCOMPARE(got.size(), want.size());
            for (int y = 0; y < kH; y += 7) {
                for (int x = 0; x < kW; x += 5) {
                    Vec3b a = got.at<Vec3b>(y, x);
                    Vec3b b = want.at<Vec3b>(y, x);
                    QCOMPARE((int)a[0], (int)b[0]);
                    QCOMPARE((int)a[1], (int)b[1]);
                    QCOMPARE((int)a[2], (int)b[2]);
                }
            }
        }
    }
}

void tst_P8_1_1::blendNode_edgeTileSizeMismatch_normalizesGeometry()
{
    // The top layer's image is smaller than the base's. The blend
    // node must normalize both to the grid tile rect and not crash
    // (layers::canBlend requires identical sizes).
    cv::Mat big(kH, kW, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat small(64, 64, CV_8UC3, cv::Scalar(50, 50, 50));
    Layer base = makeBitmapLayer(QString("base"), big);
    Layer top  = makeBitmapLayer(QString("top"), small);
    cv::Mat got = renderBlend(base, top, Layer::Normal, 1.0f);
    QCOMPARE(got.size(), cv::Size(kW, kH));
}

// ---------------------------------------------------------------------------
// GroupNode (P8.1.1 Phase B)
// ---------------------------------------------------------------------------
void tst_P8_1_1::groupNode_fullOpacity_passesContent()
{
    TileGrid grid(kW, kH);
    Layer content = makeBitmapLayer(QString("content"),
        solidTile(kW, kH, cv::Scalar(50, 100, 150)));
    LayerBitmapNode contentNode(&content);
    GroupNode g(0, contentNode.id(), 1.0f);
    CompositorGraph cg;
    cg.addNode(&contentNode);
    cg.addNode(&g);
    cg.addEdge(contentNode.id(), g.id());
    RenderGraph rg(&cg, g.id(), &grid);
    cv::Mat canvas = rg.execute();
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 50);
    QCOMPARE((int)p[1], 100);
    QCOMPARE((int)p[2], 150);
}

void tst_P8_1_1::groupNode_halfOpacity_blendsOverBase()
{
    TileGrid grid(kW, kH);
    Layer baseL = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(0, 0, 0)));
    Layer content = makeBitmapLayer(QString("content"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    LayerBitmapNode baseNode(&baseL);
    LayerBitmapNode contentNode(&content);
    GroupNode g(baseNode.id(), contentNode.id(), 0.5f);
    CompositorGraph cg;
    cg.addNode(&baseNode);
    cg.addNode(&contentNode);
    cg.addNode(&g);
    cg.addEdge(baseNode.id(), g.id());
    cg.addEdge(contentNode.id(), g.id());
    RenderGraph rg(&cg, g.id(), &grid);
    cv::Mat canvas = rg.execute();
    // base=0, content=200, opacity 0.5 -> 100
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 100);
}

void tst_P8_1_1::groupNode_missingContent_passesBase()
{
    TileGrid grid(kW, kH);
    Layer baseL = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(70, 70, 70)));
    LayerBitmapNode baseNode(&baseL);
    GroupNode g(baseNode.id(), CompositorNode::NodeId{9999}, 0.5f);
    CompositorGraph cg;
    cg.addNode(&baseNode);
    cg.addNode(&g);
    cg.addEdge(baseNode.id(), g.id());
    RenderGraph rg(&cg, g.id(), &grid);
    cv::Mat canvas = rg.execute();
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 70);
}

void tst_P8_1_1::groupNode_rootGroup_noBase()
{
    TileGrid grid(kW, kH);
    Layer content = makeBitmapLayer(QString("content"),
        solidTile(kW, kH, cv::Scalar(11, 22, 33)));
    LayerBitmapNode contentNode(&content);
    GroupNode g(0, contentNode.id(), 0.25f);
    CompositorGraph cg;
    cg.addNode(&contentNode);
    cg.addNode(&g);
    cg.addEdge(contentNode.id(), g.id());
    RenderGraph rg(&cg, g.id(), &grid);
    cv::Mat canvas = rg.execute();
    // Root group has no base, so content passes through unchanged
    // regardless of opacity (there's nothing to composite over).
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 11);
}

// ---------------------------------------------------------------------------
// LayerMaskNode
// ---------------------------------------------------------------------------
void tst_P8_1_1::maskNode_fullMask_passesBase()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    cv::Mat mask(kH, kW, CV_8UC1, cv::Scalar(255));
    Layer maskLayer = makeBitmapLayer(QString("mask"), mask);
    cv::Mat canvas = renderMask(base, maskLayer);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 100);
}

void tst_P8_1_1::maskNode_halfMask_attenuates()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    cv::Mat mask(kH, kW, CV_8UC1, cv::Scalar(128));
    Layer maskLayer = makeBitmapLayer(QString("mask"), mask);
    cv::Mat canvas = renderMask(base, maskLayer);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QVERIFY(qAbs((int)p[0] - 100) <= 2);
}

void tst_P8_1_1::maskNode_emptyMask_passesBase()
{
    TileGrid grid(kW, kH);
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(80, 80, 80)));
    LayerBitmapNode baseNode(&base);
    LayerMaskNode mn(baseNode.id(), CompositorNode::NodeId{9999});
    CompositorGraph g;
    g.addNode(&baseNode);
    g.addNode(&mn);
    g.addEdge(baseNode.id(), mn.id());
    RenderGraph rg(&g, mn.id(), &grid);
    cv::Mat canvas = rg.execute();
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 80);
}

void tst_P8_1_1::maskNode_multichannelMask_takesBChannel()
{
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    cv::Mat mask3(kH, kW, CV_8UC3, cv::Scalar(128, 0, 0));
    Layer maskLayer = makeBitmapLayer(QString("mask"), mask3);
    cv::Mat canvas = renderMask(base, maskLayer);
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QVERIFY(qAbs((int)p[0] - 100) <= 2);
}

// ---------------------------------------------------------------------------
// LayerStackAdapter
// ---------------------------------------------------------------------------
static cv::Mat legacyRender(LayerStack& stack, int baseW, int baseH) {
    cv::Mat acc(baseH, baseW, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int i = 0; i < stack.count(); ++i) {
        LayerPtr l = stack.at(i);
        if (!l || !l->visible) continue;
        if (l->kind != Layer::Bitmap) continue;
        const float a = l->opacity;
        if (a >= 0.999f) {
            acc = l->image.clone();
        } else {
            cv::addWeighted(acc, 1.0f - a, l->image, a, 0.0, acc);
        }
    }
    return acc;
}

void tst_P8_1_1::adapter_singleLayer_rendersIdentical()
{
    LayerStack stack;
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(50, 100, 200)));
    stack.addLayer(base);

    auto result = compositor::LayerStackAdapter::build(stack);
    QVERIFY(result.outputNode != 0);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat dag = rg.execute();
    cv::Mat ref = legacyRender(stack, kW, kH);
    QCOMPARE(dag.size(), ref.size());
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            Vec3b a = dag.at<Vec3b>(y, x);
            Vec3b b = ref.at<Vec3b>(y, x);
            QCOMPARE((int)a[0], (int)b[0]);
            QCOMPARE((int)a[1], (int)b[1]);
            QCOMPARE((int)a[2], (int)b[2]);
        }
    }
}

void tst_P8_1_1::adapter_twoLayerNormal_blendsCorrectly()
{
    LayerStack stack;
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(50, 100, 200)));
    Layer top  = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 150, 50)));
    top.blend = Layer::Normal;
    top.opacity = 0.5f;
    stack.addLayer(base);
    stack.addLayer(top);

    auto result = compositor::LayerStackAdapter::build(stack);
    QVERIFY(result.outputNode != 0);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat dag = rg.execute();
    cv::Mat ref = legacyRender(stack, kW, kH);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            Vec3b a = dag.at<Vec3b>(y, x);
            Vec3b b = ref.at<Vec3b>(y, x);
            QVERIFY(qAbs((int)a[0] - (int)b[0]) <= 1);
            QVERIFY(qAbs((int)a[1] - (int)b[1]) <= 1);
            QVERIFY(qAbs((int)a[2] - (int)b[2]) <= 1);
        }
    }
}

void tst_P8_1_1::adapter_invisibleLayer_skipped()
{
    LayerStack stack;
    Layer base   = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(50, 100, 200)));
    Layer hidden = makeBitmapLayer(QString("hidden"),
        solidTile(kW, kH, cv::Scalar(255, 0, 0)));
    hidden.visible = false;
    Layer top    = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(100, 150, 50)));
    stack.addLayer(base);
    stack.addLayer(hidden);
    stack.addLayer(top);

    auto result = compositor::LayerStackAdapter::build(stack);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat canvas = rg.execute();
    Vec3b p = canvas.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 100);
    QCOMPARE((int)p[1], 150);
    QCOMPARE((int)p[2], 50);
}

void tst_P8_1_1::adapter_maskedLayer_appliesMask()
{
    LayerStack stack;
    Layer base   = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    Layer masked = makeBitmapLayer(QString("masked"),
        solidTile(kW, kH, cv::Scalar(50, 50, 50)));
    masked.layerMask = cv::Mat(kH, kW, CV_8UC1, cv::Scalar(128));
    masked.maskEnabled = true;
    stack.addLayer(base);
    stack.addLayer(masked);

    auto result = compositor::LayerStackAdapter::build(stack);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat dag = rg.execute();
    // The mask (128 = 50%) attenuates the top layer's color to 50*0.5=25.
    // Normal blend + opacity 1.0 -> out = (masked * mask) = 25.
    Vec3b p = dag.at<Vec3b>(kH/2, kW/2);
    QCOMPARE((int)p[0], 25);
}

// ---------------------------------------------------------------------------
// P8.1.1 Phase B: group support in the adapter
// ---------------------------------------------------------------------------
void tst_P8_1_1::adapter_group_fullOpacity_matchesLegacy()
{
    // A group containing two bitmap children at full opacity must
    // render identically to the legacy LayerStack::render().
    LayerStack stack;
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(100, 100, 100)));
    stack.addLayer(base);

    // Build a group with two children via mergeIntoGroup.
    Layer c1 = makeBitmapLayer(QString("c1"),
        solidTile(kW, kH, cv::Scalar(150, 150, 150)));
    Layer c2 = makeBitmapLayer(QString("c2"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    stack.addLayer(c1);
    stack.addLayer(c2);
    const int groupIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(groupIdx >= 0);

    auto result = compositor::LayerStackAdapter::build(stack);
    QVERIFY(result.outputNode != 0);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat dag = rg.execute();
    cv::Mat ref = stack.render();
    QCOMPARE(dag.size(), ref.size());
    for (int y = 0; y < kH; y += 3) {
        for (int x = 0; x < kW; x += 3) {
            Vec3b a = dag.at<Vec3b>(y, x);
            Vec3b b = ref.at<Vec3b>(y, x);
            QVERIFY2(qAbs((int)a[0] - (int)b[0]) <= 1, "group mismatch (B)");
            QVERIFY2(qAbs((int)a[1] - (int)b[1]) <= 1, "group mismatch (G)");
            QVERIFY2(qAbs((int)a[2] - (int)b[2]) <= 1, "group mismatch (R)");
        }
    }
}

void tst_P8_1_1::adapter_group_halfOpacity_matchesLegacy()
{
    // Same shape, but the group's opacity is 0.5, so the composite
    // result is 50% group + 50% canvas.
    LayerStack stack;
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(0, 0, 0)));
    stack.addLayer(base);

    // mergeIntoGroup needs >= 2 adjacent layers.
    Layer c1 = makeBitmapLayer(QString("c1"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    Layer c2 = makeBitmapLayer(QString("c2"),
        solidTile(kW, kH, cv::Scalar(200, 200, 200)));
    stack.addLayer(c1);
    stack.addLayer(c2);
    const int groupIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(groupIdx >= 0);
    // Set the group's opacity.
    stack.setOpacity(groupIdx, 0.5f);

    auto result = compositor::LayerStackAdapter::build(stack);
    QVERIFY(result.outputNode != 0);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat dag = rg.execute();
    cv::Mat ref = stack.render();
    Vec3b a = dag.at<Vec3b>(kH/2, kW/2);
    Vec3b b = ref.at<Vec3b>(kH/2, kW/2);
    QVERIFY(qAbs((int)a[0] - (int)b[0]) <= 1);
    QVERIFY(qAbs((int)a[1] - (int)b[1]) <= 1);
    QVERIFY(qAbs((int)a[2] - (int)b[2]) <= 1);
}

void tst_P8_1_1::adapter_groupWithBase_matchesLegacy()
{
    // A group sandwiched between two visible bitmap layers: the
    // group composites between them, honoring per-child blend modes.
    LayerStack stack;
    Layer base = makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(60, 60, 60)));
    stack.addLayer(base);

    Layer c1 = makeBitmapLayer(QString("c1"),
        solidTile(kW, kH, cv::Scalar(120, 120, 120)));
    Layer c2 = makeBitmapLayer(QString("c2"),
        solidTile(kW, kH, cv::Scalar(140, 140, 140)));
    stack.addLayer(c1);
    stack.addLayer(c2);
    const int groupIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(groupIdx >= 0);

    Layer top = makeBitmapLayer(QString("top"),
        solidTile(kW, kH, cv::Scalar(180, 180, 180)));
    top.blend = Layer::Multiply;
    top.opacity = 0.75f;
    stack.addLayer(top);

    auto result = compositor::LayerStackAdapter::build(stack);
    QVERIFY(result.outputNode != 0);
    TileGrid grid(kW, kH);
    RenderGraph rg(&result.graph, result.outputNode, &grid);
    cv::Mat dag = rg.execute();
    cv::Mat ref = stack.render();
    QCOMPARE(dag.size(), ref.size());
    for (int y = 0; y < kH; y += 3) {
        for (int x = 0; x < kW; x += 3) {
            Vec3b a = dag.at<Vec3b>(y, x);
            Vec3b b = ref.at<Vec3b>(y, x);
            QVERIFY(qAbs((int)a[0] - (int)b[0]) <= 1);
            QVERIFY(qAbs((int)a[1] - (int)b[1]) <= 1);
            QVERIFY(qAbs((int)a[2] - (int)b[2]) <= 1);
        }
    }
}

QTEST_MAIN(tst_P8_1_1)
#include "tst_P8_1_1.moc"
