// SPDX-License-Identifier: MIT
//
// LayerAdjustmentNode
//
//   Filter node (one input) for LayerKind::Adjustment.
//
//   Adjustment layers are not standalone images — they transform the
//   canvas accumulated so far. So unlike Text / Vector / SmartObject
//   they get a filter node rather than a source node, and in the DAG
//   they wrap the running composite instead of contributing a layer of
//   their own.
//
//   Mirrors the legacy LayerStack::render() adjustment branch:
//       adjusted = LUT(canvas, lut)
//       if (opacity < 1) canvas = addWeighted(canvas, 1-op, adjusted, op, 0)
//       else          canvas = adjusted
//
//   Tile safety: cv::LUT is a pure per-pixel table lookup with no
//   neighbourhood sampling, so running it per tile is exactly
//   equivalent to running it on the whole canvas. That is why this can
//   be a real node while the SmartFilter chain cannot (blur kernels
//   cross tile seams — see LayerRasterNode).

#pragma once

#include "../CompositorNode.h"
#include "../RenderGraph.h"

#include "../../imageworker/layers/Layer.h"

#include <QString>

#include <opencv2/core.hpp>

namespace compositor {

class LayerAdjustmentNode : public CompositorNode {
public:
    // Non-owning: `layer` is only read for name()/blend bookkeeping; the
    // LUT and the opacity are copied in, so the node stays valid even if
    // the Layer is mutated while a render is in flight.
    LayerAdjustmentNode(CompositorNode::NodeId canvas,
                        const cv::Mat& lut,
                        float opacity,
                        const layers::Layer* layer,
                        QString name);

    QList<NodeId> inputNodes() const override;

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override;

    // cv::LUT accepts only a 256x1 CV_8U table. Anything else is a
    // no-op in the legacy path too (render() guards the same three
    // conditions), so it is checked once here instead of per tile.
    bool hasUsableLut() const { return m_hasUsableLut; }

private:
    NodeId     m_canvas;
    cv::Mat    m_lut;
    float      m_opacity = 1.0f;
    bool       m_hasUsableLut = false;
    QString    m_name;
};

} // namespace compositor