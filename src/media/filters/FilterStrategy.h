// SPDX-License-Identifier: MIT
//
// FilterStrategy - P0-5 (2026-09-10)
//
// 20 滤镜 Strategy 模式抽象 (PS 同款).
//   - FilterKind enum: 20 个类型
//   - FilterStrategy 抽象: kind() / name() / apply(in, out)
//   - 20 派生类: 每种滤镜一个
//   - 参数化滤镜 (GaussianBlur/Threshold/Posterize/UnsharpMask/PhotoFilter) 用
//     公有成员 setter, FilterDialog 在 apply 前调
//   - 无参滤镜 (Invert/Desaturate/Emboss/FindEdges/GlowingEdges/Sharpen/SharpenMore/...):
//     apply() 直接跑
//
// 模式参考:
//   - RectSelectionStrategy (P0-4.2): gesture lifecycle + compute mask
//   - FilterStrategy 这里是 stateless 转换 (无 gesture, apply(in, out) 1-shot)
//
// 集成:
//   - FilterFactory: createFilter(kind) -> unique_ptr<FilterStrategy>
//   - FilterCommand: QUndoCommand, store before/after image
//   - FilterDialog: non-modal, Apply 实时 preview, OK push command
//   - ImageWindow::applyFilter(kind) -> factory + strategy + command push
//
#pragma once

#include <QString>
#include <opencv2/core.hpp>

namespace filter {

// ===== 20 滤镜类型 (PS 同款) =====
enum class FilterKind {
    // 模糊 (4) — 0..3
    GaussianBlur   = 0,
    BoxBlur        = 1,
    MedianBlur     = 2,
    BilateralBlur  = 3,
    // 锐化 (3) — 4..6
    Sharpen        = 4,
    SharpenMore    = 5,
    UnsharpMask    = 6,
    // 风格化 (3) — 7..9
    Emboss         = 7,
    FindEdges      = 8,
    GlowingEdges   = 9,
    // 颜色 (5) — 10..14
    Desaturate     = 10,
    Invert         = 11,
    Threshold      = 12,
    Posterize      = 13,
    GradientMap    = 14,
    // 其他 (5) — 15..19
    PhotoFilter    = 15,
    BlurMore       = 16,
    HighPass       = 17,
    Solarize       = 18,
    FilterGallery  = 19,
};

// 中文显示名 (PS 风格: 模糊/锐化/浮雕...)
const char* filterName(FilterKind k);

// ===== 抽象 =====
class FilterStrategy
{
public:
    virtual ~FilterStrategy() = default;

    virtual FilterKind kind() const = 0;
    virtual QString name() const = 0;     // 显示名
    virtual void apply(const cv::Mat& in, cv::Mat& out) = 0;

    // 参数化滤镜标记 (GaussianBlur/Threshold/Posterize/UnsharpMask/PhotoFilter 返 true)
    virtual bool hasParam() const { return false; }
    virtual QString paramText() const { return QString(); }
};

// ===== 4 模糊 =====
class GaussianBlurFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::GaussianBlur; }
    QString name() const override { return QStringLiteral("高斯模糊"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;

    int    ksize = 5;       // 1..31, odd
    double sigma = 1.0;
};

class BoxBlurFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::BoxBlur; }
    QString name() const override { return QStringLiteral("方框模糊"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    int ksize = 5;
};

class MedianBlurFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::MedianBlur; }
    QString name() const override { return QStringLiteral("中值模糊"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    int ksize = 5;
};

class BilateralBlurFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::BilateralBlur; }
    QString name() const override { return QStringLiteral("双边模糊"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    int    d          = 9;
    double sigmaColor = 75.0;
    double sigmaSpace = 75.0;
};

// ===== 3 锐化 =====
class SharpenFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Sharpen; }
    QString name() const override { return QStringLiteral("锐化"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class SharpenMoreFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::SharpenMore; }
    QString name() const override { return QStringLiteral("进一步锐化"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class UnsharpMaskFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::UnsharpMask; }
    QString name() const override { return QStringLiteral("反锐化蒙版"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    double amount = 1.0;   // 0.5..3.0
    double radius = 2.0;
    int    threshold = 0;
};

// ===== 3 风格化 =====
class EmbossFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Emboss; }
    QString name() const override { return QStringLiteral("浮雕"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class FindEdgesFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::FindEdges; }
    QString name() const override { return QStringLiteral("查找边缘"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class GlowingEdgesFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::GlowingEdges; }
    QString name() const override { return QStringLiteral("照亮边缘"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

// ===== 5 颜色 =====
class DesaturateFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Desaturate; }
    QString name() const override { return QStringLiteral("去色"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class InvertFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Invert; }
    QString name() const override { return QStringLiteral("反相"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class ThresholdFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Threshold; }
    QString name() const override { return QStringLiteral("阈值"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    double level = 128.0;   // 0..255
};

class PosterizeFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Posterize; }
    QString name() const override { return QStringLiteral("色调分离"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    int levels = 4;        // 2..255
};

class GradientMapFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::GradientMap; }
    QString name() const override { return QStringLiteral("渐变映射"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    // P0 阶段: 简化, 灰度 -> 彩色 (LUT)
    bool hasParam() const override { return true; }
    QString paramText() const override;
};

// ===== 5 其他 =====
class PhotoFilterFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::PhotoFilter; }
    QString name() const override { return QStringLiteral("照片滤镜"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    // 默认: 暖色 (R+25, G+10, B-25), density 50%
    int    cyanRed     = 25;
    int    magentaGreen = 10;
    int    yellowBlue   = -25;
    int    density      = 50;
};

class BlurMoreFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::BlurMore; }
    QString name() const override { return QStringLiteral("进一步模糊"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
};

class HighPassFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::HighPass; }
    QString name() const override { return QStringLiteral("高反差保留"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    double radius = 3.0;
};

class SolarizeFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::Solarize; }
    QString name() const override { return QStringLiteral("曝光过度"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    bool hasParam() const override { return true; }
    QString paramText() const override;
    double threshold = 128.0;   // 0..255
};

class FilterGalleryFilter : public FilterStrategy {
public:
    FilterKind kind() const override { return FilterKind::FilterGallery; }
    QString name() const override { return QStringLiteral("滤镜画廊"); }
    void apply(const cv::Mat& in, cv::Mat& out) override;
    // P0 阶段: 简化 placeholder, P1 接具体算法
    bool hasParam() const override { return false; }
    QString paramText() const override;
};

} // namespace filter
