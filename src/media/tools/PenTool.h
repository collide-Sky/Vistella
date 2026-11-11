// SPDX-License-Identifier: MIT
//
// PenTool
//
// PS 同款钢笔工具 (P):
//   - 单击: 加 anchor point (起点自动 moveTo, 后续 lineTo)
//   - 双击 / Enter: 闭合 path (close subpath)
//   - Esc: 取消 collecting
//   - Alt + 单击: 删除最近 anchor
//
// M2-4 的改动:
//   - 双击闭合真的能发生了 (ToolState 之前没有 onMouseDoubleClick 这个入口,
//     键盘通路也是断的 —— 两条路都到不了).
//   - Alt + 单击删 anchor 之前只是注释里的一句话, 现在看 modifiers.
//   - anchor 坐标钳制到画布内, 拖出画布的点击不会写一个画布外的点.
//   - commitPath 现在真的 closeSubpath —— 之前是开放折线, 但注释和 UI
//     都写着"闭合".
//
// 集成:
//   - 创建 path 后 push LayerCommand::Add (VectorKind) to undoStack
//   - Layer::Vector kind + vectorPaths/vectorFillColors/vectorStrokeWidths
//     注意: 矢量路径的坐标是**图像像素坐标** (契约见 LayerStack.h).
//
#pragma once

#include "ToolState.h"

#include <QCursor>
#include <QPainterPath>
#include <QPointF>
#include <QPointer>

class QMouseEvent;
class QKeyEvent;
class QWidget;
class ImageWindow;

namespace tools {

class PenTool : public ToolState
{
public:
    enum class Mode {
        AddAnchor    = 0,    // 单击加 anchor (默认)
        RemoveAnchor = 1,    // 单击删除最近 anchor
    };

    explicit PenTool(QWidget* parent = nullptr);
    ~PenTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    // M2-4: 双击闭合路径. ToolState 现在有这个入口了.
    void onMouseDoubleClick(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;
    void onKeyRelease(QKeyEvent* e, ImageWindow* host) override;

    mediators::ToolId id() const override { return mediators::ToolId::Pen; }
    QString pageTitle() const override { return QStringLiteral("钢笔"); }
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    Mode mode() const { return m_mode; }
    void setMode(Mode m) { m_mode = m; }

    // test accessors
    int  anchorCount() const { return m_anchors.size(); }
    bool isCollecting() const { return !m_anchors.isEmpty(); }
    void clearAnchorsForTest() { m_anchors.clear(); }

    // The path that would be committed right now.
    //
    //   Exposed so a test can check the geometry (closed or not, how many
    //   segments) without going through the layer stack.
    QPainterPath buildPath(bool closed) const;

private:
    Mode m_mode = Mode::AddAnchor;

    QVector<QPointF> m_anchors;     // 累计 anchor points (画布像素坐标)
    QPointer<ImageWindow> m_host;

    // Build the path, add the vector layer and push the undo command.
    //
    //   `closed` closes the subpath (the polygon case). A path with
    //   fewer than two anchors is discarded.
    void commitPath(ImageWindow* host, bool closed);
};

} // namespace tools
