#ifndef PIXELFORMAT_H
#define PIXELFORMAT_H

// =============================================================
//  PixelFormat -- the layer pixel contract (LYR-01 / LYR-35 / CP-07 / CP-18)
//
//  Contract (docs/CONTRACTS.md, channel contract section):
//
//    1. Layer pixels (Layer::image) are always CV_8UC4 BGRA with STRAIGHT
//       alpha (not premultiplied).
//       Channel normalisation must go through toBgra() only -- never add
//       the alpha channel yourself with cvtColor, because the alpha
//       semantics of those conversions differ (255 / 0 / keep-original),
//       and mixing them brings back the "semi-transparent turns into
//       multiply-by-black" bug (CP-07).
//
//    2. The mask (Layer::mask) is grayscale 8UC1. It does NOT take part in
//       compositing directly: LayerMaskRenderer::buildAlpha() folds it
//       into a coverage value, which is then written into the source
//       layer's alpha channel. The mask and the pixel alpha meet at
//       exactly this one point.
//
//    3. Compositing uses Porter-Duff "over":
//         cov    = src.a * layer opacity
//         out.a  = cov + dst.a * (1 - cov)
//         out.rgb = (src.rgb * cov + dst.rgb * dst.a * (1 - cov)) / out.a
//       The blend mode only decides how src.rgb is computed, never how
//       alpha is computed.
//
//    4. Layer opacity (Layer::opacity) and pixel alpha are two different
//       things: the former is a layer property (multiplied into the
//       coverage), the latter is the pixel's own transparency. Mixing
//       them up is what makes a semi-transparent layer darken the
//       backdrop.
// =============================================================

#include <opencv2/core.hpp>

namespace layers {

// Normalise to CV_8UC4 BGRA (straight alpha).
//   CV_8UC1 -> gray expanded to 3 channels, alpha = 255
//   CV_8UC3 -> alpha = 255
//   CV_8UC4 -> kept as is
//   other depths -> down-converted to 8U first (convertTo rescales the
//                    range automatically), then normalised
cv::Mat toBgra(const cv::Mat &src);

// Split BGRA -> (BGR 3 channels, alpha 1 channel).
//   A non-4-channel input is normalised through toBgra() first.
void splitBgra(const cv::Mat &src, cv::Mat &bgr, cv::Mat &alpha);

// Premultiply / unpremultiply (straight <-> premultiplied).
//   Premultiplied values are only used inside compositing; storing and
//   displaying always use straight alpha.
cv::Mat premultiply(const cv::Mat &bgra);
cv::Mat unpremultiply(const cv::Mat &bgra);

// Porter-Duff "over" compositing (straight alpha).
//   base     : CV_8UC4 BGRA straight alpha (the canvas)
//   srcBgr   : the source layer colour (3 channels), as produced by the
//              blend mode
//   srcAlpha : the source coverage (1 channel, 0..255)
//   opacity  : the layer opacity, 0..1
//   returns CV_8UC4 BGRA with straight alpha
//
//   When both the source alpha and the canvas alpha are fully opaque the
//   fast path is taken (cv::addWeighted, SIMD). That is the common case
//   for most layers (no mask, no built-in transparency).
cv::Mat compositeOver(const cv::Mat &base, const cv::Mat &srcBgr,
                      const cv::Mat &srcAlpha, float opacity);

}   // namespace layers

#endif   // PIXELFORMAT_H
