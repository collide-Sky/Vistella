// SPDX-License-Identifier: MIT
//
// ToolGeometry implementation.
//
// See ToolGeometry.h for the why.
#include "ToolGeometry.h"

#include "../imagewindow.h"

#include <QSize>

#include <algorithm>

namespace tools {

QSize ToolGeometry::canvasSize(ImageWindow* host)
{
    if (!host) return {};
    const cv::Mat& img = host->currentImage();
    if (img.empty()) return {};
    return QSize(img.cols, img.rows);
}

QPointF ToolGeometry::clampToCanvas(ImageWindow* host, const QPointF& scenePos)
{
    const QSize sz = canvasSize(host);
    if (sz.isEmpty()) return scenePos;

    // The canvas covers the pixels 0..w-1 / 0..h-1. A position exactly
    // at w is one past the last pixel, so it is clamped down to w-1.
    const double maxX = std::max(0.0, double(sz.width() - 1));
    const double maxY = std::max(0.0, double(sz.height() - 1));
    return QPointF(std::clamp(scenePos.x(), 0.0, maxX),
                   std::clamp(scenePos.y(), 0.0, maxY));
}

QRect ToolGeometry::clampRectToCanvas(ImageWindow* host, const QRect& rect)
{
    const QSize sz = canvasSize(host);
    if (sz.isEmpty()) return rect;

    QRect r = rect.normalized();
    if (r.width() > sz.width())  r.setWidth(sz.width());
    if (r.height() > sz.height()) r.setHeight(sz.height());
    r.moveLeft(std::clamp(r.left(), 0, std::max(0, sz.width() - r.width())));
    r.moveTop(std::clamp(r.top(), 0, std::max(0, sz.height() - r.height())));
    return r;
}

QPointF ToolGeometry::clampDeltaToCanvas(ImageWindow* host,
                                         const QPointF& delta,
                                         const QPointF& contentSize)
{
    const QSize sz = canvasSize(host);
    if (sz.isEmpty()) return QPointF(0, 0);

    // The content may not leave the canvas. How far it can go in each
    // direction depends on where it currently is -- which the caller
    // knows, not this function. The caller therefore passes the delta
    // already limited to what its own position allows; this helper
    // only makes sure the delta is finite and not larger than the
    // canvas itself (a delta bigger than the canvas can only ever be a
    // mis-clamp on the caller's side).
    const double limitX = std::max(0.0, double(sz.width()));
    const double limitY = std::max(0.0, double(sz.height()));
    return QPointF(std::clamp(delta.x(), -limitX, limitX),
                   std::clamp(delta.y(), -limitY, limitY));
}

} // namespace tools
