// SPDX-License-Identifier: MIT
//
// LayerBitmapNode - P8.1 (2026-10-16) demo: wraps a Bitmap Layer.
//   Source node (no inputs). compute() copies the tile region from
//   m_layer->image. If the layer is empty or wrong size, returns empty
//   (RenderGraph leaves the destination canvas black, which matches
//   LayerStack::render() behavior for an empty base layer).
//
//   P8.1 commit. P8.2 will add a GPU backend that uploads tile to a
//   texture and lets shader copy. P8.4 will parallelize across tiles.

#pragma once

#include "../CompositorNode.h"
#include "../RenderGraph.h"

#include <QString>
#include <opencv2/core.hpp>

namespace layers { struct Layer; }   // forward; LayerBitmapNode holds a raw ptr

namespace compositor {

class LayerBitmapNode : public CompositorNode {
public:
    // Non-owning. Caller (LayerStack / ImageWindow) keeps the Layer alive
    // for the lifetime of this node.
    explicit LayerBitmapNode(const layers::Layer* layer, QString name = {});

    QList<NodeId> inputNodes() const override { return {}; }

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override;

    // Convenience for the LayerBitmapNode-based tests: trigger a re-render
    // by marking the underlying layer as edited. Marks the node dirty +
    // bumps its version.
    void markLayerEdited();

private:
    const layers::Layer* m_layer = nullptr;
    QString m_name;
};

} // namespace compositor
