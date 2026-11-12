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
    m_dragRect = QRectF();

    // The alt key switches between zooming in and zooming out.
    const bool alt = (e->modifiers() & Qt::AltModifier) != 0;

    // The zoom applies on the press, and it zooms AT THE CLICK POINT.
    //
    //   The canvas uses AnchorUnderMouse, so setZoom() keeps the point
    //   under the cursor fixed -- that is the behaviour the user expects
    //   from a zoom tool.
    const double factor = zoomFactorFor(alt);
    const double z = std::clamp(canvas->zoom() * factor, 0.05, 20.0);
    m_lastZoom = z;
    canvas->setZoom(z);
}

void ZoomTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_dragging || !host) return;

    auto* canvas = host->imageCanvas();
    if (!canvas) return;

    // The drag builds the "zoom to rectangle" gesture. The rectangle is
    // only APPLIED on release -- zooming continuously while dragging
    // would move the scene out from under the pointer and the rectangle
    // would chase the cursor.
    m_dragRect = QRectF(m_pressPos, scenePos).normalized();
}

void ZoomTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!host) { m_dragging = false; return; }

    auto* canvas = host->imageCanvas();
    if (!canvas) { m_dragging = false; return; }

    // A drag of more than a couple of pixels means "zoom to this
    // rectangle". Anything smaller was a plain click (the zoom already
    // happened on the press).
    const bool wasDrag = m_dragRect.width() > 2.0 || m_dragRect.height() > 2.0;
    if (wasDrag) {
        if (canvas->zoomToRect(m_dragRect)) {
            m_lastZoom = canvas->zoom();
        }
    }

    m_dragging = false;
    m_dragRect = QRectF();
    Q_UNUSED(scenePos);
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

    // The angle is RELATIVE to the angle the view already had, so a
    // second drag adds to the first instead of snapping back to zero.
    m_startRotationDeg = 0.0;
    if (auto* canvas = host->imageCanvas()) {
        m_startRotationDeg = canvas->rotationDeg();
    }
    m_rotationDeg = m_startRotationDeg;
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
    const double deltaDeg = dx * 360.0 / 800.0;
    m_rotationDeg = m_startRotationDeg + deltaDeg;

    // The VIEW is rotated, the pixels are not.
    //
    //   The canvas owns the rotation and rebuilds the transform from it
    //   together with the zoom, so rotating no longer throws the zoom
    //   away and a later zoom change no longer throws the rotation away.
    canvas->setRotationDeg(m_rotationDeg);
}

void RotateViewTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(host);
    Q_UNUSED(scenePos);
    m_rotating = false;
}

} // namespace tools
