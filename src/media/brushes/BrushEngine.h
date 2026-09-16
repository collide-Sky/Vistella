// SPDX-License-Identifier: MIT
//
// BrushEngine - P1.1 (2026-09-15) Brush full implementation
//   CPU-side brush stamp generation + paint + scattering/texture/dual-brush application.
//   All methods are static (no state), so engine is thread-safe and can run on
//   background thread (worker pool) when painting large brushes.
//
//   PS-equivalent stamp math:
//     intensity(d, hardness) = (1 - d/r) ^ (hardness/100 * GAUSSIAN_BIAS)
//     GAUSSIAN_BIAS = 2.0  (PS empirical; 100% hardness -> sharp edge,
//                                    50% hardness -> linear falloff,
//                                    0% hardness -> wide Gaussian-like falloff)
//   For SoftRound/Airbrush/Chalk/Charcoal/Textured/Sampled, shape.type selects
//   a different stamp generation path (see generateStamp() impl).

#pragma once

#include <QImage>
#include <QString>
#include <QtGlobal>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "BrushSettings.h"
#include "BrushShape.h"
#include "BrushDynamics.h"

namespace brushes {

struct BrushPreset;   // forward decl (BrushEngine.h doesn't need full BrushPreset def)

struct StampResult {
    cv::Mat        stamp;       // CV_8UC1, single channel intensity 0..255
    cv::Point2f    originOffset;// sub-pixel offset for high-resolution paint
    int            actualDiameter;
};

class BrushEngine {
public:
    // Generate a stamp at given size + pressure (0..1).
    //   Returns a CV_8UC1 stamp of (diameter x diameter) pixels, single channel.
    //   originOffset is sub-pixel offset for proper alignment at high pressure.
    static StampResult generateStamp(const BrushSettings &s, const BrushShape &shape,
                                     double pressure = 1.0,
                                     bool flipX = false, bool flipY = false);

    // Apply stamp to target (CV_8UC3 BGRA or CV_8UC1 GRAY) at point pt with opacity (0..1).
    //   mode: 0 = Paint (accumulate), 1 = Erase (subtract), 2 = Smudge (mix)
    //   blendMode: 0 = Normal, 1 = Multiply, 2 = Screen (PS transfer modes)
    static void applyStamp(cv::Mat &target, cv::Point pt, const StampResult &stamp,
                           const QColor &color, double opacity, double flow,
                           int mode = 0, int blendMode = 0);

    // Scatter point: randomize position based on scatterX/scatterY and control pressure.
    static cv::Point2f scatterPoint(cv::Point2f origin,
                                   const ScatteringDynamics &s,
                                   double pressure, double radius);

    // Texture: multiply stamp by pattern at current position (uses pattern repeat).
    static cv::Mat applyTexture(const cv::Mat &stamp,
                               const cv::Mat &pattern,
                               const TextureDynamics &t);

    // Dual brush: combine stamp with secondary brush (blend shapes).
    static cv::Mat applyDual(const cv::Mat &stamp,
                             const cv::Mat &secondaryStamp,
                             const DualBrushDynamics &d);

    // Resolve jitter + pressure for a single stamp. Used by BrushTool to compute
    // the final per-step parameters from raw settings + dynamics + pressure.
    struct ResolvedParams {
        int     diameter;
        double  opacity;        // 0..1
        double  flow;           // 0..1
        int     angle;
        int     roundness;
        cv::Point2f scatterOffset;
    };
    static ResolvedParams resolve(const BrushSettings &s, const BrushShape &shape,
                                  const BrushDynamics &d,
                                  double pressure, double pressureFade,
                                  qreal randomSeed);

    // Generate a thumbnail QImage (64x64) showing the stamp at the preset's default size.
    static QImage generateThumbnail(const BrushPreset &p);

private:
    // Generate falloff for given diameter + hardness. Returns CV_32FC1, 0..1.
    static cv::Mat generateFalloff(int diameter, double hardness, ShapeType type);

    // Generate elliptical mask (rotated + rounded) from a circular falloff.
    static cv::Mat generateElliptical(int diameter, double roundness, int angle,
                                      const cv::Mat &falloff);

    // Add noise to stamp (for Chalk/Charcoal).
    static void applyShapeNoise(cv::Mat &stamp, ShapeType type);

    // Clamp helper.
    static int clamp(int v, int lo, int hi);
};

} // namespace brushes