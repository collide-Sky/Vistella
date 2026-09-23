// SPDX-License-Identifier: MIT
//
// FilterFactory implementation - P0-5.2 (2026-09-10)
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
    }
    return nullptr;
}

} // namespace filter
