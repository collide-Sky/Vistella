#include "RefineEdge.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace masks {

namespace {

// OpenCV requires odd kernel sizes, >= 3. sigma=0 -> use a small
// default kernel and let OpenCV compute sigma internally.
inline cv::Size kernelFor(qreal sigma) {
    if (sigma <= 0.0) return cv::Size(0, 0);  // 0 -> no-op
    int k = int(std::ceil(sigma * 2.0 * 3.0)) | 1;  // 3*sigma each side, odd
    if (k < 3) k = 3;
    return cv::Size(k, k);
}

// Apply smooth (noise reduction) via gaussian blur with sigma = smooth/10.
cv::Mat applySmooth(const cv::Mat& in, int smooth) {
    if (smooth <= 0) return in;
    const qreal sigma = qreal(smooth) / 10.0;
    const cv::Size k = kernelFor(sigma);
    if (k.width == 0) return in;
    cv::Mat out;
    cv::GaussianBlur(in, out, k, sigma, sigma);
    return out;
}

// Apply feather via a second gaussian blur with sigma = feather/10.
// Layered on top of smooth so the user can have both at once.
cv::Mat applyFeather(const cv::Mat& in, int feather) {
    if (feather <= 0) return in;
    const qreal sigma = qreal(feather) / 10.0;
    const cv::Size k = kernelFor(sigma);
    if (k.width == 0) return in;
    cv::Mat out;
    cv::GaussianBlur(in, out, k, sigma, sigma);
    return out;
}

// Contrast: linear stretch around the midpoint 128.
// contrast in [-100, +100] -> gain in [0.5, 1.5] (1.0 == no change).
cv::Mat applyContrast(const cv::Mat& in, int contrast) {
    if (contrast == 0) return in;
    const qreal gain = 1.0 + qreal(contrast) / 200.0;  // +-50% at extremes
    cv::Mat out(in.size(), in.type());
    const int N = in.rows * in.cols;
    const uchar* src = in.data;
    uchar* dst = out.data;
    for (int i = 0; i < N; ++i) {
        const qreal v = qreal(src[i]);
        const qreal c = (v - 128.0) * gain + 128.0;
        dst[i] = uchar(std::clamp(int(std::lround(c)), 0, 255));
    }
    return out;
}

// Shift Edge: signed additive shift on the alpha channel. Positive
// expands the selected region (more pixels reach 255), negative
// contracts it (more pixels reach 0). Internally a small bias is
// applied before clamping so the boundary still moves.
cv::Mat applyShift(const cv::Mat& in, int shift) {
    if (shift == 0) return in;
    const int bias = qBound(-100, shift, 100) * 2;  // +/-200 max
    cv::Mat out(in.size(), in.type());
    const int N = in.rows * in.cols;
    const uchar* src = in.data;
    uchar* dst = out.data;
    for (int i = 0; i < N; ++i) {
        const int v = int(src[i]) + bias;
        dst[i] = uchar(std::clamp(v, 0, 255));
    }
    return out;
}

}  // namespace

cv::Mat RefineEdge::apply(const cv::Mat& input, const RefineEdgeParams& p) {
    if (input.empty()) return cv::Mat();
    cv::Mat m = input.clone();
    if (p.smooth  > 0) m = applySmooth(m,  p.smooth);
    if (p.feather > 0) m = applyFeather(m, p.feather);
    if (p.contrast != 0) m = applyContrast(m, p.contrast);
    if (p.shift   != 0) m = applyShift(m,   p.shift);
    return m;
}

}  // namespace masks