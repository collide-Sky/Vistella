// SPDX-License-Identifier: MIT
//
// TransformMath - P0-6.3 (2026-09-14)
//
// 4 mode 矩阵数学 (Scale / Rotate / Skew / Distort) — 一次性到位, 不分阶段实装
//   - Scale:    等比 / 非等比缩放 (Shift = 等比)
//   - Rotate:   围绕中心旋转, 角度累加 (Rotate handle 拖动)
//   - Skew:     4 边中点斜切, Shift = 约束 1 方向
//   - Distort:  4 角独立 xy 拖动 (自由 3x3 矩阵 / 透视)
//
// 强约束:
//   - 数学公式参考 PS / GIMP 实现, 不糊 (用 cv::Mat 2x3 / 3x3 affine + 自写透视)
//   - 4 mode 一起实装, 不先 Scale 后 Distort
//   - 单位: scene 坐标 (跟 view 缩放无关)
//
#pragma once

#include "TransformBox.h"

#include <QPointF>
#include <QRectF>
#include <QTransform>

namespace transform {

class TransformMath
{
public:
    // 4 mode 入口
    //   box: 当前 box 状态 (含 rect, corners, rotation, mode)
    //   handle: 拖动的 handle (None = 不拖, 只算 box 当前 transform)
    //   newPos: 拖到的位置 (scene 坐标)
    //   shift: Shift 键是否按下
    // 返回 3x3 QTransform
    static QTransform computeTransform(const TransformBox& box,
                                       TransformBox::Handle handle,
                                       const QPointF& newPos,
                                       bool shift);

    // 单独算 4 mode 矩阵 (P0-6.7 tests 用)
    static QTransform scaleMatrix(const QRectF& rect, TransformBox::Handle handle,
                                 const QPointF& newPos, bool shift);
    static QTransform rotateMatrix(const QRectF& rect, qreal angleDeg);
    static QTransform skewMatrix(const QRectF& rect, TransformBox::Handle handle,
                                 const QPointF& newPos, bool shift);
    static QTransform distortMatrix(const QPointF* corners);

    // 复合: 旋转 + 缩放 (4 mode 公共基础)
    //   Translate 中心到原点 → Rotate → Scale → Translate 回中心
    static QTransform rotateAroundCenter(const QPointF& center, qreal angleDeg,
                                        const QRectF& sourceRect);

    // 辅助: 算 Shift 等比缩放后的目标 (Scale 模式 Shift = 沿对角方向联动)
    static QPointF constrainedScalePoint(const QRectF& origRect,
                                         TransformBox::Handle handle,
                                         const QPointF& newPos,
                                         bool shift);

    // 辅助: 算 Skew 模式 Shift 约束 (Top/Bottom: X 不动, Left/Right: Y 不动)
    static QPointF constrainedSkewPoint(const QRectF& origRect,
                                        TransformBox::Handle handle,
                                        const QPointF& newPos,
                                        bool shift);
};

}  // namespace transform
