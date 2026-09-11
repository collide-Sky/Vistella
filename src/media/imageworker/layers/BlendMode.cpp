// =============================================================
// BlendMode 实现
// 阶段 1 W4.3 Phase 2 (2026-09-04) — 详见 BlendMode.h
// =============================================================

#include "BlendMode.h"

#include <opencv2/core.hpp>

namespace layers {

// =====================================================================
//  工具: alpha blend (通用辅助)
// =====================================================================

namespace {

// 基础 alpha blend: result = (1 - opacity) * base + opacity * blend
//   (cv::addWeighted 包装, 饱和到 0-255)
cv::Mat alphaBlend(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (opacity <= 0.0f) return base.clone();
    if (opacity >= 1.0f) return blend.clone();
    cv::Mat out;
    cv::addWeighted(base, 1.0 - opacity, blend, opacity, 0.0, out);
    return out;
}

// saturate 到 0-255 (8U)
inline uchar satU8(double v)
{
    if (v < 0.0) return 0;
    if (v > 255.0) return 255;
    return static_cast<uchar>(v);
}

} // namespace

// =====================================================================
//  11 种基础混合模式
// =====================================================================

cv::Mat blendNormal(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    return alphaBlend(base, blend, opacity);
}

// Multiply: A * B / 255
//   全白 = 不变, 全黑 = 黑, 中间灰 = 变暗
cv::Mat blendMultiply(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            o[x] = satU8((b[x] * l[x]) / 255.0);
        }
    }
    return alphaBlend(base, out, opacity);
}

// Screen: 255 - (255-A) * (255-B) / 255
//   Multiply 的反义, 变亮
cv::Mat blendScreen(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            o[x] = satU8(255.0 - ((255 - b[x]) * (255 - l[x])) / 255.0);
        }
    }
    return alphaBlend(base, out, opacity);
}

// Overlay: 根据 base 亮度选择 Multiply / Screen
//   base > 128: Screen, base < 128: Multiply
//   保留 base 的明暗, 加强 blend 的对比
cv::Mat blendOverlay(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            const double base_d = b[x];
            const double blend_d = l[x];
            double result;
            if (base_d < 128.0) {
                // Multiply: 2 * base * blend / 255
                result = (2.0 * base_d * blend_d) / 255.0;
            } else {
                // Screen: 255 - 2 * (255-base) * (255-blend) / 255
                result = 255.0 - (2.0 * (255.0 - base_d) * (255.0 - blend_d)) / 255.0;
            }
            o[x] = satU8(result);
        }
    }
    return alphaBlend(base, out, opacity);
}

// SoftLight (Photoshop 算法):
//   blend < 128: result = base - (255 - 2*blend) * (255 - base) * base / (255*255)
//   blend >= 128: result = base + (2*blend - 255) * (D(base) - base)
//     其中 D(base) = ((base/255)^(1/3) * 255) if base > 0 else 0
// 简化实现 (Pegtop 算法, 跟 PS 略有差异但视觉相似)
cv::Mat blendSoftLight(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            const double b_d = b[x];
            const double l_d = l[x];
            double result;
            if (l_d < 128.0) {
                result = b_d - (1.0 - 2.0 * l_d / 255.0) * b_d * (1.0 - b_d / 255.0);
            } else {
                const double D = (b_d > 0.0) ? std::pow(b_d / 255.0, 0.5) : 0.0;
                result = b_d + (2.0 * l_d / 255.0 - 1.0) * (D * 255.0 - b_d);
            }
            o[x] = satU8(result);
        }
    }
    return alphaBlend(base, out, opacity);
}

// HardLight: Overlay 对称 (base / blend 互换)
//   blend > 128: Screen, blend < 128: Multiply
cv::Mat blendHardLight(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            const double b_d = b[x];
            const double l_d = l[x];
            double result;
            if (l_d < 128.0) {
                result = (2.0 * b_d * l_d) / 255.0;
            } else {
                result = 255.0 - (2.0 * (255.0 - b_d) * (255.0 - l_d)) / 255.0;
            }
            o[x] = satU8(result);
        }
    }
    return alphaBlend(base, out, opacity);
}

// ColorDodge: 提亮 base (用 blend)
//   result = base / (255 - blend)  (blend=255 → 255; blend=0 → base 不变)
cv::Mat blendColorDodge(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            const double b_d = b[x];
            const double l_d = l[x];
            double result;
            if (l_d >= 255.0) {
                result = 255.0;
            } else {
                result = std::min(255.0, (b_d * 255.0) / (255.0 - l_d));
            }
            o[x] = satU8(result);
        }
    }
    return alphaBlend(base, out, opacity);
}

// ColorBurn: 加深 base (用 blend)
//   result = 255 - (255 - base) / blend  (blend=0 → 0; blend=255 → base 不变)
cv::Mat blendColorBurn(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            const double b_d = b[x];
            const double l_d = l[x];
            double result;
            if (l_d <= 0.0) {
                result = 0.0;
            } else {
                result = std::max(0.0, 255.0 - ((255.0 - b_d) * 255.0) / l_d);
            }
            o[x] = satU8(result);
        }
    }
    return alphaBlend(base, out, opacity);
}

// Darken: min(A, B)
cv::Mat blendDarken(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            o[x] = std::min(b[x], l[x]);
        }
    }
    return alphaBlend(base, out, opacity);
}

// Lighten: max(A, B)
cv::Mat blendLighten(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            o[x] = std::max(b[x], l[x]);
        }
    }
    return alphaBlend(base, out, opacity);
}

// Difference: |A - B|
cv::Mat blendDifference(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            o[x] = satU8(std::abs(int(b[x]) - int(l[x])));
        }
    }
    return alphaBlend(base, out, opacity);
}

// Exclusion: A + B - 2*A*B/255
//   跟 Difference 相似但更柔和
cv::Mat blendExclusion(const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) return cv::Mat();
    cv::Mat out(base.size(), base.type());
    for (int y = 0; y < base.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *l = blend.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < base.cols * base.channels(); ++x) {
            o[x] = satU8(b[x] + l[x] - (2 * b[x] * l[x]) / 255.0);
        }
    }
    return alphaBlend(base, out, opacity);
}

// =====================================================================
//  调度器
// =====================================================================

cv::Mat applyBlend(Layer::BlendMode mode, const cv::Mat &base, const cv::Mat &blend, float opacity)
{
    if (!canBlend(base, blend)) {
        return base.empty() ? cv::Mat() : base.clone();
    }
    switch (mode) {
    case Layer::Normal:      return blendNormal(base, blend, opacity);
    case Layer::Multiply:    return blendMultiply(base, blend, opacity);
    case Layer::Screen:      return blendScreen(base, blend, opacity);
    case Layer::Overlay:     return blendOverlay(base, blend, opacity);
    case Layer::SoftLight:   return blendSoftLight(base, blend, opacity);
    case Layer::HardLight:   return blendHardLight(base, blend, opacity);
    case Layer::ColorDodge:  return blendColorDodge(base, blend, opacity);
    case Layer::ColorBurn:   return blendColorBurn(base, blend, opacity);
    case Layer::Darken:      return blendDarken(base, blend, opacity);
    case Layer::Lighten:     return blendLighten(base, blend, opacity);
    case Layer::Difference:  return blendDifference(base, blend, opacity);
    case Layer::Exclusion:   return blendExclusion(base, blend, opacity);
    // Phase 3: Hue / Saturation / Color / Luminosity (HSV 复合)
    case Layer::Hue:
    case Layer::Saturation:
    case Layer::Color:
    case Layer::Luminosity:
    default:
        // Phase 2 暂不支持, fallback 到 Normal
        return blendNormal(base, blend, opacity);
    }
}

} // namespace layers
