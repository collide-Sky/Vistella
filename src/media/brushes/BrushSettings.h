// SPDX-License-Identifier: MIT
//
// BrushSettings - P1.1 (2026-09-15) Brush full implementation
//   PS-style brush tip parameters (size, hardness, opacity, flow, spacing, angle, roundness)
//   Inspired by Photoshop's Brush panel "Shape Dynamics" basic settings.
//   Designed to be 100% data (no Qt event loop dependency) so it can be used
//   from BrushEngine (CPU), ToolState, QSettings serialization, and tests.

#pragma once

#include <QString>

namespace brushes {

struct BrushSettings {
    // Diameter in pixels (1..2000). Always odd-friendly; engine clamps.
    int     size       = 32;

    // Hardness 0..100. 100 = sharp edge, 0 = full Gaussian falloff.
    int     hardness   = 80;

    // Opacity 0..100. Per-stamp alpha multiplier.
    int     opacity    = 100;

    // Flow 0..100. Accumulation rate (PS uses both: per-stamp intensity multiplied by flow).
    int     flow       = 100;

    // Spacing 1..1000 (%). Distance between stamps as % of brush diameter.
    int     spacing    = 25;

    // Angle 0..359 (degrees). Rotates the elliptical shape.
    int     angle      = 0;

    // Roundness 0..100. 100 = perfectly round, 0 = flat line.
    int     roundness  = 100;

    // Helper for JSON / QSettings serialization.
    static BrushSettings fromJsonString(const QString &s);
    QString toJsonString() const;
};

} // namespace brushes