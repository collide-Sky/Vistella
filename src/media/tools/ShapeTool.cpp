// SPDX-License-Identifier: MIT
//
// ShapeTool implementation - P0-9.1 (2026-09-15)
//
// 详见 ShapeTool.h 头注释
//
#include "ShapeTool.h"
#include "../imagewindow.h"
#include "../imageworker/layers/LayerStack.h"
#include "../imageworker/layers/LayerCommand.h"
#include "logger.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

ShapeTool::ShapeTool(QWidget* /*parent*/) : ToolState() {}

void ShapeTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_state = Idle;
    m_polygonPoints.clear();
    m_customPath = QPainterPath();
    LOG_DEBUG("[ShapeTool] onEnter host={}", host ? "yes" : "null");
}

void ShapeTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_state = Idle;
    m_polygonPoints.clear();
    m_customPath = QPainterPath();
    m_host = nullptr;
    LOG_DEBUG("[ShapeTool] onExit");
}

QCursor ShapeTool::cursor() const
{
    // PS 同款: 十字光标
    return Qt::CrossCursor;
}

QPainterPath ShapeTool::buildPath(const QPointF& a, const QPointF& b) const
{
    QPainterPath p;
    switch (m_kind) {
    case ShapeKind::Rectangle: {
        p.addRect(QRectF(a, b).normalized());
        break;
    }
    case ShapeKind::Ellipse: {
        p.addEllipse(QRectF(a, b).normalized());
        break;
    }
    case ShapeKind::Line: {
        p.moveTo(a);
        p.lineTo(b);
        break;
    }
    case ShapeKind::Polygon:
    case ShapeKind::Custom:
    default:
        // Polygon / Custom 在 onMouseRelease 用 m_polygonPoints / m_customPath
        break;
    }
    return p;
}

void ShapeTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(host);
    if (!m_host || e->button() != Qt::LeftButton) return;

    if (m_kind == ShapeKind::Polygon) {
        // PS 同款: 单击加 point, 双击 close path
        m_polygonPoints.append(scenePos);
        m_state = PolygonCollecting;
        return;
    }
    if (m_kind == ShapeKind::Custom) {
        // Freehand path, 单击加 anchor
        if (m_customPath.isEmpty()) {
            m_customPath.moveTo(scenePos);
        } else {
            m_customPath.lineTo(scenePos);
        }
        m_state = PolygonCollecting;   // reuse state for collecting
        return;
    }

    // Rectangle/Ellipse/Line: 标准 press..drag..release
    m_pressScenePos = scenePos;
    m_lastScenePos  = scenePos;
    m_state = Dragging;
}

void ShapeTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& scenePos)
{
    if (m_state == Dragging) {
        m_lastScenePos = scenePos;
    }
}

void ShapeTool::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host) return;

    if (m_kind == ShapeKind::Polygon) {
        // Polygon 不在 release 创建, 等 double-click / Enter
        Q_UNUSED(scenePos);
        return;
    }
    if (m_kind == ShapeKind::Custom) {
        // Custom path 持续累计, mouseRelease 不创建 (等 explicit "complete" 按钮)
        Q_UNUSED(scenePos);
        return;
    }

    if (m_state != Dragging) return;
    QPainterPath path = buildPath(m_pressScenePos, scenePos);
    if (path.isEmpty()) {
        m_state = Idle;
        return;
    }

    // 创建 Vector layer
    layers::Layer vecLayer;
    vecLayer.kind = layers::Layer::Vector;
    vecLayer.name = QStringLiteral("形状 (vector)");
    vecLayer.vectorPaths.append(path);
    // 默认 fill color = 当前前景色 (P0-9.1 简化: 固定半透明色)
    vecLayer.vectorFillColors.append(QColor(0, 120, 215, 180));   // 蓝色半透明
    vecLayer.vectorStrokeWidths.append(1.5);

    auto* stack = host->layerStack();
    if (stack) {
        stack->addLayer(vecLayer);
        // P0-9.1: 推 LayerCommand::Add 到 undoStack
        if (auto* undoStack = host->undoStack()) {
            undoStack->push(new layers::LayerCommand(stack,
                                                     layers::LayerCommand::Add,
                                                     vecLayer));
        }
        LOG_INFO("[ShapeTool] created vector layer (path elements={})",
                 path.elementCount());
    }

    m_state = Idle;
}

void ShapeTool::onKeyPress(QKeyEvent* e, ImageWindow* host)
{
    if (!m_host || !host) return;

    if (e->key() == Qt::Key_Escape) {
        m_polygonPoints.clear();
        m_customPath = QPainterPath();
        m_state = Idle;
        LOG_DEBUG("[ShapeTool] Escape: cancelled collecting");
        return;
    }

    if (m_kind == ShapeKind::Polygon
        && m_state == PolygonCollecting
        && m_polygonPoints.size() >= 3
        && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        // Enter 闭合 polygon
        QPainterPath path;
        path.moveTo(m_polygonPoints.first());
        for (int i = 1; i < m_polygonPoints.size(); ++i) {
            path.lineTo(m_polygonPoints[i]);
        }
        path.closeSubpath();

        layers::Layer vecLayer;
        vecLayer.kind = layers::Layer::Vector;
        vecLayer.name = QStringLiteral("多边形 (vector)");
        vecLayer.vectorPaths.append(path);
        vecLayer.vectorFillColors.append(QColor(0, 120, 215, 180));
        vecLayer.vectorStrokeWidths.append(1.5);

        if (auto* stack = host->layerStack()) {
            stack->addLayer(vecLayer);
            if (auto* undoStack = host->undoStack()) {
                undoStack->push(new layers::LayerCommand(stack,
                                                         layers::LayerCommand::Add,
                                                         vecLayer));
            }
        }
        m_polygonPoints.clear();
        m_state = Idle;
    }
    // Custom path 完成: Esc or 双击 — 双击在 mousePressEvent 处理
}

void ShapeTool::onKeyRelease(QKeyEvent* /*e*/, ImageWindow* /*host*/) {}

QWidget* ShapeTool::optionPage(QWidget* parent)
{
    // PS 同款: 5 kind combo + Fill color + Stroke color + Stroke width
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    auto* kindLabel = new QLabel(QStringLiteral("形状:"), page);
    layout->addWidget(kindLabel);

    auto* kindCombo = new QComboBox(page);
    kindCombo->addItem(QStringLiteral("矩形"),     static_cast<int>(ShapeKind::Rectangle));
    kindCombo->addItem(QStringLiteral("椭圆"),     static_cast<int>(ShapeKind::Ellipse));
    kindCombo->addItem(QStringLiteral("直线"),     static_cast<int>(ShapeKind::Line));
    kindCombo->addItem(QStringLiteral("多边形"),   static_cast<int>(ShapeKind::Polygon));
    kindCombo->addItem(QStringLiteral("自定义"),   static_cast<int>(ShapeKind::Custom));
    kindCombo->setCurrentIndex(static_cast<int>(m_kind));
    layout->addWidget(kindCombo);

    auto* hint = new QLabel(QStringLiteral("(双击/Enter 闭合 Polygon)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addWidget(hint);

    layout->addStretch(1);

    QObject::connect(kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     page, [this, kindCombo](int idx) {
        m_kind = static_cast<ShapeKind>(kindCombo->itemData(idx).toInt());
        LOG_DEBUG("[ShapeTool] kind changed: {}", static_cast<int>(m_kind));
    });

    LOG_DEBUG("[ShapeTool] optionPage created");
    return page;
}

} // namespace tools