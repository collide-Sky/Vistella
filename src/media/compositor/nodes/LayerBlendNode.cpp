// SPDX-License-Identifier: MIT
//
// LayerBlendNode impl - P8.1.1 Phase B (2026-10-20)
//
//   Delegates to layers::applyBlend() so compositor blend math is
//   identical to the legacy LayerStack::render() path. The only
//   compositor-specific concern is tile geometry: edge tiles may
//   have different sizes on base vs top (because the source layers
//   can be different resolutions, or because one side is a partial
//   edge tile). layers::canBlend() requires identical sizes, so we
//   normalize to the grid's tile rect before delegating.

#include "LayerBlendNode.h"

#include "../RenderGraph.h"   // for RenderContext full definition
#include "../TileGrid.h"

#include "../../imageworker/layers/BlendMode.h"   // layers::applyBlend

#include <opencv2/imgproc.hpp>   // cv::cvtColor
#include <algorithm>

namespace compositor {

namespace {

// Force `src` to match `dst` geometry by zero-padding or center-
// cropping. Returns a CV_8UC3 copy of exactly dst.size().
cv::Mat fitToSize(const cv::Mat& src, const cv::Size& target) {
    if (src.empty()) return cv::Mat();
    cv::Mat work = src;
    if (work.type() != CV_8UC3) {
        if (work.channels() == 1) {
            cv::Mat bgr;
            cv::cvtColor(work, bgr, cv::COLOR_GRAY2BGR);
            work = bgr;
        } else if (work.channels() == 4) {
            cv::Mat bgr;
            cv::cvtColor(work, bgr, cv::COLOR_BGRA2BGR);
            work = bgr;
        } else {
            return cv::Mat();
        }
    }
    if (work.size() == target) return work;

    const int tw = target.width;
    const int th = target.height;

    if (work.cols >= tw && work.rows >= th) {
        // Crop from the top-left (compositor convention: the tile
        // origin is the image origin, not the center).
        return work(cv::Rect(0, 0, tw, th)).clone();
    }
    // Zero-pad (canvas origin alignment).
    cv::Mat out(th, tw, CV_8UC3, cv::Scalar(0, 0, 0));
    const int copyW = std::min(tw, work.cols);
    const int copyH = std::min(th, work.rows);
    if (copyW > 0 && copyH > 0) {
        work(cv::Rect(0, 0, copyW, copyH))
            .copyTo(out(cv::Rect(0, 0, copyW, copyH)));
    }
    return out;
}

}   // namespace

LayerBlendNode::LayerBlendNode(CompositorNode::NodeId base,
                               CompositorNode::NodeId top,
                               layers::Layer::BlendMode mode,
                               float opacity,
                               QString name)
    : m_base(base)
    , m_top(top)
    , m_mode(mode)
    , m_opacity(opacity)
    , m_name(std::move(name))
{
}

cv::Mat LayerBlendNode::compute(int tileX, int tileY,
                                 const TileGrid& grid,
                                 RenderContext& ctx)
{
    cv::Mat base = ctx.requestNodeTile(m_base, tileX, tileY);
    cv::Mat top  = ctx.requestNodeTile(m_top,  tileX, tileY);

    // Missing base -> no output (the caller leaves the canvas black,
    // which matches the legacy render() behavior for a missing base).
    if (base.empty()) return cv::Mat();
    // Missing top -> pass through base unchanged.
    if (top.empty()) return base;

    // Normalize both sides to the grid's tile rect so layers::
    // canBlend() sees matching geometry.
    const cv::Size want = grid.tileRect(tileX, tileY).size();
    const cv::Mat baseFit = fitToSize(base, want);
    const cv::Mat topFit  = fitToSize(top,  want);
    if (baseFit.empty() || topFit.empty()) {
        return base;   // geometry mismatch we cannot reconcile
    }

    // Delegate. layers::applyBlend already handles opacity and all
    // 15 blend modes (the 4 HSV modes fall back to Normal there).
    return layers::applyBlend(m_mode, baseFit, topFit, m_opacity);
}

} // namespace compositor