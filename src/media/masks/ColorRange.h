#pragma once

#include <QImage>
#include <QPoint>
#include <QVector>
#include <opencv2/core.hpp>

namespace masks {

// Parameters for the Color Range -> alpha conversion.
struct ColorRangeParams {
    QVector<QPoint> samplePoints;   // image-pixel coordinates of user clicks
    int   fuzziness = 30;            // 0..255 (PS-style Fuzziness)
    bool  invert    = false;         // invert result
};

// Convert one or more sample points to a pixel mask (CV_8UC1).
//
// Algorithm (HSV-based):
//   1. Read each sample's HSV value.
//   2. For each image pixel, compute its HSV distance to the nearest
//      sample. Hue is weighted highest for colour fidelity; saturation/
//      value provide a softer component.
//   3. Map distance to alpha via a fuzziness-controlled threshold:
//        alpha = clamp(255 - distance * 255 / fuzziness, 0..255)
//      so distance 0 -> 255 (opaque) and distance >= fuzziness -> 0.
//   4. Optionally invert.
//
// If samplePoints is empty, the result is an empty matrix.
class ColorRange {
public:
    static cv::Mat computeMask(const QImage& source,
                               const ColorRangeParams& params);
};

}  // namespace masks