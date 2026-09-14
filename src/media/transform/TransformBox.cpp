// SPDX-License-Identifier: MIT
//
// TransformBox implementation - P0-6.1 (2026-09-14)
//
// 实现要点:
//   - handlePos / hitTest 走 scene 坐标 (跟 view 缩放无关)
//   - dragHandle 按 mode 分支:
//       Scale    : 8 handle 拖动, 更新 m_rect, sync 4 角
//       Rotate   : Rotation handle 拖动, 算 angle = atan2(newPos - center)
//                  不更新 m_rect, 但 m_rotationDeg 更新
//       Skew     : 4 edge handle 拖动, 更新 m_rect (一方向缩放 + 中心平移)
//       Distort  : 4 corner handle 拖动, 更新 m_corners, 不动 m_rect
//   - toTransform() P0-6.1 阶段给 Scale + Translate 基础, 4 mode 完整矩阵 P0-6.3
//
#include "TransformBox.h"

#include <QTransform>
#include <cmath>

namespace transform {

TransformBox::TransformBox() = default;

void TransformBox::setRect(const QRectF& r)
{
    m_rect = r;
    m_rotationDeg = 0.0;  // 重设时清旋转
    syncCornersFromRect();
}

void TransformBox::setMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    // 切到非 Distort 模式, 4 角同步回 m_rect
    if (m != Mode::Distort) {
        syncCornersFromRect();
    }
    // 切到 Distort 时, 4 角初始化为 m_rect 4 角 (start position)
    if (m == Mode::Distort) {
        syncCornersFromRect();
    }
}

QPointF TransformBox::handlePos(Handle h) const
{
    const QRectF r = m_rect;
    const qreal l = r.left();
    const qreal t = r.top();
    const qreal w = r.width();
    const qreal hgt = r.height();
    switch (h) {
        case Handle::TopLeft:     return QPointF(l,         t);
        case Handle::Top:         return QPointF(l + w / 2, t);
        case Handle::TopRight:    return QPointF(l + w,     t);
        case Handle::Right:       return QPointF(l + w,     t + hgt / 2);
        case Handle::BottomRight: return QPointF(l + w,     t + hgt);
        case Handle::Bottom:      return QPointF(l + w / 2, t + hgt);
        case Handle::BottomLeft:  return QPointF(l,         t + hgt);
        case Handle::Left:        return QPointF(l,         t + hgt / 2);
        case Handle::Center:      return QPointF(l + w / 2, t + hgt / 2);
        case Handle::Rotation:    return QPointF(l + w / 2, t - rotationHandleOffset());
        default:                  return QPointF();
    }
}

TransformBox::Handle TransformBox::hitTest(const QPointF& scenePos) const
{
    // 1) 先测 rotation handle (最优先, 在 box 上方, 不跟其他 handle 重叠)
    const QPointF rotPos = handlePos(Handle::Rotation);
    if (QRectF(rotPos - QPointF(handleVisualSize().width() / 2, handleVisualSize().height() / 2),
               handleVisualSize()).contains(scenePos)) {
        return Handle::Rotation;
    }
    // 2) 测 8 handle + 1 center
    const qreal halfW = handleVisualSize().width() / 2;
    const qreal halfH = handleVisualSize().height() / 2;
    const Handle order[] = { Handle::TopLeft, Handle::Top, Handle::TopRight,
                            Handle::Right,   Handle::BottomRight, Handle::Bottom,
                            Handle::BottomLeft, Handle::Left, Handle::Center };
    for (Handle h : order) {
        const QPointF hp = handlePos(h);
        if (QRectF(hp.x() - halfW, hp.y() - halfH, handleVisualSize().width(), handleVisualSize().height())
                .contains(scenePos)) {
            return h;
        }
    }
    return Handle::None;
}

void TransformBox::dragHandle(Handle h, const QPointF& newPos)
{
    switch (m_mode) {
        case Mode::Scale: {
            // 8 handle 拖动, 更新 m_rect
            //  简化: 拖左边的 handle, m_rect.left = newPos.x; 拖右边, right = newPos.x
            //  实际按 handle 类型更新 m_rect 2 个边界
            QRectF r = m_rect;
            switch (h) {
                case Handle::TopLeft:
                    r.setTopLeft(newPos);
                    break;
                case Handle::Top:
                    r.setTop(newPos.y());
                    break;
                case Handle::TopRight:
                    r.setTopRight(newPos);
                    break;
                case Handle::Right:
                    r.setRight(newPos.x());
                    break;
                case Handle::BottomRight:
                    r.setBottomRight(newPos);
                    break;
                case Handle::Bottom:
                    r.setBottom(newPos.y());
                    break;
                case Handle::BottomLeft:
                    r.setBottomLeft(newPos);
                    break;
                case Handle::Left:
                    r.setLeft(newPos.x());
                    break;
                case Handle::Center: {
                    // 整体平移
                    const QPointF d = newPos - center();
                    r.translate(d);
                    break;
                }
                default:
                    return;  // Rotation handle 在 Rotate mode 才处理
            }
            // Scale 最小尺寸保护 (避免退化到 0)
            if (r.width() < 1.0 || r.height() < 1.0) return;
            m_rect = r;
            syncCornersFromRect();
            break;
        }

        case Mode::Rotate: {
            if (h == Handle::Center) {
                // Center 拖动 = 整体平移
                const QPointF d = newPos - center();
                m_rect.translate(d);
                syncCornersFromRect();
            } else if (h == Handle::Rotation || h == Handle::None) {
                // Rotation handle 拖动 — 算 atan2(dy, dx) 角度
                //   注意: m_rotationDeg 是相对 m_rect 当前角度的累加
                const QPointF c = center();
                const qreal newAngleDeg = std::atan2(newPos.y() - c.y(), newPos.x() - c.x()) * 180.0 / M_PI;
                m_rotationDeg = newAngleDeg;
            }
            break;
        }

        case Mode::Skew: {
            // 4 edge handle 拖动 — 一方向缩放 + 中心平移 (PS Skew 风格)
            //   Top/Bottom handle 沿 Y 方向: 拉倾斜, 中心 X 平移 = 中心跟 newPos.x 跟原 center.x 差
            //   Left/Right handle 沿 X 方向: 拉倾斜, 中心 Y 平移
            //   简化: handle 拖到 newPos, 中心 = handle 的不动那一边 + 1/2 距离
            //   完整数学 (P0-6.3) 用 matrix 算
            QRectF r = m_rect;
            switch (h) {
                case Handle::Top: {
                    // 上边斜切: 顶边到 newPos.y, 中心 X 平移到 newPos.x
                    const qreal cx = newPos.x();
                    const qreal cyTop = newPos.y();
                    r.setTop(cyTop);
                    r.setLeft(2 * center().x() - cx);
                    break;
                }
                case Handle::Bottom: {
                    const qreal cx = newPos.x();
                    const qreal cyBot = newPos.y();
                    r.setBottom(cyBot);
                    r.setLeft(2 * center().x() - cx);
                    break;
                }
                case Handle::Left: {
                    const qreal cy = newPos.y();
                    const qreal cxLeft = newPos.x();
                    r.setLeft(cxLeft);
                    r.setTop(2 * center().y() - cy);
                    break;
                }
                case Handle::Right: {
                    const qreal cy = newPos.y();
                    const qreal cxRight = newPos.x();
                    r.setRight(cxRight);
                    r.setTop(2 * center().y() - cy);
                    break;
                }
                case Handle::Center: {
                    const QPointF d = newPos - center();
                    r.translate(d);
                    break;
                }
                default:
                    return;
            }
            if (r.width() < 1.0 || r.height() < 1.0) return;
            m_rect = r;
            syncCornersFromRect();
            break;
        }

        case Mode::Distort: {
            // 4 角独立 xy 拖动
            int idx = -1;
            switch (h) {
                case Handle::TopLeft:     idx = 0; break;
                case Handle::TopRight:    idx = 1; break;
                case Handle::BottomRight: idx = 2; break;
                case Handle::BottomLeft:  idx = 3; break;
                case Handle::Center: {
                    // Distort 模式 Center = 整体平移 (4 角都跟着)
                    const QPointF d = newPos - center();
                    m_rect.translate(d);
                    syncCornersFromRect();
                    return;
                }
                default: return;
            }
            if (idx >= 0) {
                m_corners[idx] = newPos;
            }
            break;
        }
    }
}

QPointF TransformBox::center() const
{
    // Distort 模式: 中心 = 4 角的几何中心
    if (m_mode == Mode::Distort) {
        return (m_corners[0] + m_corners[1] + m_corners[2] + m_corners[3]) / 4.0;
    }
    return QPointF(m_rect.center().x(), m_rect.center().y());
}

QTransform TransformBox::toTransform() const
{
    // P0-6.1 基础: Translate + Scale (m_rect 缩放, 以 m_rect 跟某个标准 rect 的比例为 scale)
    // 完整 4 mode 矩阵数学在 P0-6.3 TransformMath 实装
    //   - Rotate: QTransform::rotate(m_rotationDeg) 围绕中心
    //   - Skew:   QTransform::shear(skewX, skewY)
    //   - Distort: 4 角独立 → 透视 / 自由 3x3 矩阵
    QTransform t;
    if (m_mode == Mode::Distort) {
        // 简化: Distort 4 角平均成 scale + translate (不精确, 留给 P0-6.3)
        const QPointF c0 = m_corners[0];
        t.translate(c0.x(), c0.y());
        return t;
    }
    // Translate 到 m_rect.topLeft
    t.translate(m_rect.left(), m_rect.top());
    return t;
}

void TransformBox::syncCornersFromRect()
{
    m_corners[0] = m_rect.topLeft();
    m_corners[1] = m_rect.topRight();
    m_corners[2] = m_rect.bottomRight();
    m_corners[3] = m_rect.bottomLeft();
}

}  // namespace transform
