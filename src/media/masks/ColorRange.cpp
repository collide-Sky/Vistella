#include "ColorRange.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace masks {

namespace {

// Read the HSV triple of the pixel at (x, y) in BGR. Channels H are
// 0..179, S/V are 0..255.
struct Hsv {
    double h, s, v;
};

Hsv readHsv(const cv::Mat& bgr, int x, int y) {
    const cv::Vec3b px = bgr.at<cv::Vec3b>(y, x);
    cv::Mat3b rgb(1, 1);
    rgb(0, 0) = cv::Vec3b(px[2], px[1], px[0]);  // BGR -> RGB
    cv::Mat hsv;
    cv::cvtColor(rgb, hsv, cv::COLOR_RGB2HSV);
    return {double(hsv.at<cv::Vec3b>(0, 0)[0]),
            double(hsv.at<cv::Vec3b>(0, 0)[1]),
            double(hsv.at<cv::Vec3b>(0, 0)[2])};
}

// HSV distance with hue treated as a circle (shortest arc).
// Saturation/value are weighted so a low-saturation pixel does not match
// a high-saturation sample (and vice versa).
double hsvDistance(const Hsv& a, const Hsv& b) {
    const double dh_raw = std::fabs(a.h - b.h);
    const double dh = dh_raw > 90.0 ? 180.0 - dh_raw : dh_raw;
    // Hue weight ramps up with the smaller of the two saturations:
    //   if either side is desaturated, hue carries little information.
    const double satMin = std::min(a.s, b.s);
    const double hWeight = satMin / 255.0;
    const double hComponent = dh * (1.5 + hWeight * 4.5);   // 1.5..6.0
    const double sComponent = std::fabs(a.s - b.s) * 1.0;
    const double vComponent = std::fabs(a.v - b.v) * 1.0;
    return hComponent + sComponent + vComponent;
}

cv::Mat qImageToBgr(const QImage& img) {
    switch (img.format()) {
        case QImage::Format_RGB32:
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied: {
            cv::Mat rgba(img.height(), img.width(), CV_8UC4,
                         (void*)img.bits(), img.bytesPerLine());
            cv::Mat bgr;
            cv::cvtColor(rgba, bgr, cv::COLOR_BGRA2BGR);
            return bgr;
        }
        default: {
            const QImage conv = img.convertToFormat(QImage::Format_ARGB32);
            cv::Mat rgba(conv.height(), conv.width(), CV_8UC4,
                         (void*)conv.bits(), conv.bytesPerLine());
            cv::Mat bgr;
            cv::cvtColor(rgba, bgr, cv::COLOR_BGRA2BGR);
            return bgr;
        }
    }
}

}  // namespace

cv::Mat ColorRange::computeMask(const QImage& source,
                                const ColorRangeParams& params) {
    if (source.isNull()) return cv::Mat();
    if (params.samplePoints.isEmpty()) return cv::Mat();

    const cv::Mat bgr = qImageToBgr(source);
    if (bgr.empty()) return cv::Mat();

    // 1. Collect sample HSV values.
    QVector<Hsv> samples;
    samples.reserve(params.samplePoints.size());
    for (const QPoint& p : params.samplePoints) {
        if (p.x() < 0 || p.x() >= bgr.cols || p.y() < 0 || p.y() >= bgr.rows) continue;
        samples.append(readHsv(bgr, p.x(), p.y()));
    }
    if (samples.isEmpty()) return cv::Mat();

    // 2. Per-pixel nearest-sample HSV distance.
    cv::Mat dist(bgr.rows, bgr.cols, CV_32F, cv::Scalar(1e9));
    const int N = bgr.rows * bgr.cols;
    float* distPtr = dist.ptr<float>();

    for (int y = 0; y < bgr.rows; ++y) {
        for (int x = 0; x < bgr.cols; ++x) {
            const Hsv px = readHsv(bgr, x, y);
            double best = 1e9;
            for (const Hsv& s : samples) {
                const double d = hsvDistance(px, s);
                if (d < best) best = d;
            }
            distPtr[y * bgr.cols + x] = float(best);
        }
    }

    // 3. Distance -> alpha with fuzziness gating.
    const double fuzz = std::max(1, params.fuzziness);
    cv::Mat alpha(bgr.rows, bgr.cols, CV_8UC1);
    for (int i = 0; i < N; ++i) {
        const double d = distPtr[i];
        double v = 255.0 - (d * 255.0) / fuzz;
        v = std::clamp(v, 0.0, 255.0);
        if (params.invert) v = 255.0 - v;
        alpha.data[i] = uchar(v);
    }
    return alpha;
}

}  // namespace masks