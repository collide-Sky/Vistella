// SPDX-License-Identifier: MIT
//
// PressureCurve impl - P1.1 (2026-09-15)

#include "PressureCurve.h"

#include <algorithm>

namespace brushes {

double PressureCurve::evaluate(double rawPressure) const
{
    if (rawPressure <= 0.0) return 0.0;
    if (rawPressure >= 1.0) return 1.0;

    // 1) Clamp by min/max
    const double lo = m_minimum / 100.0;
    const double hi = m_maximum / 100.0;
    if (hi <= lo) {
        // degenerate: collapse to midpoint
        return lo;
    }
    double t = (rawPressure - lo) / (hi - lo);
    t = std::clamp(t, 0.0, 1.0);

    // 2) Apply control point bend (simplified: midpoint bend)
    //   If we have control points, treat the middle one as a bend factor:
    //   y = (x + (midY - 0.5) * sin(pi*x))  -- approximation
    if (!m_pts.isEmpty()) {
        const QPointF &p = m_pts.first();
        const double x = p.x();
        const double y = p.y();
        if (x > 0.05 && x < 0.95 && y > 0.05 && y < 0.95) {
            // Smoothstep approximation that passes through (x, y)
            // For first pass: linear interpolation between control points is fine
            // if we have 1 point, just shift the curve
            const double shift = y - x;   // 0 = identity, +0.3 = curve up
            // Apply S-curve: output = t + shift * 4 * t * (1 - t)
            const double s = t + shift * 4.0 * t * (1.0 - t);
            return std::clamp(s, 0.0, 1.0);
        }
    }
    return t;
}

} // namespace brushes