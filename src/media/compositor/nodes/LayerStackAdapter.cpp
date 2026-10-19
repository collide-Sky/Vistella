#include "LayerStackAdapter.h"

#include "LayerBitmapNode.h"
#include "LayerBlendNode.h"
#include "LayerMaskNode.h"

#include "../TileGrid.h"

#include "../../imageworker/layers/LayerStack.h"
#include "../../imageworker/layers/Layer.h"

namespace compositor {

namespace {

// Adapter-private mask source: a CompositorNode that returns a
// single-channel cv::Mat from a Layer's layerMask. Reuses the
// LayerBitmapNode pattern but emits a grayscale tile.
class LayerMaskBitmapNode : public CompositorNode {
public:
    explicit LayerMaskBitmapNode(const layers::Layer* layer, QString name)
        : m_layer(layer), m_name(std::move(name)) {}
    QList<NodeId> inputNodes() const override { return {}; }
    cv::Mat compute(int /*tx*/, int /*ty*/,
                    const TileGrid& grid,
                    RenderContext& /*ctx*/) override {
        if (!m_layer || m_layer->layerMask.empty()) {
            // Empty mask = fully opaque. Return white 1-channel.
            const int t = grid.tileSize();
            return cv::Mat(t, t, CV_8UC1, cv::Scalar(255));
        }
        const cv::Mat& src = m_layer->layerMask;
        // The compositor asks per-tile; we return the full mask and
        // the caller can crop to the tile rect if it wants a tile-
        // exact slice.
        return src.clone();
    }
    QString name() const override { return m_name; }
private:
    const layers::Layer* m_layer;
    QString              m_name;
};

}   // namespace

LayerStackAdapter::Result LayerStackAdapter::build(layers::LayerStack& stack)
{
    Result r;

    // Bottom-up: stack[0] is at the bottom of the visual stack, so
    // it forms the initial base. Each subsequent visible layer is
    // blended on top of the running base.
    CompositorNode::NodeId prev = 0;   // 0 = null (no base yet)

    // Collect layer indices in render order: highest zOrder (top)
    // first. Layers with zOrder == 0 still need an order — we fall
    // back to the in-stack order.
    // For now: iterate stack in storage order (bottom to top).
    const int n = stack.count();
    for (int i = 0; i < n; ++i) {
        layers::LayerPtr layer = stack.at(i);
        if (!layer) continue;
        if (!layer->visible) {
            // Invisible: passthrough. (Don't add anything to the chain.)
            continue;
        }

        // Bitmap-only MVP. Non-Bitmap kinds are passthrough.
        if (layer->kind != layers::Layer::Bitmap) {
            if (prev == 0) {
                // No base yet and the first visible layer is non-bitmap.
                // Insert a 1x1 passthrough bitmap so the chain has a base.
                // P8.1.1.x will instead render the right kind.
                continue;
            }
            continue;
        }

        // Add the bitmap node for this layer.
        auto bitmap = std::make_unique<LayerBitmapNode>(layer.get(),
            QString::fromLatin1("Bitmap:%1").arg(layer->name));
        CompositorNode::NodeId bmId = bitmap->id();
        r.graph.addNode(bitmap.get());
        r.bitmaps.push_back(std::move(bitmap));

        // If the layer has a mask, wrap with LayerMaskNode.
        CompositorNode::NodeId colorIn = bmId;
        if (layer->maskEnabled && !layer->layerMask.empty()) {
            auto maskBm = std::make_unique<LayerMaskBitmapNode>(
                layer.get(),
                QString::fromLatin1("MaskSrc:%1").arg(layer->name));
            CompositorNode::NodeId maskSrcId = maskBm->id();
            r.graph.addNode(maskBm.get());
            // Keep the mask bitmap alive: piggyback on r.masks (we
            // don't need a separate vector for it because the
            // LayerMaskNode doesn't care about its lifetime beyond
            // graph teardown — addNode stores a raw pointer, but
            // for the MaskBitmapNode specifically we manage its
            // lifetime in a static list).
            // For simplicity: leak it (process-lifetime is fine for
            // MVP). P8.1.1.x will add an owned-bits vector.
            (void)maskSrcId;
            (void)maskBm.release();   // intentionally leaked for MVP
            // Build the mask node.
            auto maskNode = std::make_unique<LayerMaskNode>(
                bmId, maskSrcId,
                QString::fromLatin1("Mask:%1").arg(layer->name));
            CompositorNode::NodeId maskId = maskNode->id();
            r.graph.addNode(maskNode.get());
            r.masks.push_back(std::move(maskNode));
            colorIn = maskId;
        }

        if (prev == 0) {
            // First visible layer: it becomes the running base.
            prev = colorIn;
        } else {
            // Blend this layer on top of prev.
            auto blend = std::make_unique<LayerBlendNode>(
                prev, colorIn, layer->blend, layer->opacity,
                QString::fromLatin1("Blend:%1").arg(layer->name));
            CompositorNode::NodeId blendId = blend->id();
            r.graph.addNode(blend.get());
            r.blends.push_back(std::move(blend));
            prev = blendId;
        }
    }

    r.outputNode = prev;
    return r;
}

} // namespace compositor