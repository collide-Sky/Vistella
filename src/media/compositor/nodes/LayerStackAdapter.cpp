#include "LayerStackAdapter.h"

#include "../TileGrid.h"

#include "../../imageworker/layers/LayerStack.h"
#include "../../imageworker/layers/Layer.h"
#include "../../imageworker/layers/BlendMode.h"

namespace compositor {

namespace {

// Adapter-private mask source: a CompositorNode that returns a
// single-channel cv::Mat from a Layer's layerMask. Lives in the
// adapter's Result so its lifetime matches the graph.
class MaskBitmapNode : public CompositorNode {
public:
    explicit MaskBitmapNode(const layers::Layer* layer, QString name)
        : m_layer(layer), m_name(std::move(name)) {}
    QList<NodeId> inputNodes() const override { return {}; }
    cv::Mat compute(int /*tx*/, int /*ty*/,
                    const TileGrid& /*grid*/,
                    RenderContext& /*ctx*/) override {
        if (!m_layer || m_layer->layerMask.empty()) return cv::Mat();
        return m_layer->layerMask.clone();
    }
    QString name() const override { return m_name; }
private:
    const layers::Layer* m_layer;
    QString              m_name;
};

}   // namespace

// Forward declaration for the recursive helper below.
static CompositorNode::NodeId buildRange(layers::LayerStack& stack,
                                         int begin, int end,
                                         CompositorNode::NodeId base,
                                         LayerStackAdapter::Result& r,
                                         int forcedVisibleIdx = -1);

namespace {

// Build a single Bitmap layer's contribution and blend it over
// `base`. Returns the new running node id (or `base` if the layer
// contributes nothing).
CompositorNode::NodeId appendBitmapLayer(const layers::Layer* layer,
                                         CompositorNode::NodeId base,
                                         LayerStackAdapter::Result& r) {
    // Bitmap source node.
    auto bitmap = std::make_unique<LayerBitmapNode>(
        layer, QString::fromLatin1("Bitmap:%1").arg(layer->name));
    const CompositorNode::NodeId bmId = bitmap->id();
    r.graph.addNode(bitmap.get());
    r.bitmaps.push_back(std::move(bitmap));

    CompositorNode::NodeId colorIn = bmId;

    // Mask (if enabled) wraps the color node.
    if (layer->maskEnabled && !layer->layerMask.empty()) {
        auto maskSrc = std::make_unique<MaskBitmapNode>(
            layer, QString::fromLatin1("MaskSrc:%1").arg(layer->name));
        const CompositorNode::NodeId maskSrcId = maskSrc->id();
        r.graph.addNode(maskSrc.get());
        r.maskSources.push_back(
            LayerStackAdapter::Result::MaskSourceHolder{
                std::unique_ptr<CompositorNode>(maskSrc.release())});

        auto maskNode = std::make_unique<LayerMaskNode>(
            bmId, maskSrcId,
            QString::fromLatin1("Mask:%1").arg(layer->name));
        const CompositorNode::NodeId maskId = maskNode->id();
        r.graph.addNode(maskNode.get());
        r.graph.addEdge(bmId, maskId);
        r.graph.addEdge(maskSrcId, maskId);
        r.masks.push_back(std::move(maskNode));
        colorIn = maskId;
    }

    if (base == 0) {
        // First visible layer: it becomes the running base.
        return colorIn;
    }

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
// `forcedVisibleIdx` mirrors an asymmetry in the legacy LayerStack::render():
// the base-layer search loop only checks `kind == Bitmap && !image.empty()`,
// NOT `visible`. An invisible base layer still seeds the canvas, while the
// compositing loop that follows DOES skip invisible layers. Passing the base
// index here reproduces that behaviour exactly, so a stack whose only bitmap
// layer is hidden still renders that layer's image rather than a blank canvas.
static CompositorNode::NodeId buildRange(layers::LayerStack& stack,
                                         int begin, int end,
                                         CompositorNode::NodeId base,
                                         LayerStackAdapter::Result& r,
                                         int forcedVisibleIdx)
{
    CompositorNode::NodeId prev = base;
    for (int i = begin; i < end; ++i) {
        layers::LayerPtr layer = stack.at(i);
        if (!layer) continue;
        if (i != forcedVisibleIdx && !layer->visible) continue;

        switch (layer->kind) {
        case layers::Layer::Bitmap:
            prev = appendBitmapLayer(layer.get(), prev, r);
            break;

        case layers::Layer::Group: {
            const auto& children = stack.groupChildrenOf(stack.idOf(i));
            if (children.empty()) break;

            // Composite the children onto a copy of the incoming
            // canvas (matching the legacy render(): groupMat starts
            // as canvas.clone()).
            //
            // Children are NOT in m_layers, so we can't recurse via
            // buildRange(). Instead we build them as a chain off a
            // synthetic "base" that is the incoming canvas, then wrap
            // in a GroupNode for the group's opacity.
            //
            // PS semantics: children blend onto the canvas with their
            // own blend modes, THEN the group's opacity is applied.
            CompositorNode::NodeId inner = prev;
            if (inner == 0) {
                // Root group with no incoming canvas: start from a
                // black canvas so children have something to blend
                // over. The adapter can't allocate one without a
                // grid, so we use the first child's node directly
                // and let the GroupNode handle the rest.
                inner = 0;
            }
            for (const auto& child : children) {
                if (!child || !child->visible) continue;
                if (child->kind == layers::Layer::Group) {
                    // Nested group inside a group: flatten by
                    // treating it as a passthrough (P8.1.1.x will
                    // add proper nesting).
                    continue;
                }
                if (child->kind != layers::Layer::Bitmap) continue;
                inner = appendBitmapLayer(child.get(), inner, r);
            }

            if (inner == 0) break;   // no visible bitmap children

            if (prev == 0) {
                // Root group: the composite IS the output, but we
                // still wrap it so the group's opacity applies.
                auto group = std::make_unique<GroupNode>(
                    0, inner, layer->opacity,
                    QString::fromLatin1("Group:%1").arg(layer->name));
                const CompositorNode::NodeId gid = group->id();
                r.graph.addNode(group.get());
                r.graph.addEdge(inner, gid);
                r.groups.push_back(std::move(group));
                prev = gid;
                break;
            }

            auto group = std::make_unique<GroupNode>(
                prev, inner, layer->opacity,
                QString::fromLatin1("Group:%1").arg(layer->name));
            const CompositorNode::NodeId gid = group->id();
            r.graph.addNode(group.get());
            r.graph.addEdge(prev, gid);
            r.graph.addEdge(inner, gid);
            r.groups.push_back(std::move(group));
            prev = gid;
            break;
        }

        case layers::Layer::SmartFilter:
            // P1.4.4: SmartFilter sub-layers render only as part of
            // their parent SmartObject's chain. Skip here (matches
            // the legacy render() loop).
            break;

        case layers::Layer::Adjustment:
            // P8.1.1.x: add LayerAdjustmentNode (LUT application).
            break;

        case layers::Layer::Vector:
        case layers::Layer::Text:
        case layers::Layer::SmartObject:
            // P8.1.1.x: add dedicated rasterizing nodes for these
            // kinds. For now they contribute nothing to the DAG.
            break;
        }
    }
    return prev;
}

LayerStackAdapter::Result LayerStackAdapter::build(layers::LayerStack& stack)
{
    Result r;
    const int n = stack.count();

    // P8.1.1 Phase B: legacy render() picks the FIRST Bitmap layer
    // with a non-empty image as the canvas seed, then composites from
    // that index onward. Mirror that so the DAG matches pixel-for-pixel.
    int baseIdx = -1;
    for (int i = 0; i < n; ++i) {
        const auto& l = stack.at(i);
        if (l && l->kind == layers::Layer::Bitmap && !l->image.empty()) {
            baseIdx = i;
            break;
        }
    }

    if (baseIdx < 0) {
        // No bitmap base. Legacy render() creates a white canvas of
        // canvasSize() and composites everything over it. We can't
        // allocate that without a grid, so we start from the first
        // visible layer (best effort).
        r.canvasSize = stack.canvasSize();
        r.outputNode = buildRange(stack, 0, n, 0, r);
        return r;
    }

    r.canvasSize = stack.at(baseIdx)->image.size();
    r.outputNode = buildRange(stack, baseIdx, n, 0, r, baseIdx);
    return r;
}

} // namespace compositor