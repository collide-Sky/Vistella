#pragma once

#include <opencv2/core.hpp>

#include "LayerMask.h"

namespace layers {

// Composites a LayerMask onto a layer image in place.
//
// The layer may be:
//   - CV_8UC3 BGR (default for our pipeline)
//   - CV_8UC4 BGRA (text layers etc.)
//
// The output channels are unchanged; only the per-channel values are scaled
// by the effective alpha:
//
//   out(x, y, c) = layer(x, y, c) * alpha(x, y)
//
// where alpha(x, y) is computed from the pixel/vector mask, then multiplied
// by density, optionally inverted, and finally gaussian-blurred by feather.
class LayerMaskRenderer {
public:
    // Apply the mask in place. Returns true if the layer was modified.
    // If the mask is inactive, the call is a no-op and returns false.
    static bool apply(cv::Mat& layer, const LayerMask& mask);

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
