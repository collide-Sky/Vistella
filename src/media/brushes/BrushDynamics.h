// SPDX-License-Identifier: MIT
//
// BrushDynamics - P1.1 (2026-09-15) Brush full implementation
//   PS-style brush dynamics (PS Brush panel "Shape Dynamics / Scattering /
//   Texture / Dual Brush / Transfer / Smoothing").
//   Each dynamics field has a value + jitter range + control source.
//   Control sources follow PS: Off / Fade / Pen Pressure / Pen Tilt / Rotation / Stylus Wheel.
//
//   Architecture notes:
//   - All dynamics are pure data (no QObject), can be serialized to JSON
//   - BrushEngine consumes a finalized stamp parameters struct after jitter + pressure
//     resolution, so the engine doesn't need to know about control sources
//   - ToolState (eventFilter) reads stylus pressure from QTabletEvent, feeds it to dynamics

#pragma once

#include <QString>

namespace brushes {

// Jitter control source (PS brush panel dropdown).
enum class JitterControl : int {
    Off          = 0,  // 固定 jitter (no randomness)
    Fade         = 1,  // 随累计步数淡入淡出
    PenPressure  = 2,  // 跟压感
    PenTilt      = 3,  // 跟笔倾斜
    Rotation     = 4,  // 跟笔旋转
    StylusWheel  = 5,  // 跟笔轮
};

inline QString jitterControlToString(JitterControl c) {
    switch (c) {
        case JitterControl::Off:         return QStringLiteral("off");
        case JitterControl::Fade:        return QStringLiteral("fade");
        case JitterControl::PenPressure: return QStringLiteral("pressure");
        case JitterControl::PenTilt:     return QStringLiteral("tilt");
        case JitterControl::Rotation:    return QStringLiteral("rotation");
        case JitterControl::StylusWheel: return QStringLiteral("wheel");
    }
    return QStringLiteral("off");
}

inline JitterControl jitterControlFromString(const QString &s) {
    if (s == QStringLiteral("fade"))        return JitterControl::Fade;
    if (s == QStringLiteral("pressure"))    return JitterControl::PenPressure;
    if (s == QStringLiteral("tilt"))        return JitterControl::PenTilt;
    if (s == QStringLiteral("rotation"))    return JitterControl::Rotation;
    if (s == QStringLiteral("wheel"))       return JitterControl::StylusWheel;
    return JitterControl::Off;
}

// Shape dynamics sub (PS: Shape Dynamics section).
struct ShapeDynamics {
    int           sizeJitterMin   = 0;    // 0..100 (% of size)
    int           sizeJitterMax   = 0;    // 0..100
    JitterControl sizeControl     = JitterControl::Off;
    int           angleJitter     = 0;    // 0..359
    JitterControl angleControl    = JitterControl::Off;
    int           roundnessJitter = 0;    // 0..100
    JitterControl roundnessControl= JitterControl::Off;
    bool          flipX           = false;
    bool          flipY           = false;
};

// Scattering sub (PS: Scattering section).
struct ScatteringDynamics {
    int           scatterX        = 0;    // 0..1000 (% of diameter)
    int           scatterY        = 0;    // 0..1000
    JitterControl scatterControl  = JitterControl::Off;
    int           count           = 1;    // 1..16 stamps per stroke point
    int           countJitter     = 0;    // 0..100
    bool          bothAxes        = true;
};

// Texture sub (PS: Texture section).
struct TextureDynamics {
    QString       texturePath;             // pattern file path (PNG/QPT)
    int           scale         = 100;    // 25..1000 %
    JitterControl mode          = JitterControl::Off;
    int           depthMin      = 0;      // 0..1000 (% of texture influence)
    int           depthMax      = 100;
    JitterControl depthControl  = JitterControl::Off;
    bool          invert        = false;
    int           brightness    = 0;      // -100..100
    int           contrast      = 0;      // -100..100
    bool          protectTexture= false;   // 保护 alpha 不被 texture 影响
};

// Dual brush sub (PS: Dual Brush section).
struct DualBrushDynamics {
    bool          enabled        = false;
    QString       secondaryPreset;          // 引用 preset 名 (空则用 primary 内置)
    int           size           = 100;   // 25..400 %
    int           spacing        = 100;   // 1..1000 %
    int           scatter        = 0;     // 0..1000 %
    int           count          = 1;     // 1..16
};

// Transfer sub (PS: Transfer section).
struct TransferDynamics {
    int           opacityJitterMin = 0;    // 0..100
    int           opacityJitterMax = 0;    // 0..100
    JitterControl opacityControl  = JitterControl::Off;
    int           flowJitterMin    = 0;
    int           flowJitterMax    = 0;
    JitterControl flowControl      = JitterControl::Off;
    bool          airbrush         = false; // false = Build-up (PS default), true = Airbrush mode
    bool          perClick         = true;  // false = Per Stroke (Paint stays at flow while held)
};

// Smoothing sub (PS: Smoothing section).
struct SmoothingDynamics {
    bool          enabled      = true;
    int           amount       = 50;      // 0..100 (% smoothing strength)
    bool          strokeStabilizer = true; // 笔触稳定 (use centroid  of recent points)
    int           radius       = 5;       // 1..20 points
};

struct BrushDynamics {
    ShapeDynamics        shape;
    ScatteringDynamics   scattering;
    TextureDynamics      texture;
    DualBrushDynamics    dual;
    TransferDynamics     transfer;
    SmoothingDynamics    smoothing;

    static BrushDynamics fromJsonString(const QString &s);
    QString              toJsonString() const;
};

} // namespace brushes