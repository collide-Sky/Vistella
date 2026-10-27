// SPDX-License-Identifier: MIT
//
// FilterFactory implementation
//
#include "FilterFactory.h"

namespace filter {

std::unique_ptr<FilterStrategy> FilterFactory::createFilter(FilterKind kind)
{
    switch (kind) {
    case FilterKind::GaussianBlur:   return std::make_unique<GaussianBlurFilter>();
    case FilterKind::BoxBlur:        return std::make_unique<BoxBlurFilter>();
    case FilterKind::MedianBlur:     return std::make_unique<MedianBlurFilter>();
    case FilterKind::BilateralBlur:  return std::make_unique<BilateralBlurFilter>();
    case FilterKind::Sharpen:        return std::make_unique<SharpenFilter>();
    case FilterKind::SharpenMore:    return std::make_unique<SharpenMoreFilter>();
    case FilterKind::UnsharpMask:    return std::make_unique<UnsharpMaskFilter>();
    case FilterKind::Emboss:         return std::make_unique<EmbossFilter>();
    case FilterKind::FindEdges:      return std::make_unique<FindEdgesFilter>();
    case FilterKind::GlowingEdges:   return std::make_unique<GlowingEdgesFilter>();
    case FilterKind::Desaturate:     return std::make_unique<DesaturateFilter>();
    case FilterKind::Invert:         return std::make_unique<InvertFilter>();
    case FilterKind::Threshold:      return std::make_unique<ThresholdFilter>();
    case FilterKind::Posterize:      return std::make_unique<PosterizeFilter>();
    case FilterKind::GradientMap:    return std::make_unique<GradientMapFilter>();
    case FilterKind::PhotoFilter:    return std::make_unique<PhotoFilterFilter>();
    case FilterKind::BlurMore:       return std::make_unique<BlurMoreFilter>();
    case FilterKind::HighPass:       return std::make_unique<HighPassFilter>();
    case FilterKind::Solarize:       return std::make_unique<SolarizeFilter>();
    case FilterKind::FilterGallery:  return std::make_unique<FilterGalleryFilter>();
    case FilterKind::AddNoise:       return std::make_unique<AddNoiseFilter>();
    case FilterKind::ReduceNoise:    return std::make_unique<ReduceNoiseFilter>();
    case FilterKind::MedianNoise:    return std::make_unique<MedianNoiseFilter>();
    case FilterKind::WhiteBalance:   return std::make_unique<WhiteBalanceFilter>();

    // PS 风格化菜单 — alias 现有策略 + 简化实装
    // 8 模糊: 表面/方框/高斯/进一步/特殊/形状 走现有 Bilateral/Box/Gaussian.
    // 径向/镜头/扭曲/渲染 大多复杂 (cv::warpPolar + depth map / cv::remap), P7.3
    //  阶段允许 nullptr (菜单 disabled + status message "未实装").
    case FilterKind::SurfaceBlur:    return std::make_unique<BilateralBlurFilter>();
    case FilterKind::MotionBlur:     return std::make_unique<BlurMoreFilter>();      // P7.3 简化: 双 Gaussian
    case FilterKind::Blur:           return std::make_unique<BoxBlurFilter>();
    case FilterKind::Average:        return std::make_unique<BlurMoreFilter>();     // 简化
    case FilterKind::SpecialBlur:    return std::make_unique<BilateralBlurFilter>();
    case FilterKind::ShapeBlur:      return std::make_unique<BoxBlurFilter>();

    case FilterKind::RadialBlur:     return nullptr;   // P7.3 placeholder (P7.9 后续 cv::warpPolar)
    case FilterKind::LensBlur:       return nullptr;   // P7.3 placeholder
    case FilterKind::Twirl:          return nullptr;   // 旋转扭曲
    case FilterKind::Polar:          return nullptr;   // 极坐标
    case FilterKind::Ripple:         return nullptr;   // 波纹
    case FilterKind::Spherize:       return nullptr;   // 球面化
    case FilterKind::Shear:          return nullptr;   // 切变
    case FilterKind::ZigZag:         return nullptr;   // 锯齿
    case FilterKind::Pixelate:       return nullptr;   // 像素化 (cv::resize)
    case FilterKind::Fragment:       return nullptr;   // 彩块化 (K-means)
    case FilterKind::Mezzotint:      return nullptr;   // 铜版雕刻
    case FilterKind::ColorHalftone:  return nullptr;   // 彩色半调
    case FilterKind::LensFlare:      return nullptr;   // 镜头光晕
    case FilterKind::Clouds:         return nullptr;   // 分层云彩
    case FilterKind::DiffuseGlow:    return std::make_unique<GlowingEdgesFilter>();  // 简化
    case FilterKind::DustScratches:  return std::make_unique<ReduceNoiseFilter>();   // 简化 (bilateral)
    }
    return nullptr;
}

} // namespace filter
