// SPDX-License-Identifier: MIT
//
// Throttle - F-O (2026-09-10)
//
// Generic trailing-edge debouncer. Multiple trigger() calls within the
// configured interval collapse to exactly one fired() emission, fired()
// intervalMs after the last trigger().
//
// Use case: prevent high-frequency events (slider, mouse move, progress
// updates) from triggering repeated recompute / redraw / save work.
//
// Usage:
//   Throttle t(200, this);
//   connect(slider, &QSlider::valueChanged, &t, &Throttle::trigger);
//   connect(&t, &Throttle::fired, this, &MyWidget::onThrottled);
//
#pragma once

#include <QObject>
#include <QTimer>

namespace core {

class Throttle : public QObject
{
    Q_OBJECT
public:
    explicit Throttle(int intervalMs = 200, QObject* parent = nullptr);
    ~Throttle() override;

    int  interval() const { return m_intervalMs; }
    void setInterval(int ms);

public slots:
    // Trigger: collapse N calls within `intervalMs` to exactly one trailing fired().
    void trigger();

    // Cancel any pending fired() emission. After cancel(), the next trigger()
    // starts a fresh window.
    void cancel();

signals:
    // Emitted once, `intervalMs` after the last trigger() (trailing edge).
    void fired();

private:
    int     m_intervalMs = 200;
    QTimer  m_timer;
};

} // namespace core
