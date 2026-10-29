// =============================================================
//  PixelFormat implementation -- channel normalisation and Porter-Duff
//  "over" compositing.
//  See PixelFormat.h for the contract.
// =============================================================

#include "PixelFormat.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace layers {

namespace {

// Widen a 3-channel BGR image to 4-channel BGRA with alpha = 255.
cv::Mat bgrToBgra(const cv::Mat &bgr)
{
    if (bgr.empty()) return cv::Mat();
    if (bgr.channels() == 4) return bgr;

    std::vector<cv::Mat> ch;
    cv::split(bgr, ch);
    ch.push_back(cv::Mat(bgr.rows, bgr.cols, CV_8U, cv::Scalar(255)));
    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

// Extract the alpha channel of a BGRA image (no copy of the data).
cv::Mat alphaChannelOf(const cv::Mat &bgra)
{
    if (bgra.empty() || bgra.channels() != 4) return cv::Mat();
    std::vector<cv::Mat> ch;
    cv::split(bgra, ch);
    return ch[3];
}

}   // namespace

cv::Mat toBgra(const cv::Mat &src)
{
    if (src.empty()) return src;
    if (src.type() == CV_8UC4) return src;

    cv::Mat work;
    if (src.depth() != CV_8U) {
        // 16bit / 32bit -> 8bit. convertTo rescales the range
        // automatically (16bit 65535 -> 255); do not multiply by 257
        // yourself.
        src.convertTo(work, CV_8U);
    } else {
        work = src;
    }

    switch (work.channels()) {
    case 1: {
        cv::Mat bgr;
        cv::cvtColor(work, bgr, cv::COLOR_GRAY2BGR);
        return bgrToBgra(bgr);
    }
    case 3:
        return bgrToBgra(work);
    case 4:
        return work;
    default:
        return cv::Mat();
    }
}

void splitBgra(const cv::Mat &src, cv::Mat &bgr, cv::Mat &alpha)
{
    const cv::Mat b = toBgra(src);
    if (b.empty()) {
        bgr.release();
        alpha.release();
        return;
    }
    std::vector<cv::Mat> ch;
    cv::split(b, ch);
    ch.resize(4);
    ch[3].copyTo(alpha);
    cv::merge(std::vector<cv::Mat>(ch.begin(), ch.begin() + 3), bgr);
}

cv::Mat premultiply(const cv::Mat &bgra)
{
    const cv::Mat b = toBgra(bgra);
    if (b.empty()) return b;

    std::vector<cv::Mat> ch;
    cv::split(b, ch);
    for (int c = 0; c < 3; ++c) {
        cv::multiply(ch[c], ch[3], ch[c], 1.0 / 255.0);
    }
    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

cv::Mat unpremultiply(const cv::Mat &bgra)
{
    const cv::Mat b = toBgra(bgra);
    if (b.empty()) return b;

    std::vector<cv::Mat> ch;
    cv::split(b, ch);
    for (int c = 0; c < 3; ++c) {
        cv::Mat safeA = ch[3].clone();
        // A pixel with alpha = 0 has no defined colour: keep it at 0
        // (dividing by zero would produce noise).
        cv::compare(safeA, 0, safeA, cv::CMP_GT);
        safeA.convertTo(safeA, CV_32F, 1.0 / 255.0);
        ch[c].convertTo(ch[c], CV_32F);
        cv::divide(ch[c], safeA, ch[c], 1.0 / 255.0);
        ch[c].convertTo(ch[c], CV_8U);
    }
    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

cv::Mat compositeOver(const cv::Mat &base, const cv::Mat &srcBgr,
                      const cv::Mat &srcAlpha, float opacity)
{
    cv::Mat out;
    if (base.empty()) return out;
    base.copyTo(out);
    if (srcBgr.empty()) return out;

    const int op = std::clamp(static_cast<int>(std::lround(opacity * 255.0f)), 0, 255);
    if (op == 0) return out;

    // The alpha channel must have the same size as the colour image;
    // otherwise it is treated as 0 (i.e. fully transparent).
    cv::Mat alpha = srcAlpha;
    if (alpha.empty() || alpha.size() != srcBgr.size()) {
        alpha = cv::Mat(srcBgr.size(), CV_8U, cv::Scalar(255));
    }
    if (alpha.channels() != 1) {
        std::vector<cv::Mat> ch;
        cv::split(alpha, ch);
        alpha = ch[0];
    }

    cv::Mat baseAlpha = alphaChannelOf(base);


    // ---- Fast path: source and canvas are both opaque -> plain lerp (SIMD) ----
    double srcMin = 0.0, srcMax = 0.0;
    cv::minMaxLoc(alpha, &srcMin, &srcMax);
    double baseMin = 0.0, baseMax = 0.0;
    if (!baseAlpha.empty()) cv::minMaxLoc(baseAlpha, &baseMin, &baseMax);

    if (srcMin >= 255.0 && baseMin >= 255.0) {
        cv::Mat baseBgr, dummy;
        splitBgra(base, baseBgr, dummy);

        cv::Mat blended;
        const double o = op / 255.0;
        cv::addWeighted(baseBgr, 1.0 - o, srcBgr, o, 0.0, blended);

        std::vector<cv::Mat> bch;
        cv::split(base, bch);
        std::vector<cv::Mat> rgb;
        cv::split(blended, rgb);
        rgb.push_back(bch[3]);
        cv::merge(rgb, out);
        return out;
    }

    // ---- General path: Porter-Duff over with alpha ----
    for (int y = 0; y < out.rows; ++y) {
        const uchar *b = base.ptr<uchar>(y);
        const uchar *s = srcBgr.ptr<uchar>(y);
        const uchar *sa = alpha.ptr<uchar>(y);
        uchar *o = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols; ++x) {
            const int cov = (static_cast<int>(sa[x]) * op) / 255;
            if (cov == 0) continue;

            const int bA = b[x * 4 + 3];
            const int inv = 255 - cov;
            const int dstTerm = (bA * inv) / 255;      // the backdrop's premultiplied contribution
            const int outA = cov + dstTerm;
            o[x * 4 + 3] = static_cast<uchar>(outA);
            if (outA <= 0) {
                o[x * 4 + 0] = o[x * 4 + 1] = o[x * 4 + 2] = 0;
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                const int num = static_cast<int>(s[x * 3 + c]) * cov
                              + static_cast<int>(b[x * 4 + c]) * dstTerm;
                o[x * 4 + c] = static_cast<uchar>(num / outA);
            }
        }
    }
    return out;
}

}   // namespace layers
