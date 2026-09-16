#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include "LiquifyMesh.h"

namespace filters::liquify {

class LiquifyEngine;

// Interactive preview canvas for the Liquify dialog.
//
// Owns a cached rendered image. Mouse press / move / release events on the
// canvas translate to tool stamps applied through the engine; after each
// stamp the affected region is re-rendered and the canvas repaints.
//
// Coordinate translation: image-space pixels map 1:1 to widget-space pixels.
// The canvas paints the image at its natural size, top-left aligned.
class LiquifyCanvas : public QWidget {
    Q_OBJECT
public:
    explicit LiquifyCanvas(LiquifyEngine* engine, QWidget* parent = nullptr);

    void setShowMesh(bool show);
    void setShowFrozen(bool show);

    // Force a full re-render of the cached image.
    void rebuildCache();

    // Apply a paint stroke from the outside (e.g. dialog forwards a freeze
    // mask brush stroke). Pass image-space points.
    void stampExternal(const QPointF& center, qreal radius, qreal pressure,
                       const QPointF& direction, LiquifyToolMode mode);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QPointF widgetToImage(const QPointF& p) const;
    void stampAt(const QPointF& imagePos, const QPointF& direction);
    void paintOverlay(QPainter& p);

    LiquifyEngine* m_engine = nullptr;
    QImage m_cache;        // current rendered image, full size
    bool m_showMesh = false;
    bool m_showFrozen = false;
    bool m_painting = false;
    QPointF m_lastPos;     // last image-space stroke point
};

}  // namespace filters::liquify
