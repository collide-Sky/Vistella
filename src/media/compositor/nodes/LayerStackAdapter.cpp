#include "LayerStackAdapter.h"

#include "../../imageworker/layers/LayerMaskRenderer.h"
#include "../../imageworker/layers/PixelFormat.h"

#include "../TileGrid.h"

#include "../../imageworker/layers/LayerStack.h"
#include "../../imageworker/layers/Layer.h"
#include "../../imageworker/layers/BlendMode.h"

#include <opencv2/imgproc.hpp>   // cv::resize (mask normalization)

#include <QSize>                // QSize (蒙版覆盖度归一化用的画布尺寸)

#include <algorithm>             // std::min

namespace compositor {

namespace {

// Adapter-private mask source: a CompositorNode that yields a
// single-channel cv::Mat — the mask's **coverage** — from a Layer's
// LayerMask. Lives in the adapter's Result so its lifetime matches the graph.
//
// The coverage is folded in one place, by LayerMaskRenderer::buildAlpha()
// (the single entry point): density / feather / invert are all handled
// there, and the mask is normalised to the canvas geometry as well -- a
// 16x16 brush mask on a 32x32 layer is scaled up, and slicing the
// original directly would leave everything outside the mask footprints
// fully masked.
//
// Then it is sliced per tile, the same way LayerBitmapNode does it for
// the colour layers.
// The normalised result is memoised, so an undersized brush mask is
// rescaled only once per render.
class MaskBitmapNode : public CompositorNode {
public:
    explicit MaskBitmapNode(const layers::Layer* layer, QString name)
        : m_layer(layer), m_name(std::move(name)) {}
    QList<NodeId> inputNodes() const override { return {}; }
    cv::Mat compute(int tx, int ty,
                    const TileGrid& grid,
                    RenderContext& /*ctx*/) override;
    QString name() const override { return m_name; }
private:
    const cv::Mat& normalizedMask(const cv::Size& canvas);

    const layers::Layer* m_layer;
    QString              m_name;
    cv::Mat              m_normalized;
    cv::Size             m_normalizedFor;
};

}   // namespace

const cv::Mat& MaskBitmapNode::normalizedMask(const cv::Size& canvas)
{
    if (m_normalizedFor == canvas && !m_normalized.empty()) return m_normalized;

    // The single mask model: Layer::mask -> coverage (grayscale 8UC1).
    cv::Mat coverage = layers::LayerMaskRenderer::buildAlpha(
        m_layer->mask, QSize(canvas.width, canvas.height));
    if (coverage.empty() || coverage.size() != canvas) {
        coverage = cv::Mat(canvas, CV_8UC1, cv::Scalar(255));
    }
    m_normalized = coverage;
    m_normalizedFor = canvas;
    return m_normalized;
}

cv::Mat MaskBitmapNode::compute(int tileX, int tileY,
                                const TileGrid& grid,
                                RenderContext& /*ctx*/)
{
    if (!m_layer || !m_layer->mask.isActive()) return cv::Mat();

    const cv::Rect r = grid.tileRect(tileX, tileY);
    if (r.width <= 0 || r.height <= 0) return cv::Mat();

    const cv::Mat& full = normalizedMask(
        cv::Size(grid.imageWidth(), grid.imageHeight()));

    // The mask now matches the canvas, so tiles outside it can only come
    // from a grid larger than the mask; zero (= masked out) is the safe
    // answer there, matching LayerBitmapNode's undersized-layer handling.
    cv::Mat tile(r.height, r.width, full.type(), cv::Scalar(0));
    if (r.x >= full.cols || r.y >= full.rows) return tile;

    const int copyW = std::min(r.width, full.cols - r.x);
    const int copyH = std::min(r.height, full.rows - r.y);
    if (copyW <= 0 || copyH <= 0) return tile;

    full(cv::Rect(r.x, r.y, copyW, copyH))
        .copyTo(tile(cv::Rect(0, 0, copyW, copyH)));
    return tile;
}

namespace {

// Build the graph's root node and the canvas geometry it implies.
//
// Two legacy rules live here, both from LayerStack::render():
//
// 1. Base layer present (baseIdx >= 0)
//      canvas = baseLayer->image.clone()
//      if (baseLayer->opacity < 1)
//          canvas = addWeighted(canvas, opacity, zeros, 0, 0)
//    The base layer is composited over a BLACK backdrop at its own
//    opacity — unlike every other layer, which reaches its opacity
//    through applyBlend(). That is what LayerOpacityNode is for.
//
//    Visibility quirk, mirrored on purpose: the base-layer search skips
//    the `visible` check, but the compositing loop below it does not. So
//    a hidden base layer still seeds the canvas, and — being skipped by
//    the loop — keeps its raw image with no opacity applied. Both halves
//    of that are encoded below; changing either alone breaks parity.
//
// 2. No base layer (baseIdx < 0)
//      canvas = Mat(canvasSize(), CV_8UC3, Scalar(255, 255, 255))
//    A white canvas that every visible layer then blends onto. The DAG
//    needs something to hang the first layer off, which is what
//    SolidCanvasNode provides.
CompositorNode::NodeId makeRoot(layers::LayerStack& stack,
                                LayerStackAdapter::Result& r)
{
    r.canvasSize = stack.canvasSize();
    auto canvas = std::make_unique<SolidCanvasNode>(
        r.canvasSize, cv::Scalar(255, 255, 255, 255),
        QStringLiteral("Canvas:white"));
    const CompositorNode::NodeId id = canvas->id();
    r.graph.addNode(canvas.get());
    r.canvases.push_back(std::move(canvas));
    return id;
}

// Build one group's contribution: composite its children onto the
// incoming canvas, then apply the group's opacity with Normal blend.
//
// Declared before appendColorLayer, which appendGroupLayer also calls.
CompositorNode::NodeId appendColorLayer(const layers::LayerStack& stack,
                                       layers::LayerPtr layer,
                                       int index,
                                       CompositorNode::NodeId base,
                                       LayerStackAdapter::Result& r);

// Build one group's contribution: composite its children onto the
// incoming canvas, then apply the group's opacity with Normal blend.
//
// Mirrors LayerStack::compositeChildrenInto() on the legacy side, and
// recurses so a group nested inside a group composites its own subtree
// first and then its own opacity. Children are not in m_layers, so this
// cannot go through buildRange(); it walks groupChildrenOf() directly.
//
// Child exclusions match the legacy group branch: a SmartFilter belongs
// to its parent SmartObject's chain (and a group child has no chain
// index anyway), and an Adjustment has no standalone raster — neither can
// contribute inside a group. The group's own blendMode is intentionally
// ignored, as PS does.
CompositorNode::NodeId appendGroupLayer(layers::LayerStack& stack,
                                       const layers::LayerPtr& layer,
                                       CompositorNode::NodeId base,
                                       LayerStackAdapter::Result& r) {
    const auto& children = stack.groupChildrenOf(layer);
    if (children.empty()) return base;

    // A group is its own compositing stack (PS semantics): the child
    //   layers are composited onto a TRANSPARENT group canvas, then that
    //   canvas is composited back over the outer canvas with the group
    //   opacity.
    auto transparent = std::make_unique<SolidCanvasNode>(
        r.canvasSize, cv::Scalar(0, 0, 0, 0),
        QString::fromLatin1("GroupCanvas:%1").arg(layer->name));
    const CompositorNode::NodeId transparentId = transparent->id();
    r.graph.addNode(transparent.get());
    r.canvases.push_back(std::move(transparent));

    CompositorNode::NodeId inner = transparentId;
    for (const auto& child : children) {
        if (!child || !child->visible) continue;

        if (child->kind == layers::Layer::Group) {
            // Nested group: recurse. Group opacity is applied at each
            // level, matching the legacy recursion.
            inner = appendGroupLayer(stack, child, inner, r);
            continue;
        }
        if (child->kind == layers::Layer::SmartFilter) continue;
        if (child->kind == layers::Layer::Adjustment) continue;

        // index -1: group children are not in m_layers, so they carry no
        // SmartFilter chain (matching the legacy group branch).
        inner = appendColorLayer(stack, child, -1, inner, r);
    }

    if (inner == base) return base;   // no visible color children

    auto group = std::make_unique<GroupNode>(
        base, inner, layer->opacity,
        QString::fromLatin1("Group:%1").arg(layer->name));
    const CompositorNode::NodeId gid = group->id();
    r.graph.addNode(group.get());
    r.graph.addEdge(base, gid);
    if (inner != base) r.graph.addEdge(inner, gid);
    r.groups.push_back(std::move(group));
    return gid;
}

}   // namespace

// Forward declaration for the recursive helper below.
static CompositorNode::NodeId buildRange(layers::LayerStack& stack,
                                         int begin, int end,
                                         CompositorNode::NodeId base,
                                         LayerStackAdapter::Result& r);

namespace {

// Build one layer's contribution and blend it over `base`. Returns the
// new running node id (or `base` if the layer contributes nothing).
//
// `index` is the layer's position in m_layers, or -1 for a group child.
// It reaches the source node because only top-level layers carry a
// SmartFilter chain in the legacy render().
//
// Bitmap keeps its own zero-copy node (LayerBitmapNode reads
// layer->image directly). Everything else rasterizes through
// LayerRasterNode. Either way the downstream mask + blend wiring is
// identical, which is why this is one function rather than one per kind.
CompositorNode::NodeId appendColorLayer(const layers::LayerStack& stack,
                                       layers::LayerPtr layer,
                                       int index,
                                       CompositorNode::NodeId base,
                                       LayerStackAdapter::Result& r) {
    if (!layer) return base;

    // Source node.
    CompositorNode::NodeId colorIn = 0;
    if (layer->kind == layers::Layer::Bitmap) {
        auto bitmap = std::make_unique<LayerBitmapNode>(
            layer.get(), QString::fromLatin1("Bitmap:%1").arg(layer->name));
        colorIn = bitmap->id();
        r.graph.addNode(bitmap.get());
        r.bitmaps.push_back(std::move(bitmap));
    } else {
        auto raster = std::make_unique<LayerRasterNode>(
            &stack, layer, index,
            QString::fromLatin1("Raster:%1").arg(layer->name));
        colorIn = raster->id();
        r.graph.addNode(raster.get());
        r.rasters.push_back(std::move(raster));
    }

    // Mask (if enabled) wraps the color node.
    if (layer->mask.isActive()) {
        auto maskSrc = std::make_unique<MaskBitmapNode>(
            layer.get(), QString::fromLatin1("MaskSrc:%1").arg(layer->name));
        const CompositorNode::NodeId maskSrcId = maskSrc->id();
        r.graph.addNode(maskSrc.get());
        r.maskSources.push_back(
            LayerStackAdapter::Result::MaskSourceHolder{
                std::unique_ptr<CompositorNode>(maskSrc.release())});

        auto maskNode = std::make_unique<LayerMaskNode>(
            colorIn, maskSrcId,
            QString::fromLatin1("Mask:%1").arg(layer->name));
        const CompositorNode::NodeId maskId = maskNode->id();
        r.graph.addNode(maskNode.get());
        r.graph.addEdge(colorIn, maskId);
        r.graph.addEdge(maskSrcId, maskId);
        r.masks.push_back(std::move(maskNode));
        colorIn = maskId;
    }

    // Every layer reaches this function with a real base: build() makes
    // the graph root explicit, and the group branch inherits the
    // incoming canvas. So there is no "first layer becomes the canvas"
    // case here — that convention is what previously swallowed a layer's
    // opacity, and it is gone.
    Q_ASSERT(base != 0);

    auto blend = std::make_unique<LayerBlendNode>(
        base, colorIn, layer->blend, layer->opacity,
        QString::fromLatin1("Blend:%1").arg(layer->name));
    const CompositorNode::NodeId blendId = blend->id();
    r.graph.addNode(blend.get());
    r.graph.addEdge(base, blendId);
    r.graph.addEdge(colorIn, blendId);
    r.blends.push_back(std::move(blend));
    return blendId;
}

}   // namespace

// Recursively build layers [begin, end) starting from `base`.
// Handles LayerKind::Group by recursing into groupChildrenOf().
// Recursively build layers [begin, end) starting from `base`.
// Handles LayerKind::Group by recursing into groupChildrenOf().
//
// `base` is always a real node id: build() constructs an explicit root
// (the base layer, or a SolidCanvasNode when there is no bitmap base)
// before calling in here. That is deliberate — an earlier version used
// base == 0 to mean "this layer is the canvas", which silently dropped
// that layer's opacity and diverged from legacy whenever a stack had no
// bitmap base layer.
static CompositorNode::NodeId buildRange(layers::LayerStack& stack,
                                         int begin, int end,
                                         CompositorNode::NodeId base,
                                         LayerStackAdapter::Result& r)
{
    CompositorNode::NodeId prev = base;
    for (int i = begin; i < end; ++i) {
        layers::LayerPtr layer = stack.at(i);
        if (!layer || !layer->visible) continue;

        switch (layer->kind) {
        case layers::Layer::Bitmap:
        case layers::Layer::Text:
        case layers::Layer::Vector:
        case layers::Layer::SmartObject:
            prev = appendColorLayer(stack, layer, i, prev, r);
            break;

        case layers::Layer::Group: {
            prev = appendGroupLayer(stack, layer, prev, r);
            break;
        }

        case layers::Layer::SmartFilter:
            // P1.4.4: SmartFilter sub-layers render only as part of
            // their parent SmartObject's chain, which LayerRasterNode
            // applies to the rasterized full image. Skip here (matches
            // the legacy render() loop).
            break;

        case layers::Layer::Adjustment: {
            // Not a source: an adjustment rewrites the canvas
            // accumulated so far, so it wraps `prev` instead of
            // contributing a layer of its own.
            if (prev == 0) break;   // nothing to adjust yet
            auto adj = std::make_unique<LayerAdjustmentNode>(
                prev, layer->adjustmentLut, layer->opacity,
                layer.get(), QString::fromLatin1("Adjust:%1").arg(layer->name));
            const CompositorNode::NodeId adjId = adj->id();
            r.graph.addNode(adj.get());
            r.graph.addEdge(prev, adjId);
            r.adjustments.push_back(std::move(adj));
            prev = adjId;
            break;
        }
        }
    }
    return prev;
}

LayerStackAdapter::Result LayerStackAdapter::build(layers::LayerStack& stack)
{
    Result r;
    const int n = stack.count();

    // The root is the document-level white canvas and EVERY layer in
    //   the stack takes part in the compositing (LYR-13 / LYR-14): the base
    //   image is no longer the root, so hiding it really reveals what is
    //   below it.
    const CompositorNode::NodeId root = makeRoot(stack, r);
    r.outputNode = buildRange(stack, 0, n, root, r);
    return r;
}

} // namespace compositor