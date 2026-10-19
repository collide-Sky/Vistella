#include "LayerBlendNode.h"

#include "../RenderGraph.h"   // for RenderContext full definition
#include "../TileGrid.h"

#include <opencv2/core.hpp>

namespace compositor {

namespace {

// Apply opacity to a CV_8UC3 tile: out = top * opacity (saturating).
// Used to pre-multiply the top layer before blend math.
cv::Mat applyOpacity(const cv::Mat& top, float opacity) {
    if (opacity >= 0.999f) return top;
    if (opacity <= 0.001f) {
        return cv::Mat::zeros(top.size(), top.type());
    }
    cv::Mat out(top.size(), top.type());
    for (int y = 0; y < out.rows; ++y) {
        const uchar* sp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            int v = static_cast<int>(sp[x] * opacity + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : v);
        }
    }
    return out;
}

// Normal blend: out = base*(1-a) + top*a, with a = opacity. The
// caller has NOT pre-multiplied top; opacity is applied here directly.
void blendNormal(const cv::Mat& base, const cv::Mat& top,
                 cv::Mat& out, float opacity) {
    out.create(base.size(), base.type());
    const float a = opacity;
    const float ia = 1.0f - a;
    for (int y = 0; y < out.rows; ++y) {
        const uchar* bp = base.ptr<uchar>(y);
        const uchar* tp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            int v = static_cast<int>(bp[x] * ia + tp[x] * a + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : v);
        }
    }
}

// Multiply: out = (base * top + 127) / 255, then alpha-blend with base.
//   Equivalent to:  m = (base * top)/255; out = base*(1-a) + m*a
void blendMultiply(const cv::Mat& base, const cv::Mat& top,
                  cv::Mat& out, float opacity) {
    out.create(base.size(), base.type());
    const float a = opacity;
    const float ia = 1.0f - a;
    for (int y = 0; y < out.rows; ++y) {
        const uchar* bp = base.ptr<uchar>(y);
        const uchar* tp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            const int m = (bp[x] * tp[x] + 127) / 255;
            int v = static_cast<int>(bp[x] * ia + m * a + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : v);
        }
    }
}

// Screen: out = 255 - (255-base)*(255-top)/255, then alpha-blend.
void blendScreen(const cv::Mat& base, const cv::Mat& top,
                 cv::Mat& out, float opacity) {
    out.create(base.size(), base.type());
    const float a = opacity;
    const float ia = 1.0f - a;
    for (int y = 0; y < out.rows; ++y) {
        const uchar* bp = base.ptr<uchar>(y);
        const uchar* tp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            const int s = 255 - ((255 - bp[x]) * (255 - tp[x]) + 127) / 255;
            int v = static_cast<int>(bp[x] * ia + s * a + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : (v < 0 ? 0 : v));
        }
    }
}

// Darken / Lighten: per-channel min/max, then alpha-blend.
void blendDarken(const cv::Mat& base, const cv::Mat& top,
                 cv::Mat& out, float opacity) {
    out.create(base.size(), base.type());
    const float a = opacity;
    const float ia = 1.0f - a;
    for (int y = 0; y < out.rows; ++y) {
        const uchar* bp = base.ptr<uchar>(y);
        const uchar* tp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            const int d = std::min(bp[x], tp[x]);
            int v = static_cast<int>(bp[x] * ia + d * a + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : v);
        }
    }
}
void blendLighten(const cv::Mat& base, const cv::Mat& top,
                  cv::Mat& out, float opacity) {
    out.create(base.size(), base.type());
    const float a = opacity;
    const float ia = 1.0f - a;
    for (int y = 0; y < out.rows; ++y) {
        const uchar* bp = base.ptr<uchar>(y);
        const uchar* tp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            const int l = std::max(bp[x], tp[x]);
            int v = static_cast<int>(bp[x] * ia + l * a + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : v);
        }
    }
}

// Difference: per-channel |base - top|, then alpha-blend.
void blendDifference(const cv::Mat& base, const cv::Mat& top,
                    cv::Mat& out, float opacity) {
    out.create(base.size(), base.type());
    const float a = opacity;
    const float ia = 1.0f - a;
    for (int y = 0; y < out.rows; ++y) {
        const uchar* bp = base.ptr<uchar>(y);
        const uchar* tp = top.ptr<uchar>(y);
        uchar* dp = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols * 3; ++x) {
            const int di = std::abs(bp[x] - tp[x]);
            int v = static_cast<int>(bp[x] * ia + di * a + 0.5f);
            dp[x] = static_cast<uchar>(v > 255 ? 255 : v);
        }
    }
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
    if (base.empty()) return cv::Mat();
    if (top.empty())  return base;   // missing top = pass-through

    // Make sure both are CV_8UC3 with the same size as the grid slot.
    const cv::Rect r = grid.tileRect(tileX, tileY);
    if (top.size() != r.size() || top.type() != CV_8UC3) {
        // Mismatched geometry: bail to base (best effort).
        return base;
    }
    if (base.size() != r.size() || base.type() != CV_8UC3) {
        return top;
    }

    // Opacity is applied per-mode in the blend helpers (so each
    // mode can choose how to mix base and top). We do NOT pre-scale
    // top here because that would lose base's contribution for
    // non-100% opacity.
    cv::Mat out;
    switch (m_mode) {
    case layers::Layer::Normal:
        blendNormal(base, top, out, m_opacity);
        break;
    case layers::Layer::Multiply:
        blendMultiply(base, top, out, m_opacity);
        break;
    case layers::Layer::Screen:
        blendScreen(base, top, out, m_opacity);
        break;
    case layers::Layer::Darken:
        blendDarken(base, top, out, m_opacity);
        break;
    case layers::Layer::Lighten:
        blendLighten(base, top, out, m_opacity);
        break;
    case layers::Layer::Difference:
        blendDifference(base, top, out, m_opacity);
        break;
    default:
        // P8.1.1.x will add Overlay/SoftLight/HardLight/ColorDodge/
        // ColorBurn/Exclusion/Hue/Saturation/Color/Luminosity.
        // For now: fall through to Normal so the rest of the
        // pipeline still produces a usable image.
        blendNormal(base, top, out, m_opacity);
        break;
    }
    return out;
}

} // namespace compositor