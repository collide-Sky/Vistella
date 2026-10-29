#include "LayerMaskNode.h"

#include "../RenderGraph.h"   // for RenderContext
#include <opencv2/imgproc.hpp>

namespace compositor {

LayerMaskNode::LayerMaskNode(CompositorNode::NodeId base,
                             CompositorNode::NodeId mask,
                             QString name)
    : m_base(base), m_mask(mask), m_name(std::move(name))
{
}

cv::Mat LayerMaskNode::compute(int tileX, int tileY,
                               const TileGrid& grid,
                               RenderContext& ctx)
{
    cv::Mat base = ctx.requestNodeTile(m_base, tileX, tileY);
    if (base.empty()) return cv::Mat();

    cv::Mat mask = ctx.requestNodeTile(m_mask, tileX, tileY);
    if (mask.empty()) {
        // No mask source = no masking. Pass-through.
        return base;
    }
    // Ensure the mask is 8U single-channel.
    //   Every compositor node emits 4-channel BGRA, so a mask authored
    //   as CV_8UC1 arrives with its grey value in B/G/R and alpha 255.
    //   The coverage is therefore read from the B channel (project
    //   convention). Ignoring the 4-channel case would silently turn
    //   this node into a pass-through.
    if (mask.type() != CV_8UC1) {
        cv::Mat single;
        if (mask.channels() == 3 || mask.channels() == 4) {
            // Take the B channel as the coverage (project convention).
            cv::extractChannel(mask, single, 0);
        } else if (mask.channels() == 1) {
            single = mask;
        } else {
            return base;
        }
        mask = single;
    }

    // Resize mask to base size if needed (rare, but LayerMask may
    // come from a smaller source like a brush-stroke canvas).
    if (mask.size() != base.size()) {
        cv::resize(mask, mask, base.size(), 0, 0, cv::INTER_LINEAR);
    }

    // The mask only changes the alpha (CP-07): out.a = base.a * coverage / 255
    //   The colour is NOT touched -- it used to be a per-channel RGB
    //   multiply, so covering with a mask turned the pixels black and a
    //   semi-transparent mask degenerated into multiply-by-black.
    if (base.channels() != 4) return base;

    std::vector<cv::Mat> ch;
    cv::split(base, ch);
    if (ch.size() < 4) return base;
    cv::multiply(ch[3], mask, ch[3], 1.0 / 255.0);
    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

} // namespace compositor
