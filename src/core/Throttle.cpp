// SPDX-License-Identifier: MIT
//
// Throttle implementation - F-O (2026-09-10)
//
#include "Throttle.h"
#include "logger.h"

namespace core {

Throttle::Throttle(int intervalMs, QObject* parent)
    : QObject(parent), m_intervalMs(intervalMs)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &Throttle::fired);
}

Throttle::~Throttle() = default;

void Throttle::setInterval(int ms)
{
    m_intervalMs = ms;
    m_timer.setInterval(m_intervalMs);
}

void Throttle::trigger()
{
    // Trailing-edge debounce: restart the single-shot timer. fired() fires
    // exactly once, `intervalMs` after the LAST trigger() call.
    m_timer.start();
    LOG_DEBUG("[Throttle] trigger (interval={}ms)", m_intervalMs);
}

void Throttle::cancel()
{
    // Drop a pending fired() (e.g. when caller wants to clear state immediately
    // and not be re-applied by a stale debounce window).
    m_timer.stop();
}

} // namespace core
