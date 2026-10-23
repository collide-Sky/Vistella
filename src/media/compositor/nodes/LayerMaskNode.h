// SPDX-License-Identifier: MIT
//
// LayerMaskNode - P8.1.1 (2026-10-19) apply a layer mask to a base tile.
//
//   Two input ports:
//     - port 0 = base tile (the layer's color)
//     - port 1 = mask tile (CV_8UC1 grayscale, 0=fully transparent,
//                          255=fully opaque)
//
//   Output: per-pixel mix between base and "transparent" based on
//   mask value. P8.1.1 emits the base tile with alpha-channel
//   semantics implicit (since CV_8UC3 has no alpha, the mask is
//   just applied as a multiplicative attenuation of the color).
//   This is the right behavior for compositor output — the canvas
//   has no per-pixel alpha at this stage; full alpha compositing
//   against an arbitrary background is a render-target concern
//   that the LayerStackAdapter handles at the top of the chain.
//
//   Mask value 0  -> output = base * 0           (fully masked)
//   Mask value 255 -> output = base * 1           (no mask)
//   Mask value 128 -> output = base * 0.5         (half masked)

#pragma once

#include "../CompositorNode.h"

#include <opencv2/core.hpp>

namespace compositor {

class LayerMaskNode : public CompositorNode {
public:
    LayerMaskNode(CompositorNode::NodeId base,
                  CompositorNode::NodeId mask,
                  QString name = QStringLiteral("Mask"));
    QList<NodeId> inputNodes() const override { return { m_base, m_mask }; }
    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;
    QString name() const override { return m_name; }

private:
    NodeId  m_base;
    NodeId  m_mask;
    QString m_name;
};

} // namespace compositor