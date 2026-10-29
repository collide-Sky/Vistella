#ifndef LAYER_H
#define LAYER_H

// =============================================================
// Layer — 完整 Layer
//
// 设计动机:
//    imageWindow.m_current 是单一 cv::Mat, 改一处覆盖全部
//    完整 PS 图层系统, 5 种 LayerKind + 15 种 BlendMode + 链接/编组/锁定
//
// 5 种 LayerKind (Phase 1 全部声明, Phase 3 全部实现):
//   - Bitmap       (位图, cv::Mat, Phase 1 实现)
//   - Vector       (矢量, QPainterPath list, Phase 3)
//   - Text         (文字, QString, Phase 3)
//   - SmartObject  (智能对象, sourceFilePath 引用, Phase 3+5)
//   - Adjustment   (调整层, curves/levels/..., Phase 3)
//
// 15 种 BlendMode (Phase 1 只用 Normal, Phase 2 全实现):
//   Normal / Multiply / Screen / Overlay / SoftLight / HardLight
//   ColorDodge / ColorBurn / Darken / Lighten / Difference / Exclusion
//   Hue / Saturation / Color / Luminosity
//
// 设计原则:
//   0. Pixel contract: the pixels of a layer in the stack are always
//      CV_8UC4 BGRA with straight alpha (see PixelFormat.h);
//      Layer::mask is the single mask model, and the render only
//      honours it.
//   1. Layer 用 shared_ptr + 值语义 cv::Mat (不可变, 修改 = 创建新 Layer 进撤销栈)
//   2. LayerKind 决定哪个 payload 字段有效 (Bitmap 用 image, Text 用 text, ...)
//   3. 不持有 QObject parent (避免 lifecycle 复杂度)
//   4. isValid() 校验当前 kind 必须的 payload
// =============================================================

#include <opencv2/core.hpp>

#include <QString>
#include <QVector>
#include <QColor>
#include <QPainterPath>
#include <QTransform>

#include "LayerMask.h"

#include <memory>

namespace layers {

// 图层稳定身份 (LYR-06 / LYR-12 / LYR-16).
//   所有外部引用 —— 撤销命令 / 编组 children 表 / 智能滤镜链 / 面板 item ——
//   一律用这个 id:
//     int 下标     : 增删 / 移动图层就漂移, 撤销时按旧下标会删错层
//     LayerPtr 地址: 值拷贝一次就变, 编组 / 复制之后原地址查不到
//     cv::Mat::data: 空 image 的层 (Text / Group / Adjustment) 全是 nullptr
//   id 由 LayerStack 在图层首次入栈时分配, 值拷贝时随 Layer 一起带走,
//   所以"删掉再撤销"能拿回同一个身份.
using LayerId = quint64;

struct Layer {
    // ---- 稳定身份 ----
    //   0 = 还没进过栈, 由 LayerStack 入栈时分配.
    LayerId stableId = 0;

    // ---- 类型 (Phase 1: Bitmap 完整, 其他 enum 声明) ----
    enum LayerKind {
        Bitmap,        // cv::Mat
        Vector,        // vectorPaths
        Text,          // text + fontSize + color
        SmartObject,   // sourceFilePath
        Adjustment,    // adjustmentType + adjustmentLut
        SmartFilter,   // SmartObject sub-filter (filterType + filterStrength)
        Group          // Container layer; children live in
                      // LayerStack::m_groups keyed by Group layer stable id.
    };
    LayerKind kind = Bitmap;

    // ---- 混合模式 (Phase 1 只用 Normal, Phase 2 实现其他) ----
    enum BlendMode {
        Normal,
        Multiply, Screen, Overlay, SoftLight, HardLight,
        ColorDodge, ColorBurn, Darken, Lighten, Difference, Exclusion,
        Hue, Saturation, Color, Luminosity,
    };
    BlendMode blend = Normal;

    // ---- 元数据 (所有类型共用) ----
    QString name;                // 显示名
    bool    visible = true;       // 是否参与渲染
    bool    locked  = false;      // 锁定后不能编辑 (rename / opacity / move)
    bool    isLinked = false;      // 链接 (跟其他 linked layer 一起移动/缩放/旋转)
    float   opacity = 1.0f;       // 0.0 - 1.0
    int     zOrder  = 0;          // 越大越上面 (跟 Photoshop 一致)

    // ---- Bitmap payload (kind == Bitmap) ----
    //   Pixel contract (LYR-01 / LYR-35 / CP-07):
    //     The pixels of a layer in the stack are always CV_8UC4 BGRA with
    //     STRAIGHT alpha (not premultiplied).
    //     LayerStack normalises them on insert (PixelFormat::toBgra), so a
    //     3-channel layer created outside becomes 4-channel once it is in
    //     the stack.
    //     See PixelFormat.h for the compositing maths.
    cv::Mat image;

    // ---- Vector payload (kind == Vector, Phase 3) ----
    QVector<QPainterPath> vectorPaths;
    QVector<QColor>        vectorFillColors;
    QVector<qreal>         vectorStrokeWidths;
    // vector stroke colors (PS 同款, 每 shape 独立 stroke color)
    QVector<QColor>        vectorStrokeColors;

    // ---- Text payload (kind == Text, Phase 3) ----
    QString text;
    int     fontSize = 24;
    QColor  textColor = Qt::white;
    QString fontFamily = QStringLiteral("Arial");

    // ---- SmartObject payload (kind == SmartObject, Phase 3+5) ----
    QString sourceFilePath;       // 源文件绝对路径 (嵌入时为空)
    bool    sourceEmbedded = false; // true = 嵌入 (复制源文件到 cache), false = 链接

    // Non-destructive transform (SmartObject only)
    //   Identity = no transform applied. rasterize() applies to source pixels
    //   when loading. Persisted via LayerCommand::SetSmartObjectTransform.
    QTransform transform;
    bool    hasTransform = false;

    // ---- Adjustment payload (kind == Adjustment, Phase 3) ----
    QString adjustmentType;       // "curves" / "levels" / "hueSat" / "colorBalance" / ...
    cv::Mat adjustmentLut;        // 256x1 CV_8U (curves / levels LUT)

    // ---- SmartFilter payload (kind == SmartFilter, P1.4.4 2026-09-17) ----
    //   Sub-filter of a SmartObject layer. References parent via
    //   Layer::parentSmartId (LayerStack::smartFilterIdsFor). Owns the
    //   filtered image (cv::Mat)
    //   so the renderer can stack outputs without re-running the filter.
    QString filterType;           // "GaussianBlur" / "Sharpen" / "Brightness" / ...
    double  filterStrength = 1.0; // 0..2 intensity multiplier
    //   父 SmartObject 的 stableId (0 = 孤儿). 用 id 而不是下标:
    //   下标会随增删 / 移动漂移, 智能滤镜会因此挂到别的图层上 (LYR-16).
    LayerId parentSmartId = 0;
    int     filterSlotIndex  = -1; // 0-based position in parent's smartFilters chain

    // ---- Mask (the single model) ----
    //   Layer::mask is the layer's ONLY mask truth (LYR-15 / CP-18).
    //   There used to be a pair, cv::Mat layerMask + bool maskEnabled,
    //   which the render consumed, while the UI / brush wrote this struct
    //   -> two states written independently, and "add mask / invert /
    //   feather / density" had no visual effect at all.
    //   Now: the render only honours mask, and the coverage is folded in
    //   by LayerMaskRenderer::buildAlpha().
    LayerMask mask;

    // ---- 构造 ----
    Layer() = default;
    Layer(const QString &name_, const cv::Mat &image_)
        : kind(Bitmap), name(name_), image(image_.clone()) {}
    Layer(const QString &name_, const QString &text_, int fontSize_ = 24)
        : kind(Text), name(name_), text(text_), fontSize(fontSize_) {}

    // 拷贝 / 移动 (默认 cv::Mat 用 clone 走值语义, QString 走值语义)
    Layer(const Layer &) = default;
    Layer &operator=(const Layer &) = default;
    Layer(Layer &&) noexcept = default;
    Layer &operator=(Layer &&) noexcept = default;

    // ---- 验证 ----
    // Bitmap: image 必须非空, 8U, 3 通道
    // Text: text 非空
    // SmartObject: sourceFilePath 非空 (embedded 时 cache 文件存在)
    // Vector/Adjustment: 暂宽松
    bool isValid() const {
        switch (kind) {
        case Bitmap:
            // Both 3 channels (not yet normalised into the stack) and
// 4 channels (the in-stack contract) are valid
            return !image.empty() && image.depth() == CV_8U
                && (image.channels() == 3 || image.channels() == 4);
        case Text:
            return !text.isEmpty();
        case SmartObject:
            return !sourceFilePath.isEmpty();
        case SmartFilter:
            return !image.empty() && image.depth() == CV_8U
                && (image.channels() == 3 || image.channels() == 4)
                && !filterType.isEmpty() && parentSmartId != 0;
        case Group:
            return true;  // Empty group is valid; non-empty validated by LayerStack::m_groups entry.
        case Vector:
        case Adjustment:
        default:
            return true;
        }
    }

    // ---- 图像尺寸 (Bitmap 用, 其他 kind 走 base image) ----
    int width() const { return image.cols; }
    int height() const { return image.rows; }
};

using LayerPtr = std::shared_ptr<Layer>;

} // namespace layers

#endif // LAYER_H
