#pragma once

#include <opencv2/core.hpp>

namespace masks {

// Refine Edge parameters.
//
//   smooth   0..100   gaussian blur kernel radius
//                       (sigma = smooth / 10 px). Softens the mask
//                       boundary noise.
//   feather  0..100   second gaussian blur with sigma = feather / 10.
//                       Standard "feather" expansion.
//   contrast -100..+100 around 128. 0 = no change. Positive stretches
//                       bright values and darkens the rest (sharper
//                       edge); negative compresses toward mid-gray.
//   shift    -100..+100 contracts/expands the selected region. 0 =
//                       no change. Positive shifts values up (selected
//                       region grows); negative shrinks it.
//
// All four knobs compose: smooth -> feather -> contrast -> shift.
struct RefineEdgeParams {
    int smooth  = 0;
    int feather = 0;
    int contrast = 0;
    int shift    = 0;
};

// Stateless 8-bit mask filter. Output has the same dimensions and type
// as input. An empty input returns empty.
class RefineEdge {
public:
    static cv::Mat apply(const cv::Mat& input, const RefineEdgeParams& p);
};

}  // namespace masks