// SPDX-License-Identifier: MIT
//
// ViewTools implementation (the hand / the zoom / the rotate view).
//
// See ViewTools.h for the why.
#include "ViewTools.h"

#include "../imagewindow.h"
#include "../imagewindow/ImageCanvas.h"
#include "logger.h"

#include <QMouseEvent>
#include <QScrollBar>

#include <cmath>

namespace tools {

// ---------------------------------------------------------------------------
//  The hand
// ---------------------------------------------------------------------------

void HandTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_panning = false;
    m_panDelta = QPointF();
    LOG_DEBUG("[HandTool] onEnter host={}", host ? "yes" : "null");
}

void HandTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_panning = false;
    m_host = nullptr;
}

void HandTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    m_panning = true;
    m_pressPos = scenePos;
    m_panDelta = QPointF();
}

void HandTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_panning || !host) return;

    auto* canvas = host->imageCanvas();
    if (!canvas) return;

    // The pan is done on the scroll bars, in VIEW coordinates. The
    // scene position changes as the view scrolls, so the delta has to
    // be computed in scene space and then converted.
    const QPointF delta = scenePos - m_pressPos;
    m_panDelta = delta;

    // The scroll bars move the view; the amount depends on the zoom
    // (one scene pixel is `zoom` view pixels).
    const double zoom = canvas->zoom();
    auto* hBar = canvas->horizontalScrollBar();
    auto* vBar = canvas->verticalScrollBar();
    if (hBar) {
        hBar->setValue(hBar->value() - static_cast<int>(std::lround(delta.x() * zoom)));
    }
    if (vBar) {
        vBar->setValue(vBar->value() - static_cast<int>(std::lround(delta.y() * zoom)));
    }
}

void HandTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(host);
    Q_UNUSED(scenePos);
    m_panning = false;
}

// ---------------------------------------------------------------------------
//  The zoom
// ---------------------------------------------------------------------------

void ZoomTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_dragging = false;
    LOG_DEBUG("[ZoomTool] onEnter host={}", host ? "yes" : "null");
}

void ZoomTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_dragging = false;
    m_host = nullptr;
}

void ZoomTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    auto* canvas = host->imageCanvas();
    if (!canvas) return;

    m_pressPos = scenePos;
    m_dragging = true;

    // The alt key switches between zooming in and zooming out.
    const bool alt = (e->modifiers() & Qt::AltModifier) != 0;
    const double factor = zoomFactorFor(alt);
    const double z = canvas->zoom() * factor;
    m_lastZoom = std::clamp(z, 0.05, 20.0);
    canvas->setZoom(m_lastZoom);
}

void ZoomTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(host);
    Q_UNUSED(scenePos);
    // The zoom applies on the press. The drag is reserved for the
    // "zoom to rectangle" behaviour, which is not implemented yet.
}

void ZoomTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(host);
    Q_UNUSED(scenePos);
    m_dragging = false;
}

// ---------------------------------------------------------------------------
//  The rotate view
// ---------------------------------------------------------------------------

void RotateViewTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_rotating = false;
    m_rotationDeg = 0.0;
    LOG_DEBUG("[RotateViewTool] onEnter host={}", host ? "yes" : "null");
}

void RotateViewTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_rotating = false;
    m_host = nullptr;
}

void RotateViewTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    m_rotating = true;
    m_pressPos = scenePos;
    m_rotationDeg = 0.0;
}

void RotateViewTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_rotating || !host) return;

    auto* canvas = host->imageCanvas();
    if (!canvas) return;

    // The horizontal distance from the press point drives the angle.
    // One full turn is 800 scene pixels.
    const double dx = scenePos.x() - m_pressPos.x();
    m_rotationDeg = dx * 360.0 / 800.0;

    // The VIEW is rotated, the pixels are not.
    QTransform t;
    t.rotate(m_rotationDeg);
    canvas->setTransform(t);
}

void RotateViewTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(host);
    Q_UNUSED(scenePos);
    m_rotating = false;
}

} // namespace tools
