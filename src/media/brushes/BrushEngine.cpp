// SPDX-License-Identifier: MIT
//
// BrushEngine impl - P1.1 (2026-09-15)

#include "BrushEngine.h"
#include "BrushPreset.h"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <random>

namespace brushes {

// ============================================================================
//  Stamp generation
// ============================================================================

int BrushEngine::clamp(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

StampResult BrushEngine::generateStamp(const BrushSettings &s, const BrushShape &shape,
                                       double pressure, bool flipX, bool flipY)
{
    StampResult r;
    const double p = std::clamp(pressure, 0.0, 1.0);

    // Pressure affects size (PS: pressure-sized brush).
    int diameter = std::max(1, int(s.size * p));
    if (diameter < 1) diameter = 1;
    r.actualDiameter = diameter;

    // 1) Falloff (CV_32FC1, 0..1)
    cv::Mat falloff = generateFalloff(diameter, s.hardness, shape.type);

    // 2) Elliptical shape (rotated + rounded)
    cv::Mat elliptical = generateElliptical(diameter, s.roundness, s.angle, falloff);

    // 3) Shape-specific noise (Chalk/Charcoal)
    applyShapeNoise(elliptical, shape.type);

    // 4) Flip X/Y if requested
    if (flipX) cv::flip(elliptical, elliptical, 1);   // horizontal
    if (flipY) cv::flip(elliptical, elliptical, 0);   // vertical

    // 5) For Textured/Sampled: use sampledBitmap directly
    if ((shape.type == ShapeType::Textured || shape.type == ShapeType::Sampled)
        && !shape.sampledBitmap.empty())
    {
        cv::Mat resized;
        cv::resize(shape.sampledBitmap, resized, cv::Size(diameter, diameter), 0, 0, cv::INTER_LINEAR);
        // Apply hardness as multiplier
        cv::Mat mul = cv::Mat::zeros(resized.size(), CV_32FC1);
        for (int y = 0; y < diameter; ++y) {
            for (int x = 0; x < diameter; ++x) {
                mul.at<float>(y, x) = resized.at<uchar>(y, x) / 255.0f * elliptical.at<float>(y, x);
            }
        }
        elliptical = mul;
    }

    // Convert to CV_8UC1 for applyStamp
    r.stamp = cv::Mat(diameter, diameter, CV_8UC1);
    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            r.stamp.at<uchar>(y, x) = (uchar)std::clamp(int(elliptical.at<float>(y, x) * 255.0f + 0.5f), 0, 255);
        }
    }

    r.originOffset = cv::Point2f(0.0f, 0.0f);   // future: sub-pixel via interpolation
    return r;
}

cv::Mat BrushEngine::generateFalloff(int diameter, double hardness, ShapeType type)
{
    const int r = (diameter + 1) / 2;  // radius
    cv::Mat m(diameter, diameter, CV_32FC1);
    const double h = std::clamp(hardness, 0.0, 100.0) / 100.0;

    // PS-falloff: (1 - d/r) ^ (GAUSSIAN_BIAS * h)
    //   h=1 -> (1-d/r)^2  (sharp-ish edge, but PS has soft edge)
    //   h=0.5 -> (1-d/r)^1 (linear)
    //   h=0 -> (1-d/r)^0 = 1 (full coverage, very soft)
    // For SoftRound/Airbrush, force low hardness effectively:
    double effective_h = h;
    if (type == ShapeType::Airbrush)  effective_h = std::min(effective_h, 0.10);
    if (type == ShapeType::SoftRound) effective_h = std::min(effective_h, 0.30);

    // GAUSSIAN_BIAS empirically chosen: 100% -> still has 1px softness for anti-aliasing
    const double GAUSSIAN_BIAS = 1.4 + 0.6 * effective_h;
    const double center = (diameter - 1) * 0.5;

    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            const double dx = x - center;
            const double dy = y - center;
            const double d  = std::sqrt(dx * dx + dy * dy);
            if (d >= r) {
                m.at<float>(y, x) = 0.0f;
            } else {
                const double ratio = d / r;
                const double intensity = std::pow(1.0 - ratio, GAUSSIAN_BIAS);
                m.at<float>(y, x) = float(std::clamp(intensity, 0.0, 1.0));
            }
        }
    }
    return m;
}

cv::Mat BrushEngine::generateElliptical(int diameter, double roundness,
                                         int angle, const cv::Mat &falloff)
{
    const double round = std::clamp(roundness, 0.0, 100.0) / 100.0;
    if (round >= 0.99 && angle % 360 == 0) return falloff;   // no-op

    cv::Mat result(diameter, diameter, CV_32FC1);
    result.setTo(0.0f);

    const int r = (diameter + 1) / 2;
    const double theta = angle * CV_PI / 180.0;
    const double cos_t = std::cos(theta);
    const double sin_t = std::sin(theta);
    const double center = (diameter - 1) * 0.5;
    const double minor_r = r * (0.05 + 0.95 * round);   // 0 roundness = flat line, 100 = full circle

    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            const double dx = x - center;
            const double dy = y - center;
            // Rotate (inverse)
            const double lx =  dx * cos_t + dy * sin_t;
            const double ly = -dx * sin_t + dy * cos_t;
            // Ellipse distance (approximated by bounding box check)
            const double ex = std::abs(lx) / r;
            const double ey = std::abs(ly) / std::max(0.5, minor_r);
            if (ex >= 1.0 || ey >= 1.0) {
                result.at<float>(y, x) = 0.0f;
                continue;
            }
            // Map ellipse back to circle distance (approximate)
            const double dist = std::sqrt(lx * lx + ly * ly);
            if (dist >= r) {
                result.at<float>(y, x) = 0.0f;
            } else {
                // Reuse falloff at this distance (approximate)
                const int src_x = clamp(int(center + lx), 0, diameter - 1);
                const int src_y = clamp(int(center + ly), 0, diameter - 1);
                result.at<float>(y, x) = falloff.at<float>(src_y, src_x);
            }
        }
    }
    return result;
}

void BrushEngine::applyShapeNoise(cv::Mat &stamp, ShapeType type)
{
    if (stamp.empty()) return;
    static thread_local std::mt19937 rng(0xC0FFEE);
    if (type == ShapeType::Chalk) {
        // Add ~15% noise (random dropouts)
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int y = 0; y < stamp.rows; ++y) {
            for (int x = 0; x < stamp.cols; ++x) {
                if (dist(rng) < 0.15f) {
                    stamp.at<float>(y, x) *= 0.5f;
                }
            }
        }
    } else if (type == ShapeType::Charcoal) {
        // Add 25% grain (high-frequency noise)
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int y = 0; y < stamp.rows; ++y) {
            for (int x = 0; x < stamp.cols; ++x) {
                const float grain = dist(rng) * 0.3f + 0.85f;
                stamp.at<float>(y, x) *= grain;
            }
        }
    }
    // Round/SoftRound/Flat/Fan/Airbrush/Textured/Sampled: no noise
}

// ============================================================================
//  Paint application
// ============================================================================

void BrushEngine::applyStamp(cv::Mat &target, cv::Point pt, const StampResult &stamp,
                             const QColor &color, double opacity, double flow,
                             int mode, int blendMode)
{
    if (stamp.stamp.empty() || target.empty()) return;

    const int dia = stamp.actualDiameter;
    if (dia <= 0) return;
    const int r = dia / 2;
    const int x0 = pt.x - r;
    const int y0 = pt.y - r;
    const int x1 = x0 + dia;
    const int y1 = y0 + dia;

    // Clip to target
    const int tx0 = std::max(0, x0);
    const int ty0 = std::max(0, y0);
    const int tx1 = std::min(target.cols, x1);
    const int ty1 = std::min(target.rows, y1);
    if (tx0 >= tx1 || ty0 >= ty1) return;

    const double alpha = std::clamp(opacity, 0.0, 1.0) * std::clamp(flow, 0.0, 1.0);
    const uchar r_color = (uchar)(color.red());
    const uchar g_color = (uchar)(color.green());
    const uchar b_color = (uchar)(color.blue());

    const int ch = target.channels();
    const bool useColor = (color.alpha() > 0);
    const bool grayscale = (ch == 1);

    for (int y = ty0; y < ty1; ++y) {
        for (int x = tx0; x < tx1; ++x) {
            const int sx = x - x0;
            const int sy = y - y0;
            const uchar sval = stamp.stamp.at<uchar>(sy, sx);
            if (sval == 0) continue;
            const double w = (sval / 255.0) * alpha;

            if (mode == 1) {  // Erase
                if (grayscale) {
                    target.at<uchar>(y, x) = (uchar)std::clamp(int(target.at<uchar>(y, x) * (1.0 - w)), 0, 255);
                } else {
                    auto &p = target.at<cv::Vec3b>(y, x);
                    for (int c = 0; c < 3; ++c) p[c] = (uchar)std::clamp(int(p[c] * (1.0 - w)), 0, 255);
                }
            } else if (mode == 2) {  // Smudge: future use
                // For now, treat as paint
                if (grayscale) {
                    uchar &p = target.at<uchar>(y, x);
                    p = (uchar)std::clamp(int(p * (1.0 - w) + 255 * w), 0, 255);
                } else {
                    auto &p = target.at<cv::Vec3b>(y, x);
                    for (int c = 0; c < 3; ++c) p[c] = (uchar)std::clamp(int(p[c] * (1.0 - w) + 255 * w), 0, 255);
                }
            } else {  // Paint
                if (grayscale) {
                    uchar &p = target.at<uchar>(y, x);
                    p = (uchar)std::clamp(int(p * (1.0 - w) + 255 * w), 0, 255);
                } else if (useColor) {
                    auto &p = target.at<cv::Vec3b>(y, x);
                    if (blendMode == 1) {  // Multiply
                        for (int c = 0; c < 3; ++c) p[c] = (uchar)std::clamp(int(p[c] * (1.0 - w) + (p[c] * (int)b_color / 255) * w), 0, 255);
                    } else if (blendMode == 2) {  // Screen
                        for (int c = 0; c < 3; ++c) p[c] = (uchar)std::clamp(int(p[c] * (1.0 - w) + (255 - ((255 - p[c]) * (255 - (int)b_color) / 255)) * w), 0, 255);
                    } else {  // Normal
                        p[0] = (uchar)std::clamp(int(p[0] * (1.0 - w) + (int)b_color * w), 0, 255);
                        p[1] = (uchar)std::clamp(int(p[1] * (1.0 - w) + (int)g_color * w), 0, 255);
                        p[2] = (uchar)std::clamp(int(p[2] * (1.0 - w) + (int)r_color * w), 0, 255);
                    }
                } else {
                    // No color = white paint (smudge-like)
                    auto &p = target.at<cv::Vec3b>(y, x);
                    for (int c = 0; c < 3; ++c) p[c] = (uchar)std::clamp(int(p[c] * (1.0 - w) + 255 * w), 0, 255);
                }
            }
        }
    }
}

// ============================================================================
//  Scatter
// ============================================================================

cv::Point2f BrushEngine::scatterPoint(cv::Point2f origin, const ScatteringDynamics &s,
                                      double pressure, double radius)
{
    static thread_local std::mt19937 rng(0x5C4FFE);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    const double sx = s.scatterX / 100.0;
    const double sy = s.bothAxes ? s.scatterY / 100.0 : 0.0;

    // Pressure-based scatter: more scatter with more pressure
    const double pressure_factor = (s.scatterControl == JitterControl::PenPressure)
                                   ? std::clamp(pressure, 0.0, 1.0)
                                   : 1.0;
    const double dx = dist(rng) * sx * radius * pressure_factor;
    const double dy = dist(rng) * sy * radius * pressure_factor;
    return cv::Point2f(origin.x + float(dx), origin.y + float(dy));
}

// ============================================================================
//  Texture (multiply stamp by pattern)
// ============================================================================

cv::Mat BrushEngine::applyTexture(const cv::Mat &stamp, const cv::Mat &pattern,
                                  const TextureDynamics &t)
{
    if (stamp.empty() || pattern.empty()) return stamp;

    cv::Mat resized;
    if (stamp.size() != pattern.size()) {
        cv::resize(pattern, resized, stamp.size(), 0, 0, cv::INTER_LINEAR);
    } else {
        resized = pattern;
    }

    cv::Mat out = stamp.clone();
    const double depth = std::clamp((t.depthMax - t.depthMin) / 100.0, 0.0, 1.0);

    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            const uchar pval = resized.at<uchar>(y, x);
            const double factor = t.invert ? (255 - pval) / 255.0 : pval / 255.0;
            // Multiply stamp by (factor * depth) blended with full stamp
            const double blend = factor * depth;
            const double newval = out.at<uchar>(y, x) * (1.0 - blend + blend * factor);
            out.at<uchar>(y, x) = (uchar)std::clamp(int(newval + 0.5), 0, 255);
        }
    }
    return out;
}

// ============================================================================
//  Dual brush (blend two stamps)
// ============================================================================

cv::Mat BrushEngine::applyDual(const cv::Mat &stamp, const cv::Mat &secondary,
                                const DualBrushDynamics &d)
{
    if (secondary.empty() || !d.enabled) return stamp;
    cv::Mat out = stamp.clone();
    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            const int sval = out.at<uchar>(y, x);
            const int dval = secondary.at<uchar>(y, x);
            // PS dual brush: lighten (max) blend at scatter % density
            out.at<uchar>(y, x) = (uchar)std::max(sval, dval);
        }
    }
    return out;
}

// ============================================================================
//  Resolve jitter + pressure
// ============================================================================

BrushEngine::ResolvedParams BrushEngine::resolve(const BrushSettings &s, const BrushShape &shape,
                                                 const BrushDynamics &d, double pressure,
                                                 double pressureFade, qreal randomSeed)
{
    static thread_local std::mt19937 rng(0xDEADBEEF);
    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    ResolvedParams r;
    r.angle      = s.angle;
    r.roundness  = s.roundness;

    // Size jitter
    double size_factor = 1.0;
    if (d.shape.sizeJitterMax > 0) {
        const double lo = d.shape.sizeJitterMin / 100.0;
        const double hi = d.shape.sizeJitterMax / 100.0;
        double j = lo + dist01(rng) * (hi - lo);
        if (d.shape.sizeControl == JitterControl::PenPressure) j *= pressure;
        size_factor = 1.0 - j;
    }
    r.diameter = std::max(1, int(s.size * size_factor * pressure));

    // Opacity jitter
    double opacity = s.opacity / 100.0;
    if (d.transfer.opacityJitterMax > 0) {
        const double lo = d.transfer.opacityJitterMin / 100.0;
        const double hi = d.transfer.opacityJitterMax / 100.0;
        double j = lo + dist01(rng) * (hi - lo);
        if (d.transfer.opacityControl == JitterControl::PenPressure) j *= pressure;
        opacity *= (1.0 - j);
    }
    opacity = std::clamp(opacity, 0.0, 1.0);

    // Flow jitter
    double flow = s.flow / 100.0;
    if (d.transfer.flowJitterMax > 0) {
        const double lo = d.transfer.flowJitterMin / 100.0;
        const double hi = d.transfer.flowJitterMax / 100.0;
        double j = lo + dist01(rng) * (hi - lo);
        if (d.transfer.flowControl == JitterControl::PenPressure) j *= pressure;
        flow *= (1.0 - j);
    }
    flow = std::clamp(flow, 0.0, 1.0);

    r.opacity = opacity;
    r.flow    = flow;

    // Scatter offset (computed externally via scatterPoint during paint loop)
    r.scatterOffset = cv::Point2f(0, 0);

    (void)pressureFade;   // future: cumulative fade for Fade control
    (void)randomSeed;
    return r;
}

// ============================================================================
//  Thumbnail
// ============================================================================

QImage BrushEngine::generateThumbnail(const BrushPreset &p)
{
    const int THUMB = 64;
    QImage img(THUMB, THUMB, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    StampResult stamp = generateStamp(p.settings, p.shape, 1.0);
    const int dia = stamp.actualDiameter;
    if (dia <= 0) return img;

    QImage stampImg(dia, dia, QImage::Format_ARGB32_Premultiplied);
    stampImg.fill(Qt::transparent);
    for (int y = 0; y < dia; ++y) {
        for (int x = 0; x < dia; ++x) {
            const uchar a = stamp.stamp.at<uchar>(y, x);
            if (a > 0) {
                stampImg.setPixelColor(x, y, QColor(120, 120, 120, a));
            }
        }
    }
    QImage scaled = stampImg.scaled(QSize(THUMB - 4, THUMB - 4), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&img);
    painter.drawImage((THUMB - scaled.width()) / 2, (THUMB - scaled.height()) / 2, scaled);

    return img;
}

} // namespace brushes