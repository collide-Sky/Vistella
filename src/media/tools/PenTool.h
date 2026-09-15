// SPDX-License-Identifier: MIT
//
// PenTool - P0-9.2 (2026-09-15)
//
// PS 同款钢笔工具 (P):
//   - 单击: 加 anchor point (起点自动 moveTo, 后续 lineTo)
//   - 单击 + 拖动: 加 anchor + control point (Bezier handle)
//   - 双击 / Enter: 闭合 path (close subpath)
//   - Esc: 取消 collecting
//   - Alt + 单击: 删除最近 anchor
//
// 集成:
//   - 创建 path 后 push LayerCommand::Add (VectorKind) to undoStack
//   - Layer::Vector kind + vectorPaths/vectorFillColors/vectorStrokeWidths
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
    // P0-9.2: 双击闭合路径 — ToolState 没声明 onMouseDoubleClick, 走 mousePress + key fallback
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;
    void onKeyRelease(QKeyEvent* e, ImageWindow* host) override;

    mediators::ToolId id() const override { return mediators::ToolId::Pen; }
    QString pageTitle() const override { return QStringLiteral("钢笔"); }
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    Mode mode() const { return m_mode; }
    void setMode(Mode m) { m_mode = m; }

    // P0-9.2 test accessors
    int  anchorCount() const { return m_anchors.size(); }
    bool isCollecting() const { return !m_anchors.isEmpty(); }
    void clearAnchorsForTest() { m_anchors.clear(); }

private:
    Mode m_mode = Mode::AddAnchor;

    QVector<QPointF> m_anchors;     // 累计 anchor points
    QPointer<ImageWindow> m_host;

    void commitPath(ImageWindow* host);
};

} // namespace tools