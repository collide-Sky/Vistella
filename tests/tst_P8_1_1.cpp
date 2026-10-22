// tst_P8_1_1.cpp
//
// Unit test for P8.1.1 (2026-10-19): LayerStack -> DAG adapter
// (LayerBlendNode + LayerMaskNode + LayerStackAdapter).

#include <QTest>
#include <QApplication>

#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

using cv::Vec3b;

#include "../src/media/compositor/TileGrid.h"
#include "../src/media/compositor/CompositorNode.h"
#include "../src/media/compositor/CompositorGraph.h"
#include "../src/media/compositor/RenderGraph.h"

#include "../src/media/compositor/nodes/LayerBitmapNode.h"
#include "../src/media/compositor/nodes/LayerBlendNode.h"
#include "../src/media/compositor/nodes/LayerMaskNode.h"
#include "../src/media/compositor/nodes/GroupNode.h"
#include "../src/media/compositor/nodes/LayerRasterNode.h"
#include "../src/media/compositor/nodes/LayerAdjustmentNode.h"
#include "../src/media/compositor/nodes/LayerStackAdapter.h"

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/BlendMode.h"

#include <QDir>
#include <QElapsedTimer>
#include <QPainterPath>
#include <QTemporaryDir>

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
using compositor::LayerStackAdapter;
using compositor::LayerRasterNode;
using compositor::LayerAdjustmentNode;
using compositor::LayerOpacityNode;
using compositor::SolidCanvasNode;
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

    // P8.1.2: LayerStack::renderViaCompositor() parity with
    // LayerStack::render() across a matrix of stack configurations.
    void parity_flagOff_usesLegacyPath();
    void parity_defaultUsesCompositorPath();
    void parity_singleBitmap();
    void parity_twoLayersAllBlendModes();
    void parity_opacityMatrix();
    void parity_maskedLayer();
    void parity_groupWithChildren();
    void parity_emptyStack();
    void parity_allInvisible();
    void parity_canvasGeometry_followsBaseLayer();

    // P8.1.1.x: the remaining LayerKinds. Each pins that the new node
    // reproduces the legacy render() output for that kind.
    void parity_textLayer_overBitmap();
    void parity_vectorLayer_overBitmap();
    void parity_adjustmentLayer_appliesLut();
    void parity_adjustmentLayer_opacityMix();
    void parity_adjustmentLayer_invalidLut_isNoOp();
    void parity_smartObjectLayer_fromFile();
    void parity_smartObject_missingSource_placeholder();
    void parity_smartObject_filterChain_applied();
    void parity_allFiveKinds_together();
    void parity_tileBoundarySplit_seamFree();
    void perf_compositorNotPathologicallySlower();
    void parity_baseLayerOpacity_darkensOverBlack();
    void parity_noBitmapBase_startsFromWhiteCanvas();
    void opacityNode_halvesTowardBlack();
    void solidCanvasNode_fillsEveryTile();
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

// ---------------------------------------------------------------------------
// P8.1.2: renderViaCompositor() parity with the legacy render() path
// ---------------------------------------------------------------------------
namespace {

// Diff two images, returning the max per-channel delta. Returns -1
// when sizes differ.
int maxDelta(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size()) return -1;
    if (a.empty()) return 0;
    int worst = 0;
    for (int y = 0; y < a.rows; ++y) {
        const uchar* ap = a.ptr<uchar>(y);
        const uchar* bp = b.ptr<uchar>(y);
        const int n = a.cols * a.channels();
        for (int x = 0; x < n; ++x) {
            const int d = std::abs(int(ap[x]) - int(bp[x]));
            if (d > worst) worst = d;
        }
    }
    return worst;
}

// Non-uniform base image so blend math is actually exercised.
cv::Mat noisyTile(int w, int h, int seed) {
    cv::Mat m(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            m.at<Vec3b>(y, x) = Vec3b(
                static_cast<uchar>((x * 3 + y * 5 + seed) % 256),
                static_cast<uchar>((y * 7 + x * 2 + seed * 3) % 256),
                static_cast<uchar>((x * 11 + y + seed * 5) % 256));
        }
    }
    return m;
}

}   // namespace

void tst_P8_1_1::parity_flagOff_usesLegacyPath()
{
    // The flag is turned OFF explicitly rather than relying on the
    // default: since P8.2 the default is ON, so a test that only checks
    // "flag is off" would stop testing anything. The legacy per-layer
    // loop stays reachable through this switch and is what the rest of
    // this file compares against, so it must keep working.
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"),
        solidTile(kW, kH, cv::Scalar(10, 20, 30))));
    stack.setCompositorEnabled(false);
    QVERIFY(!stack.compositorEnabled());

    const cv::Mat legacy = stack.render();
    const cv::Mat direct = stack.renderViaCompositor();

    // Both must exist and agree (so turning the flag back on is safe),
    // and the flag must still report off after rendering.
    QCOMPARE(legacy.size(), direct.size());
    QVERIFY(maxDelta(legacy, direct) <= 1);
    QVERIFY(!stack.compositorEnabled());
}

void tst_P8_1_1::parity_defaultUsesCompositorPath()
{
    // P8.2: a default-constructed LayerStack renders through the
    // compositor. This is the test that would catch the default being
    // flipped back, or render() quietly ignoring the flag.
    LayerStack stack;
    QVERIFY(stack.compositorEnabled());

    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 31)));
    stack.addTextLayer(QString("caption"), QString("P8.2"), 32,
                       QColor(255, 255, 255));

    // render() (flag on) must equal an explicit compositor render...
    QCOMPARE(maxDelta(stack.render(), stack.renderViaCompositor()), 0);

    // ...and must equal the legacy loop with the flag off. That is the
    // whole point of the flip: users must not see a different picture.
    stack.setCompositorEnabled(false);
    const cv::Mat legacy = stack.render();
    stack.setCompositorEnabled(true);
    QCOMPARE(maxDelta(legacy, stack.render()), 0);
}

void tst_P8_1_1::parity_singleBitmap()
{
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 3)));
    QCOMPARE(maxDelta(stack.render(), stack.renderViaCompositor()), 0);
}

void tst_P8_1_1::parity_twoLayersAllBlendModes()
{
    const Layer::BlendMode modes[] = {
        Layer::Normal, Layer::Multiply, Layer::Screen,
        Layer::Overlay, Layer::SoftLight, Layer::HardLight,
        Layer::ColorDodge, Layer::ColorBurn,
        Layer::Darken, Layer::Lighten,
        Layer::Difference, Layer::Exclusion,
    };
    for (Layer::BlendMode m : modes) {
        LayerStack stack;
        stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 1)));
        Layer top = makeBitmapLayer(QString("top"), noisyTile(kW, kH, 40));
        top.blend = m;
        stack.addLayer(top);

        const int d = maxDelta(stack.render(), stack.renderViaCompositor());
        QVERIFY2(d >= 0 && d <= 1,
                 qPrintable(QStringLiteral("blend mode %1 diverged by %2")
                                .arg(int(m)).arg(d)));
    }
}

void tst_P8_1_1::parity_opacityMatrix()
{
    const float ops[] = { 0.0f, 0.15f, 0.5f, 0.85f, 1.0f };
    for (float op : ops) {
        LayerStack stack;
        stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 9)));
        Layer top = makeBitmapLayer(QString("top"), noisyTile(kW, kH, 77));
        top.opacity = op;
        stack.addLayer(top);

        const int d = maxDelta(stack.render(), stack.renderViaCompositor());
        QVERIFY2(d >= 0 && d <= 1,
                 qPrintable(QStringLiteral("opacity %1 diverged by %2")
                                .arg(op).arg(d)));
    }
}

void tst_P8_1_1::parity_maskedLayer()
{
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 5)));

    Layer masked = makeBitmapLayer(QString("masked"), noisyTile(kW, kH, 88));
    // Horizontal gradient mask: 0 on the left, 255 on the right.
    masked.layerMask = cv::Mat(kH, kW, CV_8UC1);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            masked.layerMask.at<uchar>(y, x) =
                static_cast<uchar>(x * 255 / (kW - 1));
        }
    }
    masked.maskEnabled = true;
    stack.addLayer(masked);

    const int d = maxDelta(stack.render(), stack.renderViaCompositor());
    QVERIFY2(d >= 0 && d <= 2, qPrintable(QStringLiteral("mask diverged by %1").arg(d)));
}

void tst_P8_1_1::parity_groupWithChildren()
{
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 11)));

    Layer c1 = makeBitmapLayer(QString("c1"), noisyTile(kW, kH, 22));
    c1.blend = Layer::Multiply;
    c1.opacity = 0.7f;
    Layer c2 = makeBitmapLayer(QString("c2"), noisyTile(kW, kH, 33));
    c2.blend = Layer::Screen;
    stack.addLayer(c1);
    stack.addLayer(c2);
    const int groupIdx = stack.mergeIntoGroup({1, 2});
    QVERIFY(groupIdx >= 0);
    stack.setOpacity(groupIdx, 0.8f);

    Layer top = makeBitmapLayer(QString("top"), noisyTile(kW, kH, 44));
    top.blend = Layer::Overlay;
    stack.addLayer(top);

    const int d = maxDelta(stack.render(), stack.renderViaCompositor());
    QVERIFY2(d >= 0 && d <= 2, qPrintable(QStringLiteral("group diverged by %1").arg(d)));
}

void tst_P8_1_1::parity_emptyStack()
{
    LayerStack stack;
    const cv::Mat legacy = stack.render();
    const cv::Mat direct = stack.renderViaCompositor();
    // Legacy returns an empty Mat for an empty stack.
    QVERIFY(legacy.empty());
    // The compositor path mirrors that (nothing renderable -> the
    // white canvasSize fallback is only for the "all non-bitmap"
    // case; an empty stack has no layers at all).
    QVERIFY(direct.empty() || maxDelta(legacy, direct) <= 1);
}

void tst_P8_1_1::parity_allInvisible()
{
    // Legacy asymmetry, mirrored on purpose rather than "fixed".
    //
    // LayerStack::render() picks the base layer with a loop that checks
    // only `kind == Bitmap && !image.empty()` — never `visible`. The
    // compositing loop that follows *does* skip invisible layers. So a
    // fully hidden stack still renders its base layer's image.
    //
    // LayerStackAdapter::buildRange() reproduces this via
    // `forcedVisibleIdx`: the base index is exempt from the visibility
    // filter. Changing either side alone would silently diverge the two
    // render paths.
    LayerStack stack;
    Layer a = makeBitmapLayer(QString("a"), noisyTile(kW, kH, 2));
    a.visible = false;
    Layer b = makeBitmapLayer(QString("b"), noisyTile(kW, kH, 4));
    b.visible = false;
    stack.addLayer(a);
    stack.addLayer(b);

    const cv::Mat legacy = stack.render();
    const cv::Mat direct = stack.renderViaCompositor();

    // Both paths return the hidden base layer, unchanged.
    QVERIFY(!legacy.empty());
    QVERIFY(!direct.empty());
    QCOMPARE(legacy.size(), cv::Size(kW, kH));
    QCOMPARE(maxDelta(legacy, direct), 0);
    // Pin WHICH hidden layer wins (the first, not the last).
    QCOMPARE(maxDelta(a.image, legacy), 0);
    QCOMPARE(maxDelta(a.image, direct), 0);
}

void tst_P8_1_1::parity_canvasGeometry_followsBaseLayer()
{
    // Two different rules for "canvas size" exist in LayerStack and
    // they disagree whenever index 0 is not the compositing base:
    //   canvasSize()      -> index 0's image size (800x600 fallback)
    //   render()          -> first Bitmap-with-image layer's size
    // The adapter resolves the base itself (it has to, to pick the
    // compositing root) and publishes the result via Result::canvasSize,
    // so renderViaCompositor() must not re-derive it from a second copy
    // of the rule.
    LayerStack stack;
    // A text layer at index 0 has no image, so canvasSize() would fall
    // back to 800x600 while render() takes the bitmap's dimensions.
    stack.addTextLayer(QString("caption"), QString("hi"));
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 6)));

    const cv::Size legacySz = stack.render().size();
    const cv::Size directSz = stack.renderViaCompositor().size();

    QCOMPARE(legacySz, cv::Size(kW, kH));
    QCOMPARE(directSz, legacySz);
}

// ---------------------------------------------------------------------------
// P8.1.1.x: Text / Vector / SmartObject / Adjustment parity
// ---------------------------------------------------------------------------
namespace {

// Render both paths and assert they agree, with the stack echoed in the
// failure message so a mismatch names the configuration that broke.
void requireParity(LayerStack& stack, const char* what, int tolerance = 2) {
    const cv::Mat legacy = stack.render();
    const cv::Mat direct = stack.renderViaCompositor();
    if (legacy.empty() || direct.empty()) {
        QVERIFY2(legacy.empty() && direct.empty(),
                 qPrintable(QStringLiteral("%1: one path returned empty and "
                                          "the other did not (legacy %2x%3, "
                                          "direct %4x%5)")
                                .arg(QString::fromLatin1(what))
                                .arg(legacy.cols).arg(legacy.rows)
                                .arg(direct.cols).arg(direct.rows)));
        return;
    }
    const int d = maxDelta(legacy, direct);
    QVERIFY2(d >= 0 && d <= tolerance,
             qPrintable(QStringLiteral("%1: diverged by %2")
                            .arg(QString::fromLatin1(what)).arg(d)));
}

// A 256x1 LUT that is obvious at a glance: everything above 128 inverts.
cv::Mat invertHighLut() {
    cv::Mat lut(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = static_cast<uchar>(i > 128 ? 255 - i : i);
    }
    return lut;
}

// Identity LUT, for isolating the opacity mix from the LUT math.
cv::Mat identityLut() {
    cv::Mat lut(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) lut.at<uchar>(i) = static_cast<uchar>(i);
    return lut;
}

// A rectangle covering roughly the middle third of the 0..100 space that
// LayerStack::rasterize() scales vector paths into.
QPainterPath midRectPath() {
    QPainterPath p;
    p.addRect(20, 20, 60, 60);
    return p;
}

}   // namespace

void tst_P8_1_1::parity_textLayer_overBitmap()
{
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 12)));
    stack.addTextLayer(QString("caption"), QString("P8.1.1.x"), 40,
                       QColor(255, 255, 255));
    requireParity(stack, "text over bitmap");

    // Same again with a blend mode + partial opacity, so the test covers
    // the whole Text -> mask/blend wiring and not just rasterization.
    LayerStack blended;
    blended.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 12)));
    blended.addTextLayer(QString("caption"), QString("P8.1.1.x"), 40,
                         QColor(0, 200, 255));
    blended.setOpacity(1, 0.65f);
    blended.setBlend(1, Layer::Screen);
    requireParity(blended, "text over bitmap, Screen 0.65");
}

void tst_P8_1_1::parity_vectorLayer_overBitmap()
{
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 13)));
    stack.addVectorLayer(QString("shape"),
                         QVector<QPainterPath>{ midRectPath() },
                         QVector<QColor>{ QColor(200, 40, 40) });
    requireParity(stack, "vector over bitmap");

    LayerStack stroked;
    stroked.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 13)));
    stroked.addVectorLayer(QString("shape"),
                           QVector<QPainterPath>{ midRectPath() },
                           QVector<QColor>{ QColor(20, 220, 20) });
    stroked.setOpacity(1, 0.4f);
    stroked.setBlend(1, Layer::Multiply);
    requireParity(stroked, "vector over bitmap, Multiply 0.4");
}

void tst_P8_1_1::parity_adjustmentLayer_appliesLut()
{
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 14)));
    stack.addAdjustmentLayer(QString("invert"), QString("custom"),
                             invertHighLut());
    requireParity(stack, "adjustment LUT", 1);

    // Guard against a vacuous pass: the LUT must actually change pixels,
    // otherwise this test would also pass if the node were a no-op.
    const cv::Mat legacy = stack.render();
    QCOMPARE(maxDelta(noisyTile(kW, kH, 14), legacy) > 0, true);
}

void tst_P8_1_1::parity_adjustmentLayer_opacityMix()
{
    // Identity LUT isolates the opacity mix: with an identity LUT,
    // addWeighted(canvas, 1-op, canvas, op, 0) is exactly canvas, so
    // legacy and compositor must agree exactly (delta 0) at any opacity.
    const float ops[] = { 0.25f, 0.5f, 0.75f, 1.0f };
    for (float op : ops) {
        LayerStack stack;
        stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 15)));
        stack.addAdjustmentLayer(QString("id"), QString("custom"), identityLut());
        stack.setOpacity(1, op);
        QCOMPARE(maxDelta(stack.render(), stack.renderViaCompositor()), 0);
    }

    // Non-identity LUT at partial opacity exercises the real blend.
    LayerStack mixed;
    mixed.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 15)));
    mixed.addAdjustmentLayer(QString("invert"), QString("custom"),
                             invertHighLut());
    mixed.setOpacity(1, 0.5f);
    requireParity(mixed, "adjustment LUT @0.5", 1);
}

void tst_P8_1_1::parity_adjustmentLayer_invalidLut_isNoOp()
{
    // cv::LUT only accepts a 256x1 CV_8U table. The legacy branch guards
    // all three conditions and skips the layer when any fails; the node
    // bakes the same guard into hasUsableLut(). A wrong-shaped LUT must
    // leave the canvas alone on both paths.
    const cv::Mat badLuts[] = {
        cv::Mat(128, 1, CV_8U),              // too short
        cv::Mat(256, 1, CV_32F),             // wrong depth
        cv::Mat(256, 3, CV_8UC3),            // not single-channel
    };
    for (const auto& bad : badLuts) {
        LayerStack stack;
        stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 16)));
        stack.addAdjustmentLayer(QString("bad"), QString("custom"), bad);
        const cv::Mat direct = stack.renderViaCompositor();
        QCOMPARE(maxDelta(noisyTile(kW, kH, 16), direct), 0);
        requireParity(stack, "adjustment with unusable LUT");
    }
}

void tst_P8_1_1::parity_smartObjectLayer_fromFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("smart_src.png"));
    // Write something non-uniform so a resize or a wrong source would show.
    QVERIFY(cv::imwrite(src.toStdString(), noisyTile(kW, kH, 17)));

    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 18)));
    stack.addSmartObjectLayer(QString("smart"), src, /*embed=*/false);
    requireParity(stack, "smartObject from file");

    // And with a blend + opacity on top of the default Normal 1.0.
    LayerStack blended;
    blended.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 18)));
    blended.addSmartObjectLayer(QString("smart"), src, /*embed=*/false);
    blended.setOpacity(1, 0.55f);
    blended.setBlend(1, Layer::Overlay);
    requireParity(blended, "smartObject, Overlay 0.55");
}

void tst_P8_1_1::parity_smartObject_missingSource_placeholder()
{
    // A missing source yields the grey "MISSING" placeholder rather than
    // an empty Mat, so the layer still composites.
    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 19)));
    stack.addSmartObjectLayer(QString("smart"),
                              QStringLiteral("Z:/definitely/not/here.png"),
                              /*embed=*/false);
    requireParity(stack, "smartObject missing source");
}

void tst_P8_1_1::parity_smartObject_filterChain_applied()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("smart_filt.png"));
    QVERIFY(cv::imwrite(src.toStdString(), noisyTile(kW, kH, 20)));

    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 21)));
    const int smart = stack.addSmartObjectLayer(QString("smart"), src,
                                                /*embed=*/false);
    QVERIFY(smart >= 0);

    // GaussianBlur is the case that forced the chain to stay inside
    // LayerRasterNode: its kernel reaches outside a single tile, so a
    // per-tile implementation would seam at every tile border.
    QVERIFY(stack.appendSmartFilter(smart, QStringLiteral("GaussianBlur"),
                                    1.0, stack.rasterizeForRender(smart)) >= 0);
    requireParity(stack, "smartObject + GaussianBlur");

    // Chain of two, with the first disabled: disabled filters must be
    // skipped, not applied at zero strength.
    LayerStack chain;
    chain.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 21)));
    const int s2 = chain.addSmartObjectLayer(QString("smart"), src, false);
    const int f1 = chain.appendSmartFilter(s2, QStringLiteral("GaussianBlur"),
                                           1.5, chain.rasterizeForRender(s2));
    const int f2 = chain.appendSmartFilter(s2, QStringLiteral("Brightness"),
                                           0.5, chain.rasterizeForRender(s2));
    QVERIFY(f1 >= 0);
    QVERIFY(f2 >= 0);
    QVERIFY(chain.setSmartFilterEnabled(s2, f1, /*enabled=*/false));
    requireParity(chain, "smartObject, one disabled + one active filter");
}

void tst_P8_1_1::parity_allFiveKinds_together()
{
    // One stack exercising every kind the compositor now covers, in the
    // z-order the legacy loop would walk them.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("smart_mix.png"));
    QVERIFY(cv::imwrite(src.toStdString(), noisyTile(kW, kH, 22)));

    LayerStack stack;
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 23)));

    const int smart = stack.addSmartObjectLayer(QString("smart"), src, false);
    QVERIFY(stack.appendSmartFilter(smart, QStringLiteral("Sharpen"),
                                    0.8, stack.rasterizeForRender(smart)) >= 0);

    stack.addVectorLayer(QString("shape"),
                         QVector<QPainterPath>{ midRectPath() },
                         QVector<QColor>{ QColor(180, 180, 30) });

    stack.addTextLayer(QString("caption"), QString("P8"), 36,
                       QColor(255, 255, 255));

    stack.addAdjustmentLayer(QString("invert"), QString("custom"),
                             invertHighLut());

    requireParity(stack, "all five kinds", 2);

    // Same stack with the adjustment at partial opacity and a group
    // folded in, to catch wiring mistakes that only show once more than
    // two nodes sit between the base and the output.
    LayerStack groupy;
    groupy.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 23)));
    groupy.addVectorLayer(QString("s1"),
                          QVector<QPainterPath>{ midRectPath() },
                          QVector<QColor>{ QColor(180, 180, 30) });
    groupy.addTextLayer(QString("caption"), QString("P8"), 36,
                        QColor(255, 255, 255));
    const int g = groupy.mergeIntoGroup({1, 2});
    QVERIFY(g >= 0);
    groupy.setOpacity(g, 0.7f);
    groupy.addAdjustmentLayer(QString("invert"), QString("custom"),
                              invertHighLut());
    groupy.setOpacity(3, 0.8f);
    requireParity(groupy, "group + adjustment + opacity", 2);
}

void tst_P8_1_1::parity_tileBoundarySplit_seamFree()
{
    // The render graph tiles the canvas. A node that slices per tile must
    // reassemble into the same image the legacy single-pass render
    // produces, including on the partial edge tiles — this is what
    // catches an off-by-one in the tile slicing math.
    //
    // A canvas wider than one tile also exercises the multi-tile path,
    // which a 128x128 single-tile canvas never touches.
    LayerStack stack;
    const cv::Mat base = noisyTile(kW * 2 + 37, kH * 2 + 19, 24);
    stack.addLayer(makeBitmapLayer(QString("base"), base));
    stack.addTextLayer(QString("caption"), QString("TILE"), 44,
                       QColor(255, 255, 255));
    stack.addAdjustmentLayer(QString("invert"), QString("custom"),
                             invertHighLut());

    // Same stack rendered by the compositor but with an explicitly tiny
    // tile size, so the slicing runs many times over the same image.
    LayerStackAdapter::Result built = LayerStackAdapter::build(stack);
    QVERIFY(built.outputNode != 0);
    TileGrid fine(kW * 2 + 37, kH * 2 + 19, /*tileSize=*/32);
    RenderGraph rg(&built.graph, built.outputNode, &fine);
    const cv::Mat fineRendered = rg.execute();

    const cv::Mat legacy = stack.render();
    QCOMPARE(fineRendered.size(), legacy.size());
    QCOMPARE(maxDelta(legacy, fineRendered), 0);
}

void tst_P8_1_1::perf_compositorNotPathologicallySlower()
{
    // P8.2 made the compositor the default render path, so its cost is
    // now on every render. The DAG rebuilds per render() call and slices
    // the canvas into tiles, which the legacy single-pass loop does not,
    // so this measures the gap on a realistically sized stack.
    //
    // The assertion is deliberately loose (8x). A per-pixel cost
    // difference of this kind is machine- and build-dependent and would
    // make a tight bound flaky. The point is to catch an accidental
    // O(tiles * layers^2) or a per-tile re-rasterize — a regression that
    // shows up as 100x, not 2x. The measured numbers are printed so a
    // real slowdown is visible in the log rather than silently tolerated.
    LayerStack stack;
    const int W = 2048, H = 1536;   // ~3 MP, a large but ordinary photo
    stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(W, H, 41)));
    for (int i = 0; i < 5; ++i) {
        Layer top = makeBitmapLayer(QString("layer%1").arg(i),
                                    noisyTile(W, H, 50 + i));
        top.blend = (i % 2) ? Layer::Multiply : Layer::Screen;
        top.opacity = 0.6f;
        stack.addLayer(top);
    }
    stack.addTextLayer(QString("caption"), QString("BENCH"), 96,
                       QColor(255, 255, 255));
    stack.addAdjustmentLayer(QString("invert"), QString("custom"),
                             invertHighLut());

    // Warm up: the first compositor render pays one-off allocation and
    // OpenCV lazy-init costs that would otherwise land on the legacy run.
    stack.renderViaCompositor();

    const int kRuns = 3;
    QElapsedTimer t;

    t.start();
    for (int i = 0; i < kRuns; ++i) stack.setCompositorEnabled(false), stack.render();
    const qint64 legacyMs = t.elapsed();

    t.start();
    for (int i = 0; i < kRuns; ++i) stack.setCompositorEnabled(true), stack.render();
    const qint64 compositorMs = t.elapsed();

    const double legacyAvg = double(legacyMs) / kRuns;
    const double compAvg = double(compositorMs) / kRuns;
    qInfo("render cost on %dx%d, 7 layers: legacy %.1f ms, compositor %.1f ms "
          "(%.2fx)", W, H, legacyAvg, compAvg,
          legacyAvg > 0 ? compAvg / legacyAvg : 0.0);

    QVERIFY2(compAvg <= legacyAvg * 8.0,
             qPrintable(QStringLiteral("compositor render is %1x slower than "
                                      "legacy (%2 ms vs %3 ms)")
                            .arg(legacyAvg > 0 ? compAvg / legacyAvg : 0.0, 0, 'f', 2)
                            .arg(compAvg, 0, 'f', 1)
                            .arg(legacyAvg, 0, 'f', 1)));
}

void tst_P8_1_1::parity_baseLayerOpacity_darkensOverBlack()
{
    // Regression guard for a parity bug the P8.2 flip exposed.
    //
    // The legacy loop treats the compositing base layer differently from
    // every other layer: it seeds the canvas with the base image and then
    // applies `addWeighted(canvas, opacity, zeros, 0, 0)` — compositing it
    // over a BLACK backdrop at its own opacity. Every other layer reaches
    // its opacity through applyBlend(). An earlier adapter version used
    // "base == 0 means this layer is the canvas" and so skipped the step
    // entirely, which tst_LayerStack::test_render_blend caught.
    //
    // Only the base layer gets this treatment, so the second layer here is
    // the control: same opacity, reached the normal way.
    const float ops[] = { 0.25f, 0.5f, 0.75f };
    for (float op : ops) {
        LayerStack stack;
        stack.addLayer(makeBitmapLayer(QString("base"), noisyTile(kW, kH, 60)));
        stack.setOpacity(0, op);
        stack.addLayer(makeBitmapLayer(QString("top"), noisyTile(kW, kH, 61)));
        stack.setOpacity(1, op);
        requireParity(stack, "base layer opacity");
    }

    // A lone translucent base with nothing above it is the minimal case.
    LayerStack lone;
    lone.addLayer(makeBitmapLayer(QString("base"), solidTile(kW, kH,
                                                           cv::Scalar(200, 100, 40))));
    lone.setOpacity(0, 0.5f);
    const cv::Mat legacy = lone.render();
    QCOMPARE(maxDelta(lone.renderViaCompositor(), legacy), 0);
    // Pin the actual expectation, not just agreement: 0.5 over black.
    QCOMPARE(legacy.at<cv::Vec3b>(4, 4), cv::Vec3b(100, 50, 20));
}

void tst_P8_1_1::parity_noBitmapBase_startsFromWhiteCanvas()
{
    // A stack with no Bitmap-with-image layer at all. Legacy falls back
    // to a WHITE canvas of canvasSize() and blends every visible layer
    // onto it; the DAG reproduces that with a SolidCanvasNode root. The
    // earlier adapter instead treated the first visible layer as the
    // canvas, which lost that layer's opacity AND started from black.
    LayerStack stack;
    stack.setCanvasSize(cv::Size(kW, kH));
    stack.addTextLayer(QString("caption"), QString("ONLY"), 40,
                       QColor(0, 0, 0));

    const cv::Mat legacy = stack.render();
    const cv::Mat direct = stack.renderViaCompositor();
    QVERIFY(!legacy.empty());
    QCOMPARE(direct.size(), legacy.size());
    QCOMPARE(maxDelta(legacy, direct), 0);

    // Note the text layer cannot itself prove the root colour: rasterize()
    // drops the alpha channel in its BGRA->BGR conversion, so a
    // transparent background becomes black and the text raster covers
    // the whole canvas. Whatever the root was, Normal blend at opacity 1
    // replaces it outright.
    //
    // An Adjustment layer is the one that reads the root through, so it
    // is the discriminator. The marker LUT maps 255 -> 77 and is the
    // identity elsewhere: a white root yields 77, a black root yields 0.
    cv::Mat marker(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) marker.at<uchar>(i) = static_cast<uchar>(i);
    marker.at<uchar>(255) = 77;

    LayerStack probe;
    probe.setCanvasSize(cv::Size(kW, kH));
    probe.addAdjustmentLayer(QString("marker"), QString("custom"), marker);

    const cv::Mat probedLegacy = probe.render();
    const cv::Mat probedDirect = probe.renderViaCompositor();
    QCOMPARE(maxDelta(probedLegacy, probedDirect), 0);
    QCOMPARE(probedLegacy.at<cv::Vec3b>(2, 2), cv::Vec3b(77, 77, 77));
    QCOMPARE(probedDirect.at<cv::Vec3b>(2, 2), cv::Vec3b(77, 77, 77));
}

void tst_P8_1_1::opacityNode_halvesTowardBlack()
{
    // Unit-level check of the node the base layer relies on, so a break
    // here names the node rather than surfacing as a stack-level mismatch.
    Layer srcLayer = makeBitmapLayer(QString("l"),
                                     solidTile(8, 8, cv::Scalar(200, 100, 40)));
    LayerBitmapNode src(&srcLayer);
    LayerOpacityNode fade(src.id(), 0.5f);

    CompositorGraph g;
    g.addNode(&src);
    g.addNode(&fade);
    g.addEdge(src.id(), fade.id());

    TileGrid grid(8, 8);
    RenderGraph rg(&g, fade.id(), &grid);
    const cv::Mat out = rg.execute();

    QCOMPARE(out.at<cv::Vec3b>(4, 4), cv::Vec3b(100, 50, 20));
}

void tst_P8_1_1::solidCanvasNode_fillsEveryTile()
{
    // Multi-tile canvas with partial edge tiles: every tile must come
    // back solid, including the ones the canvas does not fully cover.
    SolidCanvasNode canvas(cv::Size(70, 50), cv::Scalar(255, 255, 255),
                           QStringLiteral("white"));
    CompositorGraph g;
    g.addNode(&canvas);
    TileGrid grid(70, 50, /*tileSize=*/32);
    RenderGraph rg(&g, canvas.id(), &grid);

    const cv::Mat out = rg.execute();
    QCOMPARE(out.size(), cv::Size(70, 50));
    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            QCOMPARE(out.at<cv::Vec3b>(y, x), cv::Vec3b(255, 255, 255));
        }
    }
}

QTEST_MAIN(tst_P8_1_1)
#include "tst_P8_1_1.moc"
