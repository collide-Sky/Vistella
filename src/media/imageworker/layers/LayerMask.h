#pragma once

#include <QPainterPath>
#include <QVector>
#include <opencv2/core.hpp>

namespace layers {

// Per-layer mask state. Replaces the older pair of `cv::Mat layerMask` + 
// `bool maskEnabled` fields on Layer; a layer without a mask keeps
// `kind == None` and no data.
//
// Pixel mask: 8-bit single-channel cv::Mat matching the layer dimensions.
//   0   = fully hidden
//   128 = half-transparent
//   255 = fully visible
//
// Vector mask: a set of painter paths. Path interiors count as visible.
//   Multiple paths combine by union unless the caller applies boolean
//   ops explicitly.
//
// Common state (applies to both kinds):
//   enabled  - if false, the mask has no effect
//   density  - 0..1 multiplier on the effective alpha
//   feather  - gaussian edge softening radius in pixels (applied via
//              OpenCV GaussianBlur on the composed alpha)
//   invert   - swap visible vs hidden
struct LayerMask {
    enum Kind { None = 0, Pixel, Vector };

    Kind                       kind = None;
    bool                       enabled = false;
    cv::Mat                    pixel;          // Pixel mask (CV_8UC1)
    QVector<QPainterPath>      vectorPaths;    // Vector mask
    qreal                      density = 1.0;  // 0..1
    qreal                      feather = 0.0;  // pixels (>= 0)
    bool                       invert = false;

    bool isActive() const {
        return enabled && kind != None && hasData();
    }

    bool hasData() const {
        return (kind == Pixel && !pixel.empty())
            || (kind == Vector && !vectorPaths.isEmpty());
    }

    void clear() {
        kind = None;
        enabled = false;
        pixel.release();
        vectorPaths.clear();
        density = 1.0;
        feather = 0.0;
        invert = false;
    }
};

}  // namespace layers
