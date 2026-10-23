// SPDX-License-Identifier: MIT
//
// LayerAdjustmentNode impl - P8.1.1.x (2026-10-20)

#include "LayerAdjustmentNode.h"

#include "../RenderGraph.h"
#include "../TileGrid.h"

namespace compositor {

LayerAdjustmentNode::LayerAdjustmentNode(CompositorNode::NodeId canvas,
                                         const cv::Mat& lut,
                                         float opacity,
                                         const layers::Layer* layer,
                                         QString name)
    : m_canvas(canvas)
    , m_lut(lut)
    , m_opacity(opacity)
    , m_name(std::move(name))
{
    if (m_name.isEmpty() && layer) m_name = layer->name;

    // Same guard as the legacy render() branch: cv::LUT requires a
    // 256x1 8-bit table, and anything else leaves the canvas untouched.
    m_hasUsableLut = !m_lut.empty()
                  && m_lut.rows == 256
                  && m_lut.cols == 1
                  && m_lut.type() == CV_8U;
}

QList<CompositorNode::NodeId> LayerAdjustmentNode::inputNodes() const
{
    return { m_canvas };
}

QString LayerAdjustmentNode::name() const
{
    return m_name;
}

cv::Mat LayerAdjustmentNode::compute(int tileX, int tileY,
                                     const TileGrid& /*grid*/,
                                     RenderContext& ctx)
{
    cv::Mat canvas = ctx.requestNodeTile(m_canvas, tileX, tileY);
    if (canvas.empty()) return cv::Mat();

    // Nothing to apply: pass the canvas through untouched, matching the
    // legacy branch that `continue`s when the LUT is unusable.
    if (!m_hasUsableLut) return canvas;

    cv::Mat adjusted;
    cv::LUT(canvas, m_lut, adjusted);

    if (m_opacity < 1.0f) {
        cv::Mat mixed;
        cv::addWeighted(canvas, 1.0f - m_opacity,
                        adjusted, m_opacity, 0, mixed);
        return mixed;
    }
    return adjusted;
}

} // namespace compositor