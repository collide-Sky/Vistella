// SPDX-License-Identifier: MIT
//
// LayerBlendNode - P8.1.1 (2026-10-19) blend two layer tiles.
//
//   A node with two input ports (base, blend) that produces a single
//   output tile = blend(base, blend_layer, mode, opacity).
//
//   MVP mode coverage (will expand in P8.1.1.x):
//     - Normal:   out = base*(1-a) + top*a
//     - Multiply: out = base*top / 255
//     - Screen:   out = 255 - (255-base)*(255-top)/255
//     - Darken:   out = min(base, top)
//     - Lighten:  out = max(base, top)
//     - Difference: out = |base - top|
//
//   Other blend modes (Overlay/SoftLight/HardLight/ColorDodge/ColorBurn/
//   Exclusion/Hue/Saturation/Color/Luminosity) fall through to Normal
//   in P8.1.1. They will be filled in once the LayerStack adapter
//   needs them (the Layer already enumerates 15 modes; the GPU shader
//   path will accelerate them in P8.2.x+).
//
//   Per-tile inputs are CV_8UC3 BGR (the project-wide convention). The
//   opacity field blends the top layer's alpha before the blend math.

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