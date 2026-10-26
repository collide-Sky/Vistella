// SPDX-License-Identifier: MIT
//
// ImageCanvas — QGraphicsView 派生,接管 scene/zoom/wheel/eventFilter
// (P0-1: 拆 imagewindow.cpp 上帝类的 6 大组件之一)
//
// P0-4.3 (2026-09-10): selection rendering
//   - Owns MarchingAnts (200ms phase ticker)
//   - drawForeground() paints selection mask border as 8px black/white dashed
//     line, offset by m_ants->phase() * 0.5px
//   - setSelectionModel(SelectionModel*) wires changed -> update() + ants start/stop
//
#pragma once

#include <QGraphicsView>
#include <QPointer>
#include <QPointF>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsEllipseItem;
class QGraphicsSceneMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QFocusEvent;
class QPainter;
class ImageWindow;

namespace selection { class SelectionModel; class MarchingAnts; }
namespace transform { class TransformBox; }

class ImageCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget* parent = nullptr);
    ~ImageCanvas() override;

    // Accessors for ImageWindow to wire the rest of the pipeline.
    QGraphicsScene*      scene()   const { return m_scene; }
    QGraphicsPixmapItem* pixmapItem() const { return m_item; }

    // Current zoom factor (1.0 = 100%).
    double zoom() const { return m_zoom; }
    void   setZoom(double z);

    // Hit-test a scene point against the registered handles. Used by
    // eventFilter for MosaicTool / TextOverlayController.
    enum class HitResult { None, MosaicBrush, TextHandle };
    HitResult hitTest(const QPointF& scenePos) const;

    // Brush cursor circle (MosaicTool owns visibility, but the QGraphicsItem
    // lives on our scene).
    void setBrushCursorRadius(int r);
    void moveBrushCursor(const QPointF& scenePos, bool visible);
    void hideBrushCursor();

    // P0-4.3: selection rendering
    void setSelectionModel(selection::SelectionModel* sel);

    // P2.1 (2026-09-22): mask overlay visibility (PS-style "Show Selection Overlay")
    //   When true, drawForeground fills the mask region with 30% blue tint
    //   so users can see exactly what is selected without obscuring the image.
    //   Default off (PS default: "marching ants only").
    void setMaskOverlayVisible(bool v);
    bool isMaskOverlayVisible() const { return m_showMaskOverlay; }

    // P0-6.10 (2026-09-14): 自由变换 box (10 handle 渲染)
    //   m_box 是 weak ref, ImageWindow 持有所有权
    //   当 m_box != nullptr 时, drawForeground 画 8 handle + 1 center + 1 rotation handle
    void setTransformBox(transform::TransformBox* box);
    transform::TransformBox* transformBox() const { return m_box; }
    selection::SelectionModel* selectionModel() const { return m_sel; }
    selection::MarchingAnts*   marchingAnts()   const { return m_ants; }

    // P7.11 (2026-10-15): dirty-rect repaint.
    //   invalidateRect(sceneRect) accumulates a scene-space rect and asks
    //   the viewport to repaint only the corresponding area. Subsequent
    //   calls before the next paint event are unioned. invalidateAll()
    //   reverts to a full-viewport repaint (e.g. after zoom or setImage).
    //   Without this API, internal callers fall back to viewport()->update()
    //   (whole viewport), which is fine for small canvases but wasteful
    //   for 50MP+ images where most of the viewport is unchanged.
    void invalidateRect(const QRectF& sceneRect);
    void invalidateAll();

    // P7.11 (2026-10-15): test hooks. Exposed only for unit tests; do
    // not call from production code.
    QRectF testAccDirtyRect()       const { return m_pendingDirtyRect; }
    bool   testAccDirtyRect_isNull() const { return m_pendingDirtyRect.isNull(); }
    bool   testDirtyAll()           const { return m_dirtyAll; }
    void   testFlushDirtyRect()           { flushDirtyRect(); }

    // Wire a back-pointer to ImageWindow for forwarders (zoom, fit, etc.).
    // Raw pointer (not QPointer) so this header doesn't need imagewindow.h's
    // full definition; ImageWindow is the owner of this component and
    // always outlives it.
    void setHost(ImageWindow* w) { m_host = w; }

public slots:
    void onZoomIn();
    void onZoomOut();
    void onFitWindow();
    void onResetZoom();

signals:
    void zoomChanged(double z);
    void wheelZoomed(int delta);
    void sceneClicked(const QPointF& scenePos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void sceneDoubleClicked(const QPointF& scenePos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void keyPressed(QKeyEvent* e);
    void keyReleased(QKeyEvent* e);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    // P0-4.3: paint selection mask border (marching ants) on top of scene
    void drawForeground(QPainter* p, const QRectF& rect) override;

private:
    // P8.3.1 (2026-10-26): true once QGraphicsView has actually stored a
    //   mouse event of its own.
    //
    //   QGraphicsView keeps "the last mouse event" in a QEventStorage, and
    //   only fills it when a press or a move reaches the base class
    //   (QGraphicsViewPrivate::storeMouseEvent). But
    //   QGraphicsView::mouseReleaseEvent reads that storage UNCONDITIONALLY,
    //   to clear the viewport cursor — and QGraphicsViewPrivate::
    //   _q_unsetViewportCursor() dereferences it without checking that it
    //   was ever engaged. A release with no matching press therefore trips
    //   Qt 6.11's `ASSERT: "m_engaged"` in gui/kernel/qevent_p.h.
    //
    //   Such a release is not exotic: press the mouse on the home page's
    //   recent-files list, the workspace opens under the cursor, and the
    //   release lands on a canvas that never saw a press.
    //
    //   The flag is set from the same place the base class stores the
    //   event, so it tracks the storage exactly rather than guessing. The
    //   one case where the base returns before storing is a null scene,
    //   which is why the assignment is guarded on scene().
    bool m_viewStoredMouseEvent = false;

    // Re-apply m_zoom to the QGraphicsView transform (resetTransform + scale).
    // Called by setZoom / onZoomIn/Out/ResetZoom / wheelEvent after m_zoom changes.
    void applyTransform();

    ImageWindow*               m_host = nullptr;
    QGraphicsScene*            m_scene   = nullptr;
    QGraphicsPixmapItem*       m_item    = nullptr;
    QGraphicsEllipseItem*      m_brushCursor = nullptr;
    double                     m_zoom    = 1.0;

    // P0-4.3: selection rendering
    selection::SelectionModel* m_sel  = nullptr;     // weak ref
    selection::MarchingAnts*   m_ants = nullptr;     // owned
    // P0-6.10: 自由变换 box (weak ref, ImageWindow::m_box 持所有权)
    transform::TransformBox*   m_box  = nullptr;

    // P2.1 (2026-09-22): mask overlay visibility (PS-style "Show Selection Overlay"
    //   default off; flip on via menu / keyboard shortcut). When enabled,
    //   drawForeground fills the inverse mask region with 30% blue tint.
    bool                       m_showMaskOverlay = false;

    // P7.11 (2026-10-15): dirty-rect accumulator (scene space). QRectF()
    // means "no pending update"; a non-empty rect is unioned across
    // invalidateRect() calls until the next paint event consumes it via
    // flushDirtyRect(). m_dirtyAll is set by invalidateAll() to force a
    // full viewport update on the next flush.
    QRectF m_pendingDirtyRect;
    bool   m_dirtyAll = false;

    // P7.11: schedule a viewport update restricted to the union of all
    // invalidateRect() calls since the last paint. Resets m_pendingDirtyRect
    // and m_dirtyAll.
    void flushDirtyRect();
};
