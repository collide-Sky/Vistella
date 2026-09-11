// SPDX-License-Identifier: MIT
//
// ImageCanvas — QGraphicsView 派生, 接管 scene/zoom/wheel/eventFilter
// (P0-1: 拆 imagewindow.cpp 上帝类的 6 大组件之一)
//
// P0-1.3 (2026-09-07): full migration
//   - Owns m_scene / m_item / m_zoom (moved from ImageWindow)
//   - 4 zoom slots (onZoomIn / Out / FitWindow / ResetZoom) implemented here
//   - wheelEvent: Ctrl+wheel zoom logic (moved from ImageWindow::eventFilter)
//   - host's eventFilter no longer intercepts Wheel; ImageCanvas owns it
//   - mousePressEvent / mouseDoubleClickEvent / keyPressEvent / keyReleaseEvent
//     keep their P0-1.1 emit-signal behaviour (P0-1.4 will route these signals
//     and let eventFilter delegate to the component)
//
#include "ImageCanvas.h"

#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsEllipseItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>

#include "../selection/SelectionModel.h"
#include "../selection/MarchingAnts.h"
#include "logger.h"

ImageCanvas::ImageCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    m_item  = m_scene->addPixmap(QPixmap());
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // 关键: SmartViewportUpdate + ItemCoordinateCache 让拖拽更丝滑
    // 之前默认是 MinimalViewportUpdate, 拖动时整个 viewport 重绘 = 卡
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState |
                         QGraphicsView::DontAdjustForAntialiasing);
    // 阶段 1 前置优化 C.2 (2026-09-03): 补 2 个性能基线
    //   1) CacheBackground: 背景 (grid / 空 scene 底色) 缓存为 pixmap,
    //      scroll 时只画 items 不重画 background, 24MP 大图滚动顺滑很多
    //   2) scene NoIndex: 阶段 0/1 imageWorker 顶层只有 1 个 pixmap item + 几个文字 item,
    //      < 100 items 用 NoIndex 比默认 BspTreeIndex 快 (避免 BSP 树维护开销)
    //      阶段 1+ 如果加图层系统 (几十个 item), 改回 BspTreeIndex
    setCacheMode(QGraphicsView::CacheBackground);
    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setFocusPolicy(Qt::StrongFocus);

    // P0-4.3 (2026-09-10): marching ants ticker
    m_ants = new selection::MarchingAnts(this);
    connect(m_ants, &selection::MarchingAnts::phaseChanged,
            this, [this](int) { viewport()->update(); });
}

ImageCanvas::~ImageCanvas() = default;

void ImageCanvas::applyTransform() {
    // Re-apply current m_zoom: reset to identity then scale uniformly.
    // This is the canonical "set zoom factor" routine; any code path that
    // mutates m_zoom must call this afterwards.
    resetTransform();
    scale(m_zoom, m_zoom);
}

void ImageCanvas::setZoom(double z) {
    if (qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    applyTransform();
    emit zoomChanged(m_zoom);
}

void ImageCanvas::onZoomIn()    { setZoom(m_zoom * 1.25); }
void ImageCanvas::onZoomOut()   { setZoom(m_zoom / 1.25); }
void ImageCanvas::onResetZoom() { setZoom(1.0); }

void ImageCanvas::onFitWindow() {
    // 等价于原 ImageWindow::onFitWindow 里的 m_current.empty() 检查
    if (!m_item || m_item->pixmap().isNull()) return;
    fitInView(m_item, Qt::KeepAspectRatio);
    // fitInView 会同时设 scale + translate 让图像居中, 取出 scale 同步给 m_zoom
    // 这样后续 resetTransform + scale(m_zoom) 不会丢 fit 的居中效果
    m_zoom = transform().m11();
    emit zoomChanged(m_zoom);
}

void ImageCanvas::setBrushCursorRadius(int r) {
    if (!m_brushCursor) return;
    m_brushCursor->setRect(-r, -r, r * 2, r * 2);
}

void ImageCanvas::moveBrushCursor(const QPointF& scenePos, bool visible) {
    if (!m_brushCursor) return;
    m_brushCursor->setPos(scenePos);
    m_brushCursor->setVisible(visible);
}

void ImageCanvas::hideBrushCursor() {
    if (m_brushCursor) m_brushCursor->setVisible(false);
}

ImageCanvas::HitResult ImageCanvas::hitTest(const QPointF& /*scenePos*/) const {
    // P0-1.2: this is what eventFilter delegates to.
    return HitResult::None;
}

void ImageCanvas::wheelEvent(QWheelEvent* e) {
    // 滚轮缩放 (Ctrl 修饰键) — 原 ImageWindow::eventFilter 里的 wheel 段
    // 关键: 不走 QGraphicsView 默认滚轮 (默认是滚动条), Ctrl+wheel 才缩放
    if (e->modifiers() & Qt::ControlModifier) {
        const double step = (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
        double z = m_zoom * step;
        if (z < 0.05) z = 0.05;
        if (z > 20.0) z = 20.0;
        setZoom(z);
        e->accept();
        return;
    }
    // 其他 wheel 事件走默认 (滚动条)
    QGraphicsView::wheelEvent(e);
}

void ImageCanvas::mousePressEvent(QMouseEvent* e) {
    QGraphicsView::mousePressEvent(e);
    emit sceneClicked(mapToScene(e->pos()), e->button(), e->modifiers());
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    QGraphicsView::mouseDoubleClickEvent(e);
    emit sceneDoubleClicked(mapToScene(e->pos()), e->button(), e->modifiers());
}

void ImageCanvas::keyPressEvent(QKeyEvent* e) {
    QGraphicsView::keyPressEvent(e);
    emit keyPressed(e);
}

void ImageCanvas::keyReleaseEvent(QKeyEvent* e) {
    QGraphicsView::keyReleaseEvent(e);
    emit keyReleased(e);
}

// ===== P0-4.3: selection rendering =====

void ImageCanvas::setSelectionModel(selection::SelectionModel* sel)
{
    if (m_sel == sel) return;
    if (m_sel) {
        disconnect(m_sel, nullptr, this, nullptr);
    }
    m_sel = sel;
    if (m_sel) {
        connect(m_sel, &selection::SelectionModel::changed,
                this, [this]() {
            if (m_ants) {
                if (m_sel->isEmpty()) m_ants->stop();
                else                  m_ants->start();
            }
            viewport()->update();
        });
        // initial sync
        if (m_ants) {
            if (m_sel->isEmpty()) m_ants->stop();
            else                  m_ants->start();
        }
        viewport()->update();
    }
    LOG_DEBUG("[ImageCanvas] setSelectionModel: sel={}",
              sel ? "yes" : "null");
}

void ImageCanvas::drawForeground(QPainter* p, const QRectF& /*rect*/)
{
    if (!m_sel || m_sel->isEmpty()) return;
    const QImage mask = m_sel->mask();
    if (mask.isNull()) return;

    // Trace mask boundary by scanning for mask pixels, then draw QPainterPath
    // that approximates the mask shape. For P0, use mask's bounding rect +
    // scanline outline (cheap, OK for typical selections).
    const QRect bbox = m_sel->boundingRect();
    if (bbox.isEmpty()) return;

    // Build outline: scan each row, find runs of selected pixels, draw rects
    //   For typical PS-style "rect + lasso" selections, this gives clean outline
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);   // crisp dashes
    const qreal phaseOffset = m_ants ? (m_ants->phase() * 0.5) : 0.0;
    QPen penBlack(Qt::black, 1.0, Qt::DashLine);
    penBlack.setDashOffset(phaseOffset);
    penBlack.setCosmetic(true);   // 1px regardless of zoom
    p->setPen(penBlack);
    // Map image -> scene (1:1 since pixmap not transformed independently)
    QPointF origin = m_item ? m_item->scenePos() : QPointF(0, 0);
    p->drawRect(QRectF(origin + QPointF(bbox.x(), bbox.y()),
                       QSizeF(bbox.width(), bbox.height())));
    p->restore();
}
