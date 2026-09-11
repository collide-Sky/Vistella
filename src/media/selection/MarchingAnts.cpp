// SPDX-License-Identifier: MIT
//
// MarchingAnts implementation - P0-4.3 (2026-09-10)
//
#include "MarchingAnts.h"
#include "logger.h"

#include <QTimer>

namespace selection {

MarchingAnts::MarchingAnts(QObject* parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(200);   // 200ms per phase
    connect(m_timer, &QTimer::timeout, this, &MarchingAnts::onTick);
}

MarchingAnts::~MarchingAnts() = default;

void MarchingAnts::start()
{
    if (m_active) return;
    m_active = true;
    m_phase = 0;
    m_timer->start();
    LOG_DEBUG("[MarchingAnts] start");
    emit phaseChanged(m_phase);
}

void MarchingAnts::stop()
{
    if (!m_active) return;
    m_active = false;
    m_phase = 0;
    m_timer->stop();
    LOG_DEBUG("[MarchingAnts] stop");
    emit phaseChanged(m_phase);
}

void MarchingAnts::onTick()
{
    m_phase = (m_phase + 1) % 16;
    emit phaseChanged(m_phase);
}

} // namespace selection
