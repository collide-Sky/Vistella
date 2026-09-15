// SPDX-License-Identifier: MIT
//
// ShapeTool - P0-9.1 (2026-09-15)
//
// PS 同款形状工具 (5 kind: Rectangle / Ellipse / Line / Polygon / Custom):
//   - click + drag: 创建 shape (跟 PS marquee 同款)
//   - Rectangle: rect = press..current
//   - Ellipse:    矩形框内切圆
//   - Line:       press -> release
//   - Polygon:    click 多个点, 双击 / Enter 闭合
//   - Custom:     path freehand (mouseMove 加 point)
//
// 集成:
//   - 创建 shape 后 push ShapeCommand 到 undoStack
//   - Layer::Vector kind + vectorPaths/vectorFillColors/vectorStrokeWidths
//   - optionPage: kind combo + fill color + stroke color + stroke width
//
#pragma once

#include "ToolState.h"
#include "../graphicstextitem.h"  // for Handle enum (re-use for shape handles)

#include <QCursor>
#include <QPainterPath>
#include <QPointF>
#include <QPointer>
#include <memory>

class QMouseEvent;
class QKeyEvent;
class QWidget;
class ImageWindow;
class QUndoCommand;

namespace tools {

class ShapeTool : public ToolState
{
public:
    enum class ShapeKind {
        Rectangle = 0,
        Ellipse   = 1,
        Line      = 2,
        Polygon   = 3,
        Custom    = 4,
    };

    explicit ShapeTool(QWidget* parent = nullptr);
    ~ShapeTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;
    void onKeyRelease(QKeyEvent* e, ImageWindow* host) override;

    mediators::ToolId id() const override { return mediators::ToolId::Shape; }
    QString pageTitle() const override { return QStringLiteral("形状"); }
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // Kind / colors / width (optionPage 直接调)
    ShapeKind kind() const { return m_kind; }
    void setKind(ShapeKind k) { m_kind = k; }

private:
    // 拖动状态机
    enum State { Idle, Dragging, PolygonCollecting };
    State m_state = Idle;

    ShapeKind m_kind = ShapeKind::Rectangle;

    QPointF m_pressScenePos;
    QPointF m_lastScenePos;

    // Polygon 临时点 (mouse press 加点, double-click close)
    QVector<QPointF> m_polygonPoints;
    // Custom path 临时点 (mouse move 加点)
    QPainterPath m_customPath;

    QPointer<ImageWindow> m_host;

    // helpers
    QPainterPath buildPath(const QPointF& a, const QPointF& b) const;
    QUndoCommand* makeShapeCommand(ImageWindow* host,
                                    const QPainterPath& path);
};

} // namespace tools