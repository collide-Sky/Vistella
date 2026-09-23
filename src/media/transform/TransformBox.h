// SPDX-License-Identifier: MIT
//
// TransformBox - P0-6.1 (2026-09-14)
//
// 自由变换包围盒: PS 风格的 8 控制点 + 1 中心 + 1 旋转手柄
//   - 4 角 (TopLeft / TopRight / BottomRight / BottomLeft): 缩放或 Distort 自由拖动
//   - 4 边中 (Top / Right / Bottom / Left): 缩放或 Skew 斜切
//   - 1 中心 (Center): 移动 (整体平移, 跟 MoveTool 不同 — Transform 中心仍保留 8 handle 位置)
//   - 1 旋转手柄 (Rotation): 顶部中点上方 25 px, 鼠标拖动旋转
//
// 4 mode 矩阵数学在 P0-6.3 (TransformMath) 实装, 本类持有 4 corner 独立位置 + 1 rotation angle,
// Mode 切换跟 TransformMath 配合计算 final 3x3 matrix
//
// 集成:
//   - SelectionModel: setRect(m_selection->boundingRect()) 初始化
//   - ImageCanvas: 渲染 8 handle + box 边框
//   - TransformTool: onMousePress/Move/Release 通过 hitTest() 找 handle, dragHandle() 更新
//   - TransformCommand: 存 m_rect + m_corners + m_rotation, undo/redo 重设
//
// 强约束 (一次性到位, 不留补丁):
//   - 4 mode 一起实装 (Scale/Rotate/Skew/Distort), 不先 Scale 后 Distort
//   - handle hit test 走 scene 坐标 (不依赖 view 缩放)
//   - 中心 / 旋转手柄不参与 Distort (Distort 只动 4 角)
//
#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

namespace transform {

class TransformBox
{
public:
    // 4 mode (跟 ImageOptionBar combo 对应)
    enum class Mode {
        Scale    = 0,  // 缩放 (8 handle 拖动, Shift = 等比)
        Rotate   = 1,  // 旋转 (1 rotation handle, 中心点不动)
        Skew     = 2,  // 斜切 (4 edge 拖动, Shift = 约束 1 方向)
        Distort  = 3,  // 自由扭曲 (4 corner 独立 xy 拖动, 不联动)
    };

    // 10 handle
    enum class Handle {
        None       = 0,
        TopLeft    = 1,   // 角
        Top        = 2,   // 边中
        TopRight   = 3,
        Right      = 4,
        BottomRight = 5,
        Bottom     = 6,
        BottomLeft = 7,
        Left       = 8,
        Center     = 9,   // 中心 (整体平移)
        Rotation   = 10,  // 旋转手柄 (顶部上方 25 px)
    };

    TransformBox();

    // ===== 初始 / 重置 =====
    void setRect(const QRectF& r);   // 重设 4 角 (跟 boundingRect 同步)
    QRectF rect() const { return m_rect; }
    // P3.2.5 (2026-09-23): 拖动开始前的 origRect (Scale/Skew only).
    //   拖动中 (m_dragging=true) 返 m_dragOrigRect; 否则返 m_rect (跟 rect() 等价).
    //   commitTransform 用 origRect + newPos 算非平凡矩阵, 不退化成 identity.
    QRectF origRect() const { return m_dragging ? m_dragOrigRect : m_rect; }
    // P3.2.5: 重置 dragging 标志 (commit / cancel 后调用).
    void resetDrag() { m_dragging = false; }

    // ===== Mode 切换 =====
    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    // ===== Handle 几何 (scene 坐标) =====
    // 8 handle 中心点
    QPointF handlePos(Handle h) const;
    // handle 视觉尺寸 (10x10 像素, 在 scene 坐标下)
    static QSizeF handleVisualSize() { return QSizeF(10, 10); }
    // rotation handle 距顶边的偏移 (scene 单位, 默认 25 px)
    static qreal rotationHandleOffset() { return 25.0; }

    // ===== Hit test (scene 坐标 → handle) =====
    //   返回 None 表示没命中
    Handle hitTest(const QPointF& scenePos) const;

    // ===== 拖动 handle 更新 box =====
    //   Scale: handle 拖到 newPos (按 Shift 是否等比, 在 TransformTool 里处理)
    //   Rotate: handle = Center, newPos 是新光标位置 (中心点不动, angle 更新)
    //   Skew:   handle 是 4 边之一, 沿垂直或水平方向斜切
    //   Distort: handle 是 4 角之一, newPos 直接是 4 角之一的新位置
    //   handle = Center: 整体平移 (按 newPos 跟 m_center 差值)
    void dragHandle(Handle h, const QPointF& newPos);

    // ===== 状态访问 (TransformCommand 序列化用) =====
    qreal rotation() const { return m_rotationDeg; }
    void setRotation(qreal deg) { m_rotationDeg = deg; }

    // 4 角独立位置 (Distort 模式用)
    QPointF cornerTL() const { return m_corners[0]; }
    QPointF cornerTR() const { return m_corners[1]; }
    QPointF cornerBR() const { return m_corners[2]; }
    QPointF cornerBL() const { return m_corners[3]; }
    // 4 角数组 (TransformMath 等批量操作用)
    const QPointF* cornersArray() const { return m_corners; }

    // 中心点 (跟 m_rect 中心同步, 但 Distort 模式下 m_corners 不动, 中心仍可平移)
    QPointF center() const;

    // ===== 3x3 矩阵计算 (P0-6.3 TransformMath 完整实装) =====
    //   返回 QTransform, 用于 ImageCanvas 渲染 / cv::warpAffine
    //   P0-6.1 阶段给基础实现 (Scale + Translate), 4 mode 完整数学在 P0-6.3
    QTransform toTransform() const;

private:
    QRectF m_rect;             // 包围盒 (image 坐标系)
    Mode   m_mode = Mode::Scale;
    qreal  m_rotationDeg = 0.0;  // 旋转角度 (度, 顺时针为正)
    QPointF m_corners[4];        // TL, TR, BR, BL — Distort 模式独立位置

    // P3.2.5 (2026-09-23): Scale/Skew 拖动开始时记录的 origRect + dragging 标志.
    //   dragHandle Scale/Skew mode 第一次进入会 snapshot m_rect 到 m_dragOrigRect
    //   (m_dragging=true), 后续 m_rect 一直被更新到 newPos 后状态. commit
    //   时 caller 调 origRect() 拿 pre-drag state 算非 identity 矩阵.
    //   注: TransformBox 不是 Q_OBJECT 派生, 加字段不影响 QString d-pointer
    //   共享路径 (跟 P3.1.3 root cause 无关 — 那种 crash 限于 QWidget/QObject 类).
    QRectF m_dragOrigRect;
    bool   m_dragging = false;

    // 同步 m_corners 跟 m_rect (Scale/Skew 时, m_corners 跟 m_rect 4 角一致)
    void syncCornersFromRect();
};

}  // namespace transform
