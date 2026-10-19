#include "LayerMaskNode.h"

#include "../RenderGraph.h"   // for RenderContext
#include <opencv2/imgproc.hpp>   // cv::resize

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
    // Defensive: ensure mask is 8U single-channel.
    if (mask.type() != CV_8UC1) {
        cv::Mat single;
        if (mask.channels() == 3) {
            // Take the B channel as the alpha (project convention).
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

    // Per-channel multiplicative attenuation: out = base * (mask/255)
    cv::Mat baseF, maskF;
    base.convertTo(baseF, CV_32FC3);
    mask.convertTo(maskF, CV_32FC1, 1.0 / 255.0);
    // Broadcast single-channel mask across 3 channels.
    std::vector<cv::Mat> channels(3, maskF);
    cv::Mat maskBgr;
    cv::merge(channels, maskBgr);
    cv::Mat outF = baseF.mul(maskBgr);
    outF.convertTo(base, CV_8UC3);   // in-place to avoid extra alloc
    return base;
}

} // namespace compositor