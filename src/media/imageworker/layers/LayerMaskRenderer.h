#pragma once

#include <QSize>   // API 里直接用了 QSize, 不能靠传递包含

#include <opencv2/core.hpp>

#include "LayerMask.h"

namespace layers {

// Fold a LayerMask into a coverage value and write it into the alpha
// channel of the layer pixels (CP-07 / CP-18).
//
// Contract (see PixelFormat.h):
//   - Layer pixels are always CV_8UC4 BGRA with STRAIGHT alpha (not
//     premultiplied)
//   - The mask is grayscale 8UC1 and does NOT take part in the
//     compositing directly; it is folded into a coverage value here:
//         out.a = pixel.a * maskCoverage / 255
//   - The compositing (Porter-Duff over) is done by layers::applyBlend
//     / compositeOver
//
// This used to be a per-channel RGB multiply (out.c = layer.c * alpha),
// which is the CP-07 "turns black instead of transparent" bug: covering
// with a mask turned the pixels black, and a semi-transparent mask
// degenerated into multiply-by-black.
// Now the mask only changes the alpha; the colour is decided during
// the compositing step.
class LayerMaskRenderer {
public:
    // Write the mask coverage into the alpha channel of the layer
    // (modified in place).
    //   layer must be CV_8UC4 BGRA (the in-stack layer pixel contract).
    //   A no-op when the mask has no effect; returns false.
    static bool applyAlpha(cv::Mat& layer, const LayerMask& mask);

    // Build an effective alpha image (CV_8UC1) from the mask without
    // applying it. Useful for previews and tests.
    // layerSize: dimensions of the source image the mask covers.
    static cv::Mat buildAlpha(const LayerMask& mask, const QSize& layerSize);

private:
    // Rasterize vector mask paths into a CV_8UC1 alpha image.
    static cv::Mat rasterizeVector(const QVector<QPainterPath>& paths,
                                   const QSize& size);
};

}  // namespace layers
