#ifndef LAYER_H
#define LAYER_H

// =============================================================
// Layer — 阶段 1 W4.3 Phase 1 完整 Layer (2026-09-04)
//
// 设计动机:
//   阶段 0 imageWindow.m_current 是单一 cv::Mat, 改一处覆盖全部
//   阶段 1 W4.3 完整 PS 图层系统, 5 种 LayerKind + 15 种 BlendMode + 链接/编组/锁定
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

struct Layer {
    // ---- 类型 (Phase 1: Bitmap 完整, 其他 enum 声明) ----
    enum LayerKind {
        Bitmap,        // cv::Mat
        Vector,        // vectorPaths
        Text,          // text + fontSize + color
        SmartObject,   // sourceFilePath
        Adjustment,    // adjustmentType + adjustmentLut
        SmartFilter,   // P1.4.4 (2026-09-17): SmartObject sub-filter (filterType + filterStrength)
        Group          // P1.5.2 (2026-09-21): Container layer; children live in
                      // LayerStack::m_groups keyed by Group layer index.
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
    cv::Mat image;                // 8UC3 BGR, 跟 base image 同尺寸

    // ---- Vector payload (kind == Vector, Phase 3) ----
    QVector<QPainterPath> vectorPaths;
    QVector<QColor>        vectorFillColors;
    QVector<qreal>         vectorStrokeWidths;
    // P0-9.1 (2026-09-15): vector stroke colors (PS 同款, 每 shape 独立 stroke color)
    QVector<QColor>        vectorStrokeColors;

    // ---- Text payload (kind == Text, Phase 3) ----
    QString text;
    int     fontSize = 24;
    QColor  textColor = Qt::white;
    QString fontFamily = QStringLiteral("Arial");

    // ---- SmartObject payload (kind == SmartObject, Phase 3+5) ----
    QString sourceFilePath;       // 源文件绝对路径 (嵌入时为空)
    bool    sourceEmbedded = false; // true = 嵌入 (复制源文件到 cache), false = 链接

    // P1.4.1 (2026-09-17): Non-destructive transform (SmartObject only).
    //   Identity = no transform applied. rasterize() applies to source pixels
    //   when loading. Persisted via LayerCommand::SetSmartObjectTransform.
    QTransform transform;
    bool    hasTransform = false;

    // ---- Adjustment payload (kind == Adjustment, Phase 3) ----
    QString adjustmentType;       // "curves" / "levels" / "hueSat" / "colorBalance" / ...
    cv::Mat adjustmentLut;        // 256x1 CV_8U (curves / levels LUT)

    // ---- SmartFilter payload (kind == SmartFilter, P1.4.4 2026-09-17) ----
    //   Sub-filter of a SmartObject layer. References parent via
    //   LayerStack::smartFiltersFor(smartIdx). Owns the filtered image (cv::Mat)
    //   so the renderer can stack outputs without re-running the filter.
    QString filterType;           // "GaussianBlur" / "Sharpen" / "Brightness" / ...
    double  filterStrength = 1.0; // 0..2 intensity multiplier
    int     parentSmartIndex = -1; // index of parent SmartObject in stack (-1 = orphan)
    int     filterSlotIndex  = -1; // 0-based position in parent's smartFilters chain

    // ---- 蒙版 (Phase 4 实现) ----
    cv::Mat layerMask;            // 灰度图 8U, 0=透明 255=不透明
    bool    maskEnabled = false;

    // P1.3.2 (2026-09-16): extended mask state (pixel/vector/density/feather/invert)
    LayerMask mask;               // new structured mask (additive to layerMask)

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
            return !image.empty() && image.depth() == CV_8U && image.channels() == 3;
        case Text:
            return !text.isEmpty();
        case SmartObject:
            return !sourceFilePath.isEmpty();
        case SmartFilter:
            return !image.empty() && image.depth() == CV_8U
                && !filterType.isEmpty() && parentSmartIndex >= 0;
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
