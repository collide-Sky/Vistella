// SPDX-License-Identifier: MIT
//
// LayerOpacityNode - P8.1.1.y (2026-10-22)
//
//   Filter node (one input) that attenuates its input toward black by a
//   fixed opacity:  out = src * opacity
//
//   Why this exists: the legacy LayerStack::render() loop treats the
//   compositing base layer differently from every other layer. It seeds
//   the canvas with the base layer's image, then — only for that layer —
//   applies
//       if (opacity < 1) canvas = addWeighted(canvas, opacity, zeros, 0, 0)
//   i.e. the base layer is composited over a black backdrop at its own
//   opacity, instead of going through applyBlend() like the rest of the
//   stack. Everything above the base reaches its opacity through
//   LayerBlendNode.
//
//   Modelled as its own node rather than as a special case inside
//   LayerBlendNode, because there is nothing to blend against: the
//   backdrop is a constant black, and encoding that as a graph edge would
//   mean inventing a node that exists only to be discarded.
//
//   The math delegates to layers::applyBlend(Normal, zeros, src, opacity)
//   so the base-layer result comes from the same tested blend code as
//   every other layer, rather than a re-derived addWeighted.

#pragma once

#include "../CompositorNode.h"
#include "../RenderGraph.h"

#include <QString>

#include <opencv2/core.hpp>

namespace compositor {

class LayerOpacityNode : public CompositorNode {
public:
    LayerOpacityNode(CompositorNode::NodeId src, float opacity,
                    QString name = {});

    QList<NodeId> inputNodes() const override;

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override;

    float opacity() const { return m_opacity; }

private:
    NodeId  m_src;
    float   m_opacity = 1.0f;
    QString m_name;
};

} // namespace compositor