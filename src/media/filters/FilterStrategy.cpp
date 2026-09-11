// SPDX-License-Identifier: MIT
//
// FilterStrategy implementations - P0-5 (2026-09-10)
//
// 20 滤镜实装. 复用 ImageProcessor 已有 gaussianBlur/medianBlur/bilateralFilter/sharpen,
// 其他用 OpenCV primitive 实现 (Sobel/Laplacian/lookup table).
//
#include "FilterStrategy.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <opencv2/imgproc.hpp>

namespace filter {

const char* filterName(FilterKind k)
{
    switch (k) {
    case FilterKind::GaussianBlur:  return "高斯模糊";
    case FilterKind::BoxBlur:       return "方框模糊";
    case FilterKind::MedianBlur:    return "中值模糊";
    case FilterKind::BilateralBlur: return "双边模糊";
    case FilterKind::Sharpen:       return "锐化";
    case FilterKind::SharpenMore:   return "进一步锐化";
    case FilterKind::UnsharpMask:   return "反锐化蒙版";
    case FilterKind::Emboss:        return "浮雕";
    case FilterKind::FindEdges:     return "查找边缘";
    case FilterKind::GlowingEdges:  return "照亮边缘";
    case FilterKind::Desaturate:    return "去色";
    case FilterKind::Invert:        return "反相";
    case FilterKind::Threshold:     return "阈值";
    case FilterKind::Posterize:     return "色调分离";
    case FilterKind::GradientMap:   return "渐变映射";
    case FilterKind::PhotoFilter:   return "照片滤镜";
    case FilterKind::BlurMore:      return "进一步模糊";
    case FilterKind::HighPass:      return "高反差保留";
    case FilterKind::Solarize:      return "曝光过度";
    case FilterKind::FilterGallery: return "滤镜画廊";
    }
    return "未知滤镜";
}

// ===== 4 模糊 =====
void GaussianBlurFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    int k = (ksize < 1) ? 1 : (ksize % 2 == 0 ? ksize + 1 : ksize);   // odd >= 1
    ImageProcessor::gaussianBlur(in, out, k, sigma);
}
QString GaussianBlurFilter::paramText() const
{
    return QStringLiteral("ksize=%1 sigma=%2").arg(ksize).arg(sigma);
}

void BoxBlurFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    int k = (ksize < 1) ? 1 : ksize;
    cv::blur(in, out, cv::Size(k, k));
}
QString BoxBlurFilter::paramText() const { return QStringLiteral("ksize=%1").arg(ksize); }

void MedianBlurFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    int k = (ksize < 1) ? 1 : (ksize % 2 == 0 ? ksize + 1 : ksize);
    ImageProcessor::medianBlur(in, out, k);
}
QString MedianBlurFilter::paramText() const { return QStringLiteral("ksize=%1").arg(ksize); }

void BilateralBlurFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    ImageProcessor::bilateralFilter(in, out, d, sigmaColor, sigmaSpace);
}
QString BilateralBlurFilter::paramText() const
{
    return QStringLiteral("d=%1 sigC=%2 sigS=%3").arg(d).arg(sigmaColor).arg(sigmaSpace);
}

// ===== 3 锐化 =====
void SharpenFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    ImageProcessor::sharpen(in, out);
}

void SharpenMoreFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    // SharpenMore = Sharpen 跑 2 次 (PS 行为)
    cv::Mat tmp;
    ImageProcessor::sharpen(in, tmp);
    ImageProcessor::sharpen(tmp, out);
}

void UnsharpMaskFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    // Unsharp mask: blurred = GaussianBlur(in, radius); mask = in - blurred; out = in + amount * mask
    if (in.empty()) return;
    cv::Mat blurred;
    int k = static_cast<int>(radius * 2.0) | 1;   // odd
    if (k < 1) k = 1;
    cv::GaussianBlur(in, blurred, cv::Size(k, k), radius);
    cv::Mat mask;
    cv::subtract(in, blurred, mask);
    out = in + cv::Scalar::all(0);   // 拷贝 in 到 out (clone)
    cv::addWeighted(in, 1.0, mask, amount, 0.0, out);
    // threshold cut (PS unsharp mask threshold 参数, 0 = no cut)
    if (threshold > 0) {
        for (int y = 0; y < out.rows; ++y) {
            for (int x = 0; x < out.cols; ++x) {
                const cv::Vec3b m = mask.at<cv::Vec3b>(y, x);
                if (std::abs(m[0]) < threshold) out.at<cv::Vec3b>(y, x) = in.at<cv::Vec3b>(y, x);
            }
        }
    }
}
QString UnsharpMaskFilter::paramText() const
{
    return QStringLiteral("amount=%1 radius=%2 thresh=%3").arg(amount).arg(radius).arg(threshold);
}

// ===== 3 风格化 =====
void EmbossFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    // 浮雕 kernel (PS 经典)
    cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
        -2, -1,  0,
        -1,  1,  1,
         0,  1,  2);
    cv::filter2D(in, out, CV_8U, kernel);
    // 偏置到 128 灰色 (PS 行为)
    cv::add(out, cv::Scalar(128, 128, 128), out);
}

void FindEdgesFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    // Find Edges = edge detection (Laplacian) -> gray + invert (PS 行为)
    cv::Mat gray;
    if (in.channels() == 3) {
        cv::cvtColor(in, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = in;
    }
    cv::Mat edges;
    cv::Laplacian(gray, edges, CV_8U, 3);
    cv::bitwise_not(edges, edges);
    if (out.channels() == 3 && in.channels() == 3) {
        cv::cvtColor(edges, out, cv::COLOR_GRAY2BGR);
    } else {
        out = edges;
    }
}

void GlowingEdgesFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    // Glowing Edges = GaussianBlur(edges) 模糊 (PS 行为)
    cv::Mat gray;
    if (in.channels() == 3) {
        cv::cvtColor(in, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = in;
    }
    cv::Mat edges;
    cv::Laplacian(gray, edges, CV_8U, 3);
    cv::GaussianBlur(edges, edges, cv::Size(5, 5), 1.0);
    cv::bitwise_not(edges, edges);
    if (out.channels() == 3 && in.channels() == 3) {
        cv::cvtColor(edges, out, cv::COLOR_GRAY2BGR);
    } else {
        out = edges;
    }
}

// ===== 5 颜色 =====
void DesaturateFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    if (in.channels() == 1) {
        in.copyTo(out);
        return;
    }
    cv::Mat gray;
    cv::cvtColor(in, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gray, out, cv::COLOR_GRAY2BGR);
}

void InvertFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    cv::bitwise_not(in, out);
}

void ThresholdFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    cv::Mat gray;
    if (in.channels() == 3) {
        cv::cvtColor(in, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = in;
    }
    cv::Mat thresh;
    cv::threshold(gray, thresh, level, 255, cv::THRESH_BINARY);
    if (in.channels() == 3) {
        cv::cvtColor(thresh, out, cv::COLOR_GRAY2BGR);
    } else {
        out = thresh;
    }
}
QString ThresholdFilter::paramText() const { return QStringLiteral("level=%1").arg(level); }

void PosterizeFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    int lv = (levels < 2) ? 2 : levels;
    // 减少每通道灰度级
    int step = 256 / lv;
    in.convertTo(out, CV_8U);
    if (out.channels() == 1) {
        out = out / step * step + step / 2;
    } else {
        std::vector<cv::Mat> ch;
        cv::split(out, ch);
        for (auto& c : ch) c = c / step * step + step / 2;
        cv::merge(ch, out);
    }
}
QString PosterizeFilter::paramText() const { return QStringLiteral("levels=%1").arg(levels); }

void GradientMapFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    // P0 简化: 灰度 -> 灰度反转 (PS 默认黑白渐变映射效果)
    if (in.empty()) return;
    cv::Mat gray;
    if (in.channels() == 3) {
        cv::cvtColor(in, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = in;
    }
    out = gray;
    cv::LUT(gray, cv::Mat(1, 256, CV_8U), out);  // identity LUT placeholder
    if (in.channels() == 3) {
        cv::cvtColor(out, out, cv::COLOR_GRAY2BGR);
    }
}
QString GradientMapFilter::paramText() const { return QStringLiteral("默认黑白"); }

// ===== 5 其他 =====
void PhotoFilterFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    cv::Mat lut = ImageProcessor::buildColorBalanceLUT(cyanRed, magentaGreen, yellowBlue);
    if (in.channels() == 1) {
        cv::LUT(in, lut, out);
    } else {
        // 对每通道分别调 (buildColorBalanceLUT 是单通道 LUT)
        std::vector<cv::Mat> ch;
        cv::split(in, ch);
        for (size_t i = 0; i < ch.size(); ++i) {
            cv::LUT(ch[i], lut, ch[i]);
        }
        cv::merge(ch, out);
    }
    // 混合原图 (density 0..100)
    if (density < 100) {
        double alpha = density / 100.0;
        cv::addWeighted(in, 1.0 - alpha, out, alpha, 0.0, out);
    }
}
QString PhotoFilterFilter::paramText() const
{
    return QStringLiteral("R=%1 G=%2 B=%3 density=%4").arg(cyanRed).arg(magentaGreen).arg(yellowBlue).arg(density);
}

void BlurMoreFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    // BlurMore = GaussianBlur 跑 3 次 (PS 行为)
    cv::Mat tmp;
    int k = 5;
    ImageProcessor::gaussianBlur(in, tmp, k, 1.0);
    ImageProcessor::gaussianBlur(tmp, out, k, 1.0);
    ImageProcessor::gaussianBlur(out, out, k, 1.0);
}

void HighPassFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    cv::Mat blurred;
    int k = static_cast<int>(radius * 2.0) | 1;
    if (k < 1) k = 1;
    cv::GaussianBlur(in, blurred, cv::Size(k, k), radius);
    // high pass = original - blurred + 128 (PS: 显示灰色底, 边缘清晰)
    cv::Mat mask;
    cv::subtract(in, blurred, mask);
    out = cv::Mat(in.size(), in.type(), cv::Scalar(128, 128, 128));
    if (in.channels() == 1) {
        cv::add(out, mask, out);
    } else {
        std::vector<cv::Mat> ch(3);
        cv::split(out, ch);
        std::vector<cv::Mat> mch;
        cv::split(mask, mch);
        for (size_t i = 0; i < 3; ++i) cv::add(ch[i], mch[i], ch[i]);
        cv::merge(ch, out);
    }
}
QString HighPassFilter::paramText() const { return QStringLiteral("radius=%1").arg(radius); }

void SolarizeFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    if (in.empty()) return;
    in.copyTo(out);
    const int t = static_cast<int>(threshold);
    if (in.channels() == 1) {
        for (int y = 0; y < out.rows; ++y) {
            uchar* row = out.ptr<uchar>(y);
            for (int x = 0; x < out.cols; ++x) {
                if (row[x] > t) row[x] = 255 - row[x];
            }
        }
    } else {
        for (int y = 0; y < out.rows; ++y) {
            cv::Vec3b* row = out.ptr<cv::Vec3b>(y);
            for (int x = 0; x < out.cols; ++x) {
                if (row[x][0] > t) row[x][0] = 255 - row[x][0];
                if (row[x][1] > t) row[x][1] = 255 - row[x][1];
                if (row[x][2] > t) row[x][2] = 255 - row[x][2];
            }
        }
    }
}
QString SolarizeFilter::paramText() const { return QStringLiteral("threshold=%1").arg(threshold); }

void FilterGalleryFilter::apply(const cv::Mat& in, cv::Mat& out)
{
    // P0 阶段 placeholder: 复制输入
    if (in.empty()) return;
    in.copyTo(out);
}
QString FilterGalleryFilter::paramText() const { return QStringLiteral("P1 阶段实装"); }

} // namespace filter
