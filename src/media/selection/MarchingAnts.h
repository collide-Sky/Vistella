// SPDX-License-Identifier: MIT
//
// MarchingAnts - P0-4.3 (2026-09-10)
//
// 200ms tick timer that drives the marching ants animation offset.
//   phase 0..15 (16 phases over 200ms*16 = 3.2s loop)
//   visual: 8px black/white dashed border, shifts 0.5px per phase
//   imagewindow.ImageCanvas::drawForeground uses phase() as pen dash offset.
//
// QObject parent ownership; starts on first selection set, stops on clear.
#pragma once

#include <QObject>

class QTimer;

namespace selection {

class MarchingAnts : public QObject
{
    Q_OBJECT
public:
    explicit MarchingAnts(QObject* parent = nullptr);
    ~MarchingAnts() override;

    void start();
    void stop();
    bool isActive() const { return m_active; }
    int  phase() const { return m_phase; }

signals:
    void phaseChanged(int phase);

private:
    void onTick();

    QTimer* m_timer  = nullptr;
    int     m_phase  = 0;
    bool    m_active = false;
};

} // namespace selection
