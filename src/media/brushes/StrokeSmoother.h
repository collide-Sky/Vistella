// SPDX-License-Identifier: MIT
//
// StrokeSmoother - P1.1 (2026-09-15) Brush full implementation
//   PS-style stroke stabilization for brush tool. Two algorithms:
//     1) Simple running average (low-pass filter, 3-7 taps)
//     2) Stroke stabilizer: keep last N points, return centroid (PS smoothing slider)
//
//   PS smoothing slider 0..100% maps to:
//     0%   -> no smoothing (1 tap)
//     100% -> heavy smoothing (10 taps + centroid over 20 px)
//
//   Tool state calls addPoint(p) on every raw mouse move; paint uses smoothed().

#pragma once

#include <QPointF>
#include <deque>

namespace brushes {

class StrokeSmoother {
public:
    StrokeSmoother() = default;

    // PS smoothing slider value 0..100.
    void    setAmount(int percent) { m_amount = percent < 0 ? 0 : (percent > 100 ? 100 : percent); }
    int     amount() const { return m_amount; }

    // Stroke stabilizer radius (number of recent points to average)
    void    setRadius(int radius) { m_radius = radius < 1 ? 1 : (radius > 50 ? 50 : radius); }
    int     radius() const { return m_radius; }

    // Enable/disable smoothing altogether.
    void    setEnabled(bool on) { m_enabled = on; }
    bool    isEnabled() const { return m_enabled; }

    // Reset history (call on mouse press).
    void    reset() { m_history.clear(); }

    // Push a new raw point with optional pressure.
    void    addPoint(const QPointF &p, double pressure = 1.0);

    // Current smoothed point (centroid of last N points, where N depends on amount).
    QPointF smoothed() const;

    // Current smoothed pressure (average of recent pressures).
    double  smoothedPressure() const;

private:
    bool                       m_enabled = true;
    int                        m_amount = 50;
    int                        m_radius = 5;
    std::deque<std::pair<QPointF, double>> m_history;
};

} // namespace brushes