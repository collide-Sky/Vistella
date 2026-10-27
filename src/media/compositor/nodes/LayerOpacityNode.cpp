// SPDX-License-Identifier: MIT
//
// LayerOpacityNode impl

#include "LayerOpacityNode.h"

#include "../RenderGraph.h"
#include "../TileGrid.h"

#include "../../imageworker/layers/Layer.h"
#include "../../imageworker/layers/BlendMode.h"

namespace compositor {

LayerOpacityNode::LayerOpacityNode(CompositorNode::NodeId src,
                                   float opacity,
                                   QString name)
    : m_src(src)
    , m_opacity(opacity)
    , m_name(std::move(name))
{
}

QList<CompositorNode::NodeId> LayerOpacityNode::inputNodes() const
{
    return { m_src };
}

QString LayerOpacityNode::name() const
{
    return m_name;
}

cv::Mat LayerOpacityNode::compute(int tileX, int tileY,
                                  const TileGrid& /*grid*/,
                                  RenderContext& ctx)
{
    cv::Mat src = ctx.requestNodeTile(m_src, tileX, tileY);
    if (src.empty()) return cv::Mat();

    // opacity >= 1 short-circuits inside applyBlend (returns blend.clone()),
    // which matches the legacy guard `if (opacity < 1.0f)`. Keeping the
    // call unconditional means one code path for every opacity.
    const cv::Mat black(src.size(), src.type(), cv::Scalar(0, 0, 0));
    return layers::applyBlend(layers::Layer::Normal, black, src, m_opacity);
}

} // namespace compositor