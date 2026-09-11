#ifndef BLENDMODE_H
#define BLENDMODE_H

// =============================================================
// BlendMode — 阶段 1 W4.3 Phase 2 11 种 PS 混合模式 (2026-09-04)
//
// PS 15 种混合模式; Phase 2 实现 11 种基础 + 4 种 HSV 复合 (Hue/Sat/Color/Luminosity):
//   基础 (11):
//     Normal       = alpha blend
//     Multiply     = A * B / 255
//     Screen       = 255 - (255-A) * (255-B) / 255
//     Overlay      = 类似 Multiply/Screen 基于 base 亮度 (>128 用 Screen, <128 用 Multiply)
//     SoftLight    = 柔和版 Overlay (Photoshop 算法)
//     HardLight    = Hard 版 Overlay (对称于 Overlay)
//     ColorDodge   = base / (255 - blend)
//     ColorBurn    = 255 - (255 - base) / blend
//     Darken       = min(A, B)
//     Lighten      = max(A, B)
//     Difference   = |A - B|
//     Exclusion    = A + B - 2*A*B/255
//   HSV 复合 (4, Phase 3):
//     Hue          = blend H 通道 + base S/V
//     Saturation   = blend S 通道 + base H/V
//     Color        = blend H+S + base V
//     Luminosity   = blend V 通道 + base H/S
//
// 设计原则:
//   - 每个 blend 函数签名一致: (base: cv::Mat, blend: cv::Mat, opacity: float) -> cv::Mat
//   - base 跟 blend 同尺寸 + 8U + 3 通道
//   - opacity 0.0 = 全 base, 1.0 = 完全混合
//   - 返回 base 跟 blend 的混合结果 (8U, 3 通道)
//   - Phase 3: 加 HSV 4 种, 改 Layer::BlendMode 枚举
// =============================================================

#include "Layer.h"

#include <opencv2/core.hpp>

namespace layers {

// 通用 blend 签名
//   base: 底图层 (e.g. canvas / 下层)
//   blend: 混合层 (e.g. 当前 layer)
//   opacity: 0.0 - 1.0
//   返回: base 跟 blend 按模式混合结果
cv::Mat blendNormal(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendMultiply(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendScreen(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendOverlay(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendSoftLight(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendHardLight(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendColorDodge(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendColorBurn(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendDarken(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendLighten(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendDifference(const cv::Mat &base, const cv::Mat &blend, float opacity);
cv::Mat blendExclusion(const cv::Mat &base, const cv::Mat &blend, float opacity);

// 调度器: 根据 Layer::BlendMode 选对应函数
//   Phase 3: 4 种 HSV 复合, 走 forward 声明
cv::Mat applyBlend(Layer::BlendMode mode, const cv::Mat &base, const cv::Mat &blend, float opacity);

// 工具: 校验 base 跟 blend 尺寸 / 通道匹配
inline bool canBlend(const cv::Mat &base, const cv::Mat &blend)
{
    return !base.empty() && !blend.empty()
           && base.size() == blend.size()
           && base.type() == CV_8UC3
           && blend.type() == CV_8UC3;
}

} // namespace layers

#endif // BLENDMODE_H
