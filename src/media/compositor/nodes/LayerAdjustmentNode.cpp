// SPDX-License-Identifier: MIT
//
// LayerAdjustmentNode impl

#include "LayerAdjustmentNode.h"

#include "../RenderGraph.h"
#include "../TileGrid.h"

#include "../../imageworker/layers/PixelFormat.h"   // layers::splitBgra

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

    // The LUT only touches the colour channels -- running the alpha
    // through the LUT as well would scramble the canvas transparency.
    cv::Mat bgr, alpha, adjustedBgr, adjusted;
    layers::splitBgra(canvas, bgr, alpha);
    cv::LUT(bgr, m_lut, adjustedBgr);
    std::vector<cv::Mat> ch;
    cv::split(adjustedBgr, ch);
    ch.push_back(alpha);
    cv::merge(ch, adjusted);

    if (m_opacity < 1.0f) {
        // The adjustment's opacity scales the COVERAGE, not the colour:
        //   the adjusted colour is composited over the original with
        //   Porter-Duff "over", exactly like every other layer. A plain
        //   addWeighted would scale the colour instead, which darkens
        //   the result and disagrees with the legacy path.
        return layers::compositeOver(canvas, adjustedBgr, alpha, m_opacity);
    }
    return adjusted;
}

} // namespace compositor