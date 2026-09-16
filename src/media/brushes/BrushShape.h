// SPDX-License-Identifier: MIT
//
// BrushShape - P1.1 (2026-09-15) Brush full implementation
//   PS-style brush tip shape selector + sampled bitmap support.
//   ShapeType enumerates the 9 built-in shapes (round/flat/fan/etc).
//   For Sampled/Textured shapes, m_sampledBitmap holds the grayscale stamp
//   that BrushEngine will use instead of generating one from settings.
//
//   Shape file format (disk persistence):
//   - Built-in shapes are stored as {type, settings} only
//   - Sampled shapes additionally store a PNG-compressed m_sampledBitmap
//   - JSON-serializable for preset file (.vbrush)

#pragma once

#include <QByteArray>
#include <QString>

#include <opencv2/core.hpp>

namespace brushes {

enum class ShapeType : int {
    Round         = 0,  // 圆硬边
    SoftRound     = 1,  // 圆柔边 (高 hardness 反向)
    Flat          = 2,  // 椭圆扁笔
    Fan           = 3,  // 扇形散开
    Airbrush      = 4,  // 极柔 (低 opacity 高 spacing)
    Chalk         = 5,  // 粉笔 (噪点 + 边缘破缺)
    Charcoal      = 6,  // 炭笔 (颗粒感)
    Textured      = 7,  // 自定义纹理 (m_sampledBitmap)
    Sampled       = 8,  // ABR 导入位图 (m_sampledBitmap)
};

inline QString shapeTypeToString(ShapeType t) {
    switch (t) {
        case ShapeType::Round:     return QStringLiteral("round");
        case ShapeType::SoftRound: return QStringLiteral("soft_round");
        case ShapeType::Flat:      return QStringLiteral("flat");
        case ShapeType::Fan:       return QStringLiteral("fan");
        case ShapeType::Airbrush:  return QStringLiteral("airbrush");
        case ShapeType::Chalk:     return QStringLiteral("chalk");
        case ShapeType::Charcoal:  return QStringLiteral("charcoal");
        case ShapeType::Textured:  return QStringLiteral("textured");
        case ShapeType::Sampled:   return QStringLiteral("sampled");
    }
    return QStringLiteral("round");
}

inline ShapeType shapeTypeFromString(const QString &s) {
    if (s == QStringLiteral("soft_round")) return ShapeType::SoftRound;
    if (s == QStringLiteral("flat"))       return ShapeType::Flat;
    if (s == QStringLiteral("fan"))        return ShapeType::Fan;
    if (s == QStringLiteral("airbrush"))   return ShapeType::Airbrush;
    if (s == QStringLiteral("chalk"))      return ShapeType::Chalk;
    if (s == QStringLiteral("charcoal"))   return ShapeType::Charcoal;
    if (s == QStringLiteral("textured"))   return ShapeType::Textured;
    if (s == QStringLiteral("sampled"))    return ShapeType::Sampled;
    return ShapeType::Round;
}

struct BrushShape {
    ShapeType    type            = ShapeType::Round;

    // Sampled bitmap (for Textured / Sampled / ABR imported brushes).
    //   Format: CV_8UC1 grayscale, alpha = intensity
    //   Size: at largest preset diameter (engine resizes to current size)
    cv::Mat      sampledBitmap;

    // PNG-compressed serialized form for persistence (lazy populated by serialize/deserialize).
    QByteArray   sampledBitmapPng;

    // Spacing density hint: 0..100. Engine uses max(shape spacing, settings spacing).
    int          spacingHint     = 25;

    static BrushShape fromJsonString(const QString &s);
    QString          toJsonString() const;
};

} // namespace brushes