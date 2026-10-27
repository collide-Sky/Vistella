// SPDX-License-Identifier: MIT
//
// LayerRasterNode
//
//   Source node (no inputs) for the layer kinds that must be
//   rasterized before they can be composited:
//
//     LayerKind::Text          QPainter text -> full-canvas BGR
//     LayerKind::Vector        QPainter paths -> full-canvas BGR
//     LayerKind::SmartObject   decode + affine + fit -> full-canvas BGR
//
//   compute() slices one tile out of that rasterized image. The
//   rasterization itself is delegated to LayerStack::rasterizeLayer()
//   so the compositor and the legacy render() path share one
//   implementation — a second copy here is exactly how the two paths
//   would drift apart.
//
//   Why this is a separate class from LayerBitmapNode:
//   LayerBitmapNode reads pixels straight out of layer->image with no
//   copy and no intermediate, which is the right shape for the base
//   layer (the largest, hottest input). Everything this node handles
//   needs a decode/QPainter pass first, so the full-canvas result is
//   cached here and re-sliced per tile.
//
//   Why SmartFilter stays inside this node instead of becoming its own
//   filter node: several chain filters (GaussianBlur) sample
//   neighbouring pixels, so running them per tile would produce a
//   visible seam at every tile boundary. Correct handling needs a halo
//   of ceil(kernel/2) pixels per side fetched from upstream. See
//   LayerStack::applySmartFilterChain(). LUT-style per-pixel filters
//   could move out to real nodes without a halo; blur-style ones can not.

#pragma once

#include "../CompositorNode.h"
#include "../RenderGraph.h"

#include "../../imageworker/layers/Layer.h"
#include "../../imageworker/layers/LayerStack.h"

#include <QString>

#include <opencv2/core.hpp>

namespace compositor {

class LayerRasterNode : public CompositorNode {
public:
    // `stack` is a non-owning borrow and must outlive this node:
    // rasterize() needs it because Text / Vector / SmartObject all size
    // their output from LayerStack::canvasSize().
    //
    // `index` is the layer's position in m_layers, or -1 when the layer
    // is a group child (children live in m_groups, not m_layers). It
    // matters beyond convenience: only top-level layers go through the
    // SmartFilter chain in the legacy render(), and that is exactly the
    // set for which index >= 0.
    LayerRasterNode(const layers::LayerStack* stack,
                    layers::LayerPtr layer,
                    int index,
                    QString name);

    QList<NodeId> inputNodes() const override { return {}; }

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override;

    // Drop the memoized raster so the next compute() re-rasterizes.
    // LayerStack::render() rebuilds the DAG on every call, so this only
    // matters for a graph that is deliberately held across edits.
    void invalidateRaster() { m_raster = cv::Mat(); m_rasterValid = false; }

    // Convenience for tests: mark the raster stale and the node dirty in
    // one call.
    void markLayerEdited();

    // -1 for group children, otherwise the m_layers position.
    int layerIndex() const { return m_index; }
    const layers::Layer* layer() const { return m_layer.get(); }

private:
    // Rasterize once, memoized. Returns an empty Mat when the layer has
    // nothing to draw (blank text, no vector paths, unresolvable
    // SmartObject), which callers already treat as "contributes
    // nothing" — the same contract as the legacy rasterize().
    const cv::Mat& raster();

    const layers::LayerStack* m_stack = nullptr;
    layers::LayerPtr          m_layer;
    int                       m_index = -1;
    QString                   m_name;

    cv::Mat m_raster;
    bool    m_rasterValid = false;
};

} // namespace compositor