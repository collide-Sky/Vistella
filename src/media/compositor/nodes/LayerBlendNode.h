// SPDX-License-Identifier: MIT
//
// LayerBlendNode - P8.1.1 (2026-10-16) blend two layer tiles.
//
//   A node with two input ports (base, blend) that produces a single
//   output tile = blend(base, blend_layer, mode, opacity).
//
//   P8.1.1 Phase B (2026-10-20): delegates to layers::applyBlend() so
//   the compositor uses EXACTLY the same math as the legacy
//   LayerStack::render() path. Previously this node had its own
//   hand-rolled per-pixel implementations for 6 modes, which meant
//   two sets of blend math to keep in sync. Now there is one.
//
//   Tile geometry note: layers::canBlend() requires base.size() ==
//   blend.size(). In the compositor, edge tiles can have differing
//   sizes (the base may be full-grid while the top is a partial
//   edge tile). We resolve that by cropping/padding to the common
//   size before delegating, and writing the result back into the
//   grid's tile rect.
//
//   Supported modes: all 15 in Layer::BlendMode. The four HSV
//   composite modes (Hue / Saturation / Color / Luminosity) fall
//   back to Normal in BlendMode.cpp (Phase 3 declared, not
//   implemented) — they will land in BlendMode.cpp, not here.

#pragma once

#include "../CompositorNode.h"
#include "../../imageworker/layers/Layer.h"

#include <opencv2/core.hpp>

namespace compositor {

class LayerBlendNode : public CompositorNode {
public:
    // inputs: port 0 = base, port 1 = top (the layer being blended on top).
    LayerBlendNode(CompositorNode::NodeId base,
                   CompositorNode::NodeId top,
                   layers::Layer::BlendMode mode,
                   float opacity = 1.0f,
                   QString name = QStringLiteral("Blend"));

    QList<NodeId> inputNodes() const override {
        return { m_base, m_top };
    }

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override { return m_name; }

    // For tests / inspection.
    layers::Layer::BlendMode mode() const { return m_mode; }
    float opacity() const { return m_opacity; }

private:
    NodeId                   m_base;
    NodeId                   m_top;
    layers::Layer::BlendMode m_mode;
    float                    m_opacity;
    QString                  m_name;
};

} // namespace compositor