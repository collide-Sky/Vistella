// SPDX-License-Identifier: MIT
//
// ShapeTool
//
// PS 同款形状工具 (5 kind: Rectangle / Ellipse / Line / Polygon / Custom):
//   - click + drag: 创建 shape (跟 PS marquee 同款)
//   - Rectangle: rect = press..current
//   - Ellipse:    矩形框内切圆
//   - Line:       press -> release
//   - Polygon:    click 多个点, 双击 / Enter 闭合
//   - Custom:     path freehand (mouseMove 加 point), 「完成」按钮提交
//
// M2-4 的改动:
//   - Polygon 的双击闭合真的能发生了 (之前 ToolState 没有 onMouseDoubleClick
//     这个入口, 键盘通路也是断的 —— 两条路都到不了, 所以多边形永远闭合不了).
//   - Custom path 有了「完成」按钮 —— 之前它只能靠一个根本不存在的
//     "complete 按钮" 提交, 于是永远提交不了.
//   - 形状的坐标钳制到画布内 (之前直接用 scene 坐标, 画到画布外).
//   - 键盘事件现在真的能到工具 (键盘通路在 M2-4 补上).
//
// 集成:
//   - 创建 shape 后 push LayerCommand::Add 到 undoStack
//   - Layer::Vector kind + vectorPaths/vectorFillColors/vectorStrokeWidths
//     注意: 矢量路径的坐标是**图像像素坐标** (契约见 LayerStack.h).
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
    // M2-4: 双击闭合 polygon.
    void onMouseDoubleClick(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;
    void onKeyRelease(QKeyEvent* e, ImageWindow* host) override;

    mediators::ToolId id() const override { return mediators::ToolId::Shape; }
    QString pageTitle() const override { return QStringLiteral("形状"); }
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // Kind / colors / width (optionPage 直接调)
    ShapeKind kind() const { return m_kind; }
    void setKind(ShapeKind k) { m_kind = k; }

    // fill / stroke 颜色 + stroke 宽度 (optionPage 直接调)
    QColor fillColor() const { return m_fillColor; }
    QColor strokeColor() const { return m_strokeColor; }
    qreal  strokeWidth() const { return m_strokeWidth; }
    void setFillColor(const QColor& c)   { m_fillColor = c; }
    void setStrokeColor(const QColor& c) { m_strokeColor = c; }
    void setStrokeWidth(qreal w)         { m_strokeWidth = w; }

    // test accessors
    int  polygonPointCount() const { return m_polygonPoints.size(); }
    bool isCollecting() const { return m_state == PolygonCollecting; }

private:
    // 拖动状态机
    enum State { Idle, Dragging, PolygonCollecting };
    State m_state = Idle;

    ShapeKind m_kind = ShapeKind::Rectangle;

    // fill / stroke 颜色 + stroke 宽度 (默认蓝色半透明 + 深灰边框)
    QColor m_fillColor   = QColor(0, 120, 215, 180);
    QColor m_strokeColor = QColor(40, 40, 40, 255);
    qreal  m_strokeWidth = 1.5;

    QPointF m_pressScenePos;
    QPointF m_lastScenePos;

    // Polygon 临时点 (mouse press 加点, double-click / Enter 闭合)
    QVector<QPointF> m_polygonPoints;
    // Custom path 临时点 (mouse press 加点)
    QPainterPath m_customPath;

    QPointer<ImageWindow> m_host;

    // helpers
    QPainterPath buildPath(const QPointF& a, const QPointF& b) const;
    // Add the vector layer and push the undo command.
    void addShapeLayer(ImageWindow* host, const QPainterPath& path, const QString& name);
    // Commit the polygon / custom path that is being collected.
    void commitCollectedPath(ImageWindow* host);
    // Cancel the collection (Esc).
    void cancelCollecting();
};

} // namespace tools
