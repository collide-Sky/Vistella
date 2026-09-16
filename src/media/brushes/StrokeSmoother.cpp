// SPDX-License-Identifier: MIT
//
// StrokeSmoother impl - P1.1 (2026-09-15)

#include "StrokeSmoother.h"

#include <algorithm>

namespace brushes {

void StrokeSmoother::addPoint(const QPointF &p, double pressure)
{
    if (!m_enabled) {
        m_history.clear();
        m_history.push_back({p, pressure});
        return;
    }
    m_history.push_back({p, pressure});
    // Cap history to max possible radius (50) + extra for safety
    while ((int)m_history.size() > 50) m_history.pop_front();
}

QPointF StrokeSmoother::smoothed() const
{
    if (m_history.empty()) return QPointF();
    if (!m_enabled || m_amount <= 0 || m_history.size() == 1) return m_history.back().first;

    // Map amount 0..100 -> radius 1..radius() (PS smoothing slider behavior)
    const int effective_radius = std::max(1, (m_amount * m_radius + 99) / 100);
    const int n = std::min<int>(effective_radius, (int)m_history.size());

    double sx = 0, sy = 0;
    int i = 0;
    for (auto it = m_history.rbegin(); it != m_history.rend() && i < n; ++it, ++i) {
        sx += it->first.x();
        sy += it->first.y();
    }
    return QPointF(sx / n, sy / n);
}

double StrokeSmoother::smoothedPressure() const
{
    if (m_history.empty()) return 1.0;
    if (!m_enabled) return m_history.back().second;
    const int n = std::min<int>(m_history.size(), 16);
    double s = 0;
    int i = 0;
    for (auto it = m_history.rbegin(); it != m_history.rend() && i < n; ++it, ++i) {
        s += it->second;
    }
    return s / n;
}

} // namespace brushes