// SPDX-License-Identifier: MIT
//
// TransformMath implementation - P0-6.3 (2026-09-14)
//
#include "TransformMath.h"

#include <QtMath>
#include <cmath>

namespace transform {

namespace {

// oppositeCorner: 4 角 handle 的对角 (约束缩放 anchor 用)
inline QPointF oppositeCorner(const QRectF& r, TransformBox::Handle h)
{
    switch (h) {
        case TransformBox::Handle::TopLeft:     return r.bottomRight();
        case TransformBox::Handle::TopRight:    return r.bottomLeft();
        case TransformBox::Handle::BottomRight: return r.topLeft();
        case TransformBox::Handle::BottomLeft:  return r.topRight();
        default:                                 return r.center();
    }
}

}  // namespace (anonymous)

QTransform TransformMath::computeTransform(const TransformBox& box,
                                            TransformBox::Handle handle,
                                            const QPointF& newPos,
                                            bool shift)
{
    switch (box.mode()) {
        case TransformBox::Mode::Scale:    return scaleMatrix(box.rect(), handle, newPos, shift);
        case TransformBox::Mode::Rotate:   return rotateMatrix(box.rect(), box.rotation());
        case TransformBox::Mode::Skew:     return skewMatrix(box.rect(), handle, newPos, shift);
        case TransformBox::Mode::Distort:  return distortMatrix(box.rect(), box.cornersArray());
    }
    return QTransform();
}

// P3.2.4 (2026-09-22): 用 origRect 算 Scale/Skew. Rotate/Distort 跟 origRect 无关.
QTransform TransformMath::computeTransformWithOrigin(const TransformBox& box,
                                                      TransformBox::Handle handle,
                                                      const QPointF& newPos,
                                                      bool shift,
                                                      const QRectF& origRect)
{
    switch (box.mode()) {
        case TransformBox::Mode::Scale:    return scaleMatrix(origRect, handle, newPos, shift);
        case TransformBox::Mode::Rotate:   return rotateMatrix(box.rect(), box.rotation());
        case TransformBox::Mode::Skew:     return skewMatrix(origRect, handle, newPos, shift);
        case TransformBox::Mode::Distort:  return distortMatrix(box.rect(), box.cornersArray());
    }
    return QTransform();
}

QTransform TransformMath::scaleMatrix(const QRectF& rect, TransformBox::Handle handle,
                                      const QPointF& newPos, bool shift)
{
    QRectF newRect = rect;
    QPointF constrained = newPos;
    if (shift) {
        constrained = constrainedScalePoint(rect, handle, newPos, true);
    }
    switch (handle) {
        case TransformBox::Handle::TopLeft:     newRect.setTopLeft(constrained); break;
        case TransformBox::Handle::Top:         newRect.setTop(constrained.y()); break;
        case TransformBox::Handle::TopRight:    newRect.setTopRight(constrained); break;
        case TransformBox::Handle::Right:       newRect.setRight(constrained.x()); break;
        case TransformBox::Handle::BottomRight: newRect.setBottomRight(constrained); break;
        case TransformBox::Handle::Bottom:      newRect.setBottom(constrained.y()); break;
        case TransformBox::Handle::BottomLeft:  newRect.setBottomLeft(constrained); break;
        case TransformBox::Handle::Left:        newRect.setLeft(constrained.x()); break;
        case TransformBox::Handle::Center: {
            const QPointF d = constrained - QPointF(rect.center().x(), rect.center().y());
            newRect.translate(d);
            break;
        }
        default: break;
    }
    if (newRect.width() < 1.0 || newRect.height() < 1.0) {
        newRect = rect;
    }
    const qreal sx = newRect.width() / rect.width();
    const qreal sy = newRect.height() / rect.height();
    QTransform t;
    t.translate(newRect.left() - rect.left() * sx, newRect.top() - rect.top() * sy);
    t.scale(sx, sy);
    return t;
}

QTransform TransformMath::rotateMatrix(const QRectF& rect, qreal angleDeg)
{
    if (qFuzzyIsNull(angleDeg)) return QTransform();
    const QPointF c = rect.center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(angleDeg);
    t.translate(-c.x(), -c.y());
    return t;
}

QTransform TransformMath::skewMatrix(const QRectF& rect, TransformBox::Handle handle,
                                     const QPointF& newPos, bool shift)
{
    QPointF constrained = newPos;
    if (shift) {
        constrained = constrainedSkewPoint(rect, handle, newPos, true);
    }
    const QPointF c = rect.center();
    qreal skewX = 0.0;
    qreal skewY = 0.0;
    switch (handle) {
        case TransformBox::Handle::Top: {
            const qreal dy = constrained.y() - rect.top();
            skewY = dy / std::max(rect.width(), 1.0);
            break;
        }
        case TransformBox::Handle::Bottom: {
            const qreal dy = constrained.y() - rect.bottom();
            skewY = dy / std::max(rect.width(), 1.0);
            break;
        }
        case TransformBox::Handle::Left: {
            const qreal dx = constrained.x() - rect.left();
            skewX = dx / std::max(rect.height(), 1.0);
            break;
        }
        case TransformBox::Handle::Right: {
            const qreal dx = constrained.x() - rect.right();
            skewX = dx / std::max(rect.height(), 1.0);
            break;
        }
        case TransformBox::Handle::Center: {
            const QPointF d = constrained - c;
            QTransform t;
            t.translate(d.x(), d.y());
            return t;
        }
        default: break;
    }
    QTransform t;
    t.translate(c.x(), c.y());
    t.shear(skewX, skewY);
    t.translate(-c.x(), -c.y());
    return t;
}

QTransform TransformMath::distortMatrix(const QRectF& origRect, const QPointF* corners)
{
    if (!corners) return QTransform();
    // P3.2.1 (2026-09-22): src = origRect 4 角 (TL/TR/BR/BL), dst = 扭曲后 corners.
    //   旧实现 quadToQuad(src, src) 两边相同, 数学上退化成 identity (src == dst),
    //   4 角拖动后根本没产生透视/仿射变换 — Distort 模式视觉无变化.
    QPolygonF src;
    src << origRect.topLeft() << origRect.topRight()
        << origRect.bottomRight() << origRect.bottomLeft();
    QPolygonF dst;
    dst << corners[0] << corners[1] << corners[2] << corners[3];
    QTransform t;
    bool ok = QTransform::quadToQuad(src, dst, t);
    if (!ok) {
        // quadToQuad 失败 (退化成三点共线等) — fallback identity,
        // caller 仍能 commit, 视觉图像不变 (跟原 P0-6.3 行为一致)
        return QTransform();
    }
    return t;
}

QTransform TransformMath::rotateAroundCenter(const QPointF& center, qreal angleDeg,
                                             const QRectF& sourceRect)
{
    Q_UNUSED(sourceRect);
    QTransform t;
    t.translate(center.x(), center.y());
    t.rotate(angleDeg);
    t.translate(-center.x(), -center.y());
    return t;
}

QPointF TransformMath::constrainedScalePoint(const QRectF& origRect,
                                              TransformBox::Handle handle,
                                              const QPointF& newPos,
                                              bool shift)
{
    if (!shift) return newPos;
    const QPointF anchor = oppositeCorner(origRect, handle);
    const qreal w0 = origRect.width();
    const qreal h0 = origRect.height();
    if (qFuzzyIsNull(w0) || qFuzzyIsNull(h0)) return newPos;
    qreal dx = (newPos.x() - anchor.x()) / w0;
    qreal dy = (newPos.y() - anchor.y()) / h0;
    bool isCorner = (handle == TransformBox::Handle::TopLeft
                  || handle == TransformBox::Handle::TopRight
                  || handle == TransformBox::Handle::BottomRight
                  || handle == TransformBox::Handle::BottomLeft);
    if (isCorner) {
        qreal s = std::max(std::abs(dx), std::abs(dy));
        if (qFuzzyIsNull(s)) return newPos;
        if (dx < 0) s = -s;
        if (dy < 0) s = -s;
        return QPointF(anchor.x() + s * w0, anchor.y() + s * h0);
    }
    return newPos;
}

QPointF TransformMath::constrainedSkewPoint(const QRectF& origRect,
                                             TransformBox::Handle handle,
                                             const QPointF& newPos,
                                             bool shift)
{
    if (!shift) return newPos;
    switch (handle) {
        case TransformBox::Handle::Top:    return QPointF(newPos.x(), origRect.top());
        case TransformBox::Handle::Bottom: return QPointF(newPos.x(), origRect.bottom());
        case TransformBox::Handle::Left:   return QPointF(origRect.left(), newPos.y());
        case TransformBox::Handle::Right:  return QPointF(origRect.right(), newPos.y());
        default: return newPos;
    }
}

}  // namespace transform
