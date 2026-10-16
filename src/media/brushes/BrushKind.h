// SPDX-License-Identifier: MIT
//
// BrushKind - P7.9.2 (2026-10-16) 13 brush dynamic types
//   PS-style brush taxonomy: 8 Basic + 3 Wet + 2 Special = 13 total kinds.
//   Each preset carries a `kind` so the dock can show the current family
//   (cmbKind readonly label) and the GPU backend can pick a default based
//   on per-kind cost (P8.2: kind.stampCost + sharedState hint).
//
//   - Basic 8: defer to BrushSharedState::needsGpu() (the panel decides).
//   - Wet 3:   always true (wet edges + scatter are heavy; GPU wins).
//   - Special 2: always false (Smudge / Eraser are simple; CPU is fine).
//
//   P7.9.2 commit (2026-10-16).

#pragma once

#include <QList>
#include <QString>

namespace tools { struct BrushSharedState; }

namespace brushes {

// PS-style 13-kinds taxonomy. Order is stable; do not reorder
// (BrushKind::allKinds() iterates in this order for the dock picker).
enum class BrushKind : int {
    // ---- Basic 8 ----
    HardRound       = 0,  // 圆硬边 (default)
    SoftRound       = 1,  // 圆柔边
    Pencil          = 2,  // 铅笔 (small + high hardness + tight spacing)
    Marker          = 3,  // 马克笔 (flat + 50% opacity + large spacing)
    Airbrush        = 4,  // 喷枪 (pressure -> flow, low hardness)
    Chalk           = 5,  // 粉笔 (gradient stamp, edge broken)
    Charcoal        = 6,  // 炭笔 (颗粒感, 高 angle jitter)
    Calligraphic    = 7,  // 书法笔 (angle/roundness-driven)
    // ---- Wet 3 ----
    Watercolor      = 8,  // 水彩 (low hardness + wet edge)
    Oil             = 9,  // 油画 (heavy deposit, no flow jitter)
    Acrylic         = 10, // 丙烯 (medium wet, soft round + high flow)
    // ---- Special 2 ----
    Smudge          = 11, // 涂抹 (drags pixels, no new color)
    Eraser          = 12, // 橡皮 (destination erase)
};

// High-level group. Drives the kind picker UI grouping.
enum class BrushCategory : int {
    Basic   = 0,  // 8 kinds
    Wet     = 1,  // 3 kinds
    Special = 2,  // 2 kinds
};

inline BrushCategory categoryOf(BrushKind k) {
    const int idx = static_cast<int>(k);
    if (idx <= 7) return BrushCategory::Basic;
    if (idx <= 10) return BrushCategory::Wet;
    return BrushCategory::Special;
}

inline const char* categoryName(BrushCategory c) {
    switch (c) {
        case BrushCategory::Basic:   return "Basic";
        case BrushCategory::Wet:     return "Wet";
        case BrushCategory::Special: return "Special";
    }
    return "Basic";
}

// PS display name (e.g. "Hard Round"). Used in cmbKind label and picker.
inline QString kindName(BrushKind k) {
    switch (k) {
        case BrushKind::HardRound:    return QStringLiteral("Hard Round");
        case BrushKind::SoftRound:    return QStringLiteral("Soft Round");
        case BrushKind::Pencil:       return QStringLiteral("Pencil");
        case BrushKind::Marker:       return QStringLiteral("Marker");
        case BrushKind::Airbrush:     return QStringLiteral("Airbrush");
        case BrushKind::Chalk:        return QStringLiteral("Chalk");
        case BrushKind::Charcoal:     return QStringLiteral("Charcoal");
        case BrushKind::Calligraphic: return QStringLiteral("Calligraphic");
        case BrushKind::Watercolor:   return QStringLiteral("Watercolor");
        case BrushKind::Oil:          return QStringLiteral("Oil");
        case BrushKind::Acrylic:      return QStringLiteral("Acrylic");
        case BrushKind::Smudge:       return QStringLiteral("Smudge");
        case BrushKind::Eraser:       return QStringLiteral("Eraser");
    }
    return QStringLiteral("Hard Round");
}

// Stable order for the dock picker (Basic 8, Wet 3, Special 2).
inline QList<BrushKind> allKinds() {
    return QList<BrushKind>{
        BrushKind::HardRound,    BrushKind::SoftRound,
        BrushKind::Pencil,       BrushKind::Marker,
        BrushKind::Airbrush,     BrushKind::Chalk,
        BrushKind::Charcoal,     BrushKind::Calligraphic,
        BrushKind::Watercolor,   BrushKind::Oil,        BrushKind::Acrylic,
        BrushKind::Smudge,       BrushKind::Eraser,
    };
}

// Per-kind needsGpu() override. P8.2 GPU backend uses this to decide
// whether to dispatch to QtGpuBackend or fall back to CpuBackend.
//
//   Basic 8:   defer to sharedState (Basic brushes are cheap unless the
//              user cranks smoothing/scatter/blendMode).
//   Wet 3:     always true. Wet edges + scatter are 4-8x cheaper on GPU
//              and the visual quality difference is large at large sizes.
//   Special 2: always false. Smudge/Eraser are point-wise ops; GPU adds
//              upload/download latency for no win.
//
// P7.9.2: read-only hint. P8.2: actual backend selection.
// Declared here, defined in BrushKind.cpp (needs full BrushSharedState
// definition from tools/ToolContext.h; .h only forward-decls it).
bool kindNeedsGpu(BrushKind k, const tools::BrushSharedState& ss);

// String conversion for JSON. PS-aligned short identifiers.
inline QString kindToString(BrushKind k) {
    switch (k) {
        case BrushKind::HardRound:    return QStringLiteral("hard_round");
        case BrushKind::SoftRound:    return QStringLiteral("soft_round");
        case BrushKind::Pencil:       return QStringLiteral("pencil");
        case BrushKind::Marker:       return QStringLiteral("marker");
        case BrushKind::Airbrush:     return QStringLiteral("airbrush");
        case BrushKind::Chalk:        return QStringLiteral("chalk");
        case BrushKind::Charcoal:     return QStringLiteral("charcoal");
        case BrushKind::Calligraphic: return QStringLiteral("calligraphic");
        case BrushKind::Watercolor:   return QStringLiteral("watercolor");
        case BrushKind::Oil:          return QStringLiteral("oil");
        case BrushKind::Acrylic:      return QStringLiteral("acrylic");
        case BrushKind::Smudge:       return QStringLiteral("smudge");
        case BrushKind::Eraser:       return QStringLiteral("eraser");
    }
    return QStringLiteral("hard_round");
}

inline BrushKind kindFromString(const QString& s) {
    if (s == QStringLiteral("soft_round"))    return BrushKind::SoftRound;
    if (s == QStringLiteral("pencil"))       return BrushKind::Pencil;
    if (s == QStringLiteral("marker"))       return BrushKind::Marker;
    if (s == QStringLiteral("airbrush"))     return BrushKind::Airbrush;
    if (s == QStringLiteral("chalk"))        return BrushKind::Chalk;
    if (s == QStringLiteral("charcoal"))     return BrushKind::Charcoal;
    if (s == QStringLiteral("calligraphic")) return BrushKind::Calligraphic;
    if (s == QStringLiteral("watercolor"))   return BrushKind::Watercolor;
    if (s == QStringLiteral("oil"))          return BrushKind::Oil;
    if (s == QStringLiteral("acrylic"))      return BrushKind::Acrylic;
    if (s == QStringLiteral("smudge"))       return BrushKind::Smudge;
    if (s == QStringLiteral("eraser"))       return BrushKind::Eraser;
    return BrushKind::HardRound;   // default fallback
}

} // namespace brushes
