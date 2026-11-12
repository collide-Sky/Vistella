// SPDX-License-Identifier: MIT
//
// HandTool / ZoomTool / RotateViewTool
//
// The three "view" tools.
//
//   The audit (docs/CODE_AUDIT_2026-10-26.md 04-18) found that the
//   canvas had ScrollHandDrag permanently on, so the left button panned
//   the canvas with EVERY tool -- the user could not paint without
//   accidentally scrolling the view.
//
//   M2-4 gives the pan its own tool (the hand) and takes the pan away
//   from the painting tools. The canvas still keeps ScrollHandDrag as
//   the fallback for the middle button, but the left button belongs to
//   the tool.
//
//   The hand tool:
//     * left drag  -> pan the canvas
//     * space + drag -> pan (the Photoshop shortcut)
//
//   The zoom tool:
//     * click      -> zoom in at the click point
//     * alt + click-> zoom out
//     * drag       -> zoom to the dragged rectangle
//
//   The rotate view tool:
//     * drag       -> rotate the view (the scene is rotated, the
//                     pixels are not -- that is what "rotate the view"
//                     means in Photoshop).
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>
#include <QRectF>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

// ---------------------------------------------------------------------------
//  The hand -- pans the canvas.
// ---------------------------------------------------------------------------
class HandTool : public ToolState
{
public:
    HandTool() = default;
    ~HandTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    // The hand owns the left button, so it must take the event away
    // from the view's own scroll-hand drag.
    bool wantsExclusiveMouse() const override { return true; }

    mediators::ToolId id() const override { return mediators::ToolId::Hand; }
    QString pageTitle() const override { return QStringLiteral("Hand"); }
    QCursor cursor() const override { return QCursor(Qt::OpenHandCursor); }

    // ===== test accessors =====
    bool    isPanning() const { return m_panning; }
    QPointF panDelta()  const { return m_panDelta; }

private:
    QPointer<ImageWindow> m_host;
    bool    m_panning = false;
    QPointF m_pressPos;
    QPointF m_panDelta;
};

// ---------------------------------------------------------------------------
//  The zoom -- zooms the canvas.
// ---------------------------------------------------------------------------
class ZoomTool : public ToolState
{
public:
    ZoomTool() = default;
    ~ZoomTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    bool wantsExclusiveMouse() const override { return true; }

    mediators::ToolId id() const override { return mediators::ToolId::Zoom; }
    QString pageTitle() const override { return QStringLiteral("Zoom"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }

    // ===== test accessors =====
    bool isDragging() const { return m_dragging; }
    double lastZoom()  const { return m_lastZoom; }
    // The rectangle the drag gesture built (empty until the mouse moves).
    QRectF dragRect() const { return m_dragRect; }

    // The zoom factor a click applies: in when the alt key is NOT
    // pressed, out when it is.
    static double zoomFactorFor(bool altPressed) { return altPressed ? 1.0 / 1.25 : 1.25; }

private:
    QPointer<ImageWindow> m_host;
    bool    m_dragging = false;
    QPointF m_pressPos;
    double  m_lastZoom = 1.0;
    // The "zoom to rectangle" gesture rectangle. Applied on release.
    QRectF  m_dragRect;
};

// ---------------------------------------------------------------------------
//  The rotate view -- rotates the view, not the pixels.
// ---------------------------------------------------------------------------
class RotateViewTool : public ToolState
{
public:
    RotateViewTool() = default;
    ~RotateViewTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    bool wantsExclusiveMouse() const override { return true; }

    mediators::ToolId id() const override { return mediators::ToolId::Rotate; }
    QString pageTitle() const override { return QStringLiteral("Rotate View"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }

    // ===== test accessors =====
    bool  isRotating() const { return m_rotating; }
    double rotationDeg() const { return m_rotationDeg; }

private:
    QPointer<ImageWindow> m_host;
    bool   m_rotating = false;
    QPointF m_pressPos;
    double m_rotationDeg = 0.0;
    // The angle the view had when the drag started. The drag ADDS to
    // it, so repeated drags accumulate instead of snapping back to zero.
    double m_startRotationDeg = 0.0;
};

} // namespace tools
