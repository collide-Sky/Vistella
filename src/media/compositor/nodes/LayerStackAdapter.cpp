#include "LayerStackAdapter.h"

#include "../TileGrid.h"

#include "../../imageworker/layers/LayerStack.h"
#include "../../imageworker/layers/Layer.h"
#include "../../imageworker/layers/BlendMode.h"

#include <opencv2/imgproc.hpp>   // cv::resize (mask normalization)

#include <algorithm>             // std::min

namespace compositor {

namespace {

// Adapter-private mask source: a CompositorNode that yields a
// single-channel cv::Mat from a Layer's layerMask. Lives in the
// adapter's Result so its lifetime matches the graph.
//
// Two steps, in this order:
//
// 1. Normalize the mask to canvas geometry. The legacy applyMask() resizes
//    a mask whose size differs from the layer it masks, so a 16x16 stroke
//    mask over a 32x32 layer is scaled UP before multiplying. Slicing the
//    raw mask instead would leave everything outside the mask's own
//    footprint at zero, which reads as "fully masked out" rather than
//    "not covered by the mask".
//
// 2. Slice by tile, exactly like LayerBitmapNode does for a colour layer.
//    Returning the whole mask used to work only when the canvas fit in one
//    tile: the consumer found a size mismatch against the tile and resized
//    the full mask down to it, so every tile received a downscaled copy of
//    the entire mask instead of its own region. That divergence appeared
//    as soon as a canvas spanned more than one tile.
//
// The normalized mask is memoized, so an undersized brush mask is rescaled
// once per render rather than once per tile.
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

    const cv::Mat& full = m_layer->layerMask;
    if (full.size() == canvas) {
        m_normalized = full;
    } else {
        cv::Mat resized;
        cv::resize(full, resized, canvas, 0, 0, cv::INTER_LINEAR);
        m_normalized = resized;
    }
    m_normalizedFor = canvas;
    return m_normalized;
}

cv::Mat MaskBitmapNode::compute(int tileX, int tileY,
                                const TileGrid& grid,
                                RenderContext& /*ctx*/)
{
    if (!m_layer || m_layer->layerMask.empty()) return cv::Mat();

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
                                int baseIdx,
                                LayerStackAdapter::Result& r)
{
    if (baseIdx < 0) {
        r.canvasSize = stack.canvasSize();
        auto canvas = std::make_unique<SolidCanvasNode>(
            r.canvasSize, cv::Scalar(255, 255, 255),
            QStringLiteral("Canvas:white"));
        const CompositorNode::NodeId id = canvas->id();
        r.graph.addNode(canvas.get());
        r.canvases.push_back(std::move(canvas));
        return id;
    }

    layers::LayerPtr base = stack.at(baseIdx);
    r.canvasSize = base->image.size();

    auto bitmap = std::make_unique<LayerBitmapNode>(
        base.get(), QString::fromLatin1("Base:%1").arg(base->name));
    CompositorNode::NodeId id = bitmap->id();
    r.graph.addNode(bitmap.get());
    r.bitmaps.push_back(std::move(bitmap));

    // Hidden base: the legacy loop skips it, so no opacity is applied
    // and the raw image stands. See rule 1 above.
    if (!base->visible || base->opacity >= 1.0f) return id;

    auto fade = std::make_unique<LayerOpacityNode>(
        id, base->opacity,
        QString::fromLatin1("BaseOpacity:%1").arg(base->name));
    const CompositorNode::NodeId fadeId = fade->id();
    r.graph.addNode(fade.get());
    r.graph.addEdge(id, fadeId);
    r.opacities.push_back(std::move(fade));
    return fadeId;
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

    CompositorNode::NodeId inner = base;
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
    if (layer->maskEnabled && !layer->layerMask.empty()) {
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

    // Legacy render() seeds the canvas with the FIRST Bitmap layer that
    // has a non-empty image, then walks forward from there. That search
    // deliberately does not check `visible` — see makeRoot() below.
    int baseIdx = -1;
    for (int i = 0; i < n; ++i) {
        const auto& l = stack.at(i);
        if (l && l->kind == layers::Layer::Bitmap && !l->image.empty()) {
            baseIdx = i;
            break;
        }
    }

    const CompositorNode::NodeId root = makeRoot(stack, baseIdx, r);

    // Layers before the base index are never reached by the legacy loop
    // (`for (i = max(0, baseIdx); ...)`), so they are excluded here too.
    const int begin = (baseIdx >= 0) ? baseIdx + 1 : 0;
    r.outputNode = buildRange(stack, begin, n, root, r);
    return r;
}

} // namespace compositor