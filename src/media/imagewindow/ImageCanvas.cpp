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
#include "../transform/TransformBox.h"
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
    // The base class stores the event here (QGraphicsViewPrivate::
    // storeMouseEvent) — unless it returned early because there is no
    // scene. See m_viewStoredMouseEvent in the header for why this has
    // to be tracked.
    m_viewStoredMouseEvent = (scene() != nullptr);
    emit sceneClicked(mapToScene(e->pos()), e->button(), e->modifiers());
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* e) {
    QGraphicsView::mouseMoveEvent(e);
    // Same as the press: the base stores the event on the way through.
    if (scene() != nullptr) m_viewStoredMouseEvent = true;
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (!m_viewStoredMouseEvent) {
        // Stray release: no press ever reached the view, so the base
        // class would read a QEventStorage that was never engaged —
        // Qt 6.11 asserts "m_engaged" and pops a debug error. Such a
        // release carries no meaning for the view either (there is no
        // drag, no click), so dropping it is the right thing, not just
        // the safe thing.
        e->accept();
        return;
    }
    m_viewStoredMouseEvent = false;
    QGraphicsView::mouseReleaseEvent(e);
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

// P0-6.10 (2026-09-14): 自由变换 box weak ref + viewport update
void ImageCanvas::setTransformBox(transform::TransformBox* box)
{
    m_box = box;
    if (m_box) {
        m_box->setRect(m_box->rect());   // 同步 4 角
    }
    viewport()->update();
}

// P2.1 (2026-09-22): mask overlay toggle (PS-style "Show Selection Overlay")
void ImageCanvas::setMaskOverlayVisible(bool v)
{
    if (m_showMaskOverlay == v) return;
    m_showMaskOverlay = v;
    LOG_INFO("[ImageCanvas] setMaskOverlayVisible: {}", v ? "ON" : "off");
    viewport()->update();
}

// ---------------------------------------------------------------------------
// P7.11 (2026-10-15): dirty-rect repaint API.
//
// invalidateRect(sceneRect) accumulates a scene-space rectangle. All
// calls before the next paint event are unioned. On the next paint
// event (we hook drawForeground, the last hook before the viewport
// commits), flushDirtyRect() is called and a viewport update is
// requested for the unioned area only. invalidateAll() reverts to a
// full-viewport update (used by setImage / setZoom / setTransformBox).
//
// This is incremental: existing internal callers still use
// viewport()->update() directly. New code (and P7.11 internal
// additions below) should use invalidateRect() for fine-grained
// updates. Future P8.1 incremental compositor hooks can accumulate
// across frames.
// ---------------------------------------------------------------------------
void ImageCanvas::invalidateRect(const QRectF& sceneRect)
{
    if (m_dirtyAll) return;   // already marked whole-viewport
    if (!sceneRect.isValid()) return;
    m_pendingDirtyRect = m_pendingDirtyRect.isNull()
        ? sceneRect
        : m_pendingDirtyRect.united(sceneRect);
    // Defer the actual viewport()->update() call to flushDirtyRect()
    // (called by drawForeground, the last hook before the viewport
    // commits). This way, multiple invalidateRect() calls between two
    // paint events collapse into a single viewport update.
}

void ImageCanvas::invalidateAll()
{
    m_dirtyAll = true;
    m_pendingDirtyRect = QRectF();
    // Schedule a full repaint (cheaper than tracking an "all" flag
    // through the paint pipeline).
    viewport()->update();
}

void ImageCanvas::flushDirtyRect()
{
    if (m_dirtyAll) {
        // invalidateAll() already called viewport()->update(); nothing
        // to do here, just clear the flag for the next round.
        m_dirtyAll = false;
        return;
    }
    if (m_pendingDirtyRect.isNull()) return;
    QRect viewportRect = mapFromScene(m_pendingDirtyRect).boundingRect()
                             .intersected(viewport()->rect());
    if (!viewportRect.isEmpty()) {
        viewport()->update(viewportRect);
    }
    m_pendingDirtyRect = QRectF();
}

void ImageCanvas::drawForeground(QPainter* p, const QRectF& /*rect*/)
{
    // P7.11 (2026-10-15): commit any pending dirty-rect union right
    // before the viewport commits the frame. drawForeground is the
    // last paint hook QGraphicsView invokes before flushing, so this
    // is the right place to schedule the accumulated update.
    flushDirtyRect();

    if (!m_sel || m_sel->isEmpty()) return;
    const QImage mask = m_sel->mask();
    if (mask.isNull()) return;

    // Trace mask boundary by scanning for mask pixels, then draw QPainterPath
    // that approximates the mask shape. For P0, use mask's bounding rect +
    // scanline outline (cheap, OK for typical selections).
    const QRect bbox = m_sel->boundingRect();
    if (bbox.isEmpty()) return;

    // Map image -> scene (1:1 since pixmap not transformed independently)
    QPointF origin = m_item ? m_item->scenePos() : QPointF(0, 0);
    QRectF sceneBbox(origin + QPointF(bbox.x(), bbox.y()),
                     QSizeF(bbox.width(), bbox.height()));

    // P2.1 (2026-09-22): mask overlay — fill the selected region with 30% blue
    //   tint so users can see exactly what is selected without obscuring the
    //   image. Default off (PS default: marching ants only). Toggle via
    //   setMaskOverlayVisible() (called from menu / keyboard shortcut).
    if (m_showMaskOverlay) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        p->setCompositionMode(QPainter::CompositionMode_SourceAtop);
        // Build a QImage of mask region tinted blue, then draw
        //   PS uses 50% blue overlay (RGB 0, 0, 255) — we use 76/255 alpha for visibility
        QImage tintImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
        tintImage.fill(QColor(0, 120, 255, 76));
        // Apply mask alpha to tint: where mask=0, tint=transparent
        QImage maskedTint(mask.size(), QImage::Format_ARGB32_Premultiplied);
        maskedTint.fill(Qt::transparent);
        for (int y = 0; y < mask.height(); ++y) {
            const uchar* mrow = mask.constScanLine(y);
            QRgb* drow = reinterpret_cast<QRgb*>(maskedTint.scanLine(y));
            for (int x = 0; x < mask.width(); ++x) {
                const int a = mrow[x];
                if (a == 0) continue;
                const int blue = 76 * a / 255;
                drow[x] = qRgba(0, 120 * a / 255, 255 * a / 255, blue);
            }
        }
        p->drawImage(sceneBbox.topLeft(), maskedTint);
        p->restore();
    }

    // Build outline: scan each row, find runs of selected pixels, draw rects
    //   For typical PS-style "rect + lasso" selections, this gives clean outline
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);   // crisp dashes
    const qreal phaseOffset = m_ants ? (m_ants->phase() * 0.5) : 0.0;
    QPen penBlack(Qt::black, 1.0, Qt::DashLine);
    penBlack.setDashOffset(phaseOffset);
    penBlack.setCosmetic(true);   // 1px regardless of zoom
    p->setPen(penBlack);
    p->drawRect(sceneBbox);
    p->restore();

    // P0-6.10 (2026-09-14): 自由变换 box 渲染 — 10 handle + 1 rotation line
    //   m_box != nullptr 时画 (m_box 由 ImageWindow 持有, m_canvas 仅 weak ref)
    if (m_box) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::TextAntialiasing, true);
        p->setPen(QPen(Qt::white, 1.0));
        p->setBrush(Qt::white);

        // 8 handle + 1 center: 10x10 白色实心方块, 1px 黑色边框 (cosmetic)
        const QPointF itemOrigin = m_item ? m_item->scenePos() : QPointF(0, 0);
        const QRectF box = m_box->rect();
        // 8 handle 位置 (scene 坐标)
        using Handle = transform::TransformBox::Handle;
        const Handle cornersAndEdges[] = {
            Handle::TopLeft, Handle::Top, Handle::TopRight,
            Handle::Right, Handle::BottomRight, Handle::Bottom,
            Handle::BottomLeft, Handle::Left, Handle::Center
        };
        for (Handle h : cornersAndEdges) {
            QPointF hp = m_box->handlePos(h);
            QRectF r(hp.x() - 5, hp.y() - 5, 10, 10);
            p->drawRect(r);
        }
        // Rotation handle: 1px 白色线从 Top 中点到 Rotation 手柄 (cos 描)
        const QPointF topMid = m_box->handlePos(Handle::Top);
        const QPointF rotPos = m_box->handlePos(Handle::Rotation);
        p->setPen(QPen(Qt::white, 1.0));
        p->drawLine(topMid, rotPos);
        // Rotation handle: 白色圆 + 1px 黑色边框
        p->setBrush(Qt::white);
        p->setPen(QPen(Qt::black, 1.0));
        p->drawEllipse(QRectF(rotPos.x() - 5, rotPos.y() - 5, 10, 10));

        p->restore();
    }
}
