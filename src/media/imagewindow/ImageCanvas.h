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

    // P0-6.10 (2026-09-14): 自由变换 box (10 handle 渲染)
    //   m_box 是 weak ref, ImageWindow 持有所有权
    //   当 m_box != nullptr 时, drawForeground 画 8 handle + 1 center + 1 rotation handle
    void setTransformBox(transform::TransformBox* box);
    transform::TransformBox* transformBox() const { return m_box; }
    selection::SelectionModel* selectionModel() const { return m_sel; }
    selection::MarchingAnts*   marchingAnts()   const { return m_ants; }

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
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    // P0-4.3: paint selection mask border (marching ants) on top of scene
    void drawForeground(QPainter* p, const QRectF& rect) override;

private:
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
};
