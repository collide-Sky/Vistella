// SPDX-License-Identifier: MIT
//
// GroupNode impl
//
//   Composites the group's content over the incoming canvas using
//   the group's opacity with Normal blend. Mirrors the legacy
//   LayerStack::render() group branch:
//       if (group.opacity < 1.0)
//           canvas = addWeighted(canvas, 1-op, groupMat, op, 0);
//       else
//           canvas = groupMat;
//   The group's own blendMode is intentionally ignored (PS does the
//   same).

#include "GroupNode.h"

#include "../RenderGraph.h"   // for RenderContext full definition
#include "../TileGrid.h"

#include "../../imageworker/layers/BlendMode.h"   // layers::applyBlend

namespace compositor {

GroupNode::GroupNode(CompositorNode::NodeId base,
                     CompositorNode::NodeId content,
                     float opacity,
                     QString name)
    : m_base(base)
    , m_content(content)
    , m_opacity(opacity)
    , m_name(std::move(name))
{
}

cv::Mat GroupNode::compute(int tileX, int tileY,
                            const TileGrid& /*grid*/,
                            RenderContext& ctx)
{
    cv::Mat content = ctx.requestNodeTile(m_content, tileX, tileY);
    if (content.empty()) {
        // No group content. If there is an incoming canvas, pass it
        // through unchanged (an empty group is a no-op).
        if (m_base) return ctx.requestNodeTile(m_base, tileX, tileY);
        return cv::Mat();
    }

    if (!m_base) {
        // Root group: no incoming canvas, content is the output.
        return content;
    }

    cv::Mat base = ctx.requestNodeTile(m_base, tileX, tileY);
    if (base.empty()) return content;   // nothing to composite over

    // Normal blend with the group's opacity. layers::applyBlend
    // already short-circuits opacity == 1.0 to `content.clone()`.
    return layers::applyBlend(layers::Layer::Normal, base, content, m_opacity);
}

} // namespace compositor