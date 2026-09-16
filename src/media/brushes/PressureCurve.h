// SPDX-License-Identifier: MIT
//
// PressureCurve - P1.1 (2026-09-15) Brush full implementation
//   PS-style pressure response curve (minimum, maximum, curve shape).
//   Maps raw stylus pressure (0..1) -> remapped pressure (0..1).
//
//   PS UI: 2 horizontal sliders (min/max) + a curve graph (clickable to add
//   control points). For simplicity we implement:
//     - min/max clamping (PS sliders)
//     - single midpoint (PS uses cubic spline; we use simple (1-cos(t*pi))/2
//       for visual approximation - good enough for first pass)
//
//   Future: full cubic spline evaluation with up to 16 control points.

#pragma once

#include <QPointF>
#include <QVector>

namespace brushes {

class PressureCurve {
public:
    PressureCurve() = default;

    // PS: Minimum 0..100 (clamps low end of input range)
    void    setMinimum(int percent) { m_minimum = clamp(percent, 0, 100); }
    int     minimum() const { return m_minimum; }

    // PS: Maximum 0..100 (clamps high end of input range)
    void    setMaximum(int percent) { m_maximum = clamp(percent, 0, 100); }
    int     maximum() const { return m_maximum; }

    // PS: Curve control points (default empty = linear)
    //   Each point (x, y) in 0..1 domain, x sorted ascending.
    //   For first pass: support 1 mid-point (0.5, y) which acts as bend.
    void    setControlPoints(const QVector<QPointF> &pts) { m_pts = pts; }
    QVector<QPointF> controlPoints() const { return m_pts; }

    // Evaluate: input 0..1 (raw pressure) -> output 0..1 (remapped).
    double  evaluate(double rawPressure) const;

    // True if curve is identity (min=0, max=100, no control points).
    bool    isLinear() const { return m_minimum == 0 && m_maximum == 100 && m_pts.isEmpty(); }

    static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

private:
    int                  m_minimum = 0;
    int                  m_maximum = 100;
    QVector<QPointF>     m_pts;
};

} // namespace brushes