#include "LiquifyCanvas.h"

#include "LiquifyBackingStore.h"
#include "LiquifyEngine.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

namespace filters::liquify {

LiquifyCanvas::LiquifyCanvas(LiquifyEngine* engine, QWidget* parent)
    : QWidget(parent), m_engine(engine) {
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void LiquifyCanvas::setShowMesh(bool show) {
    if (m_showMesh == show) return;
    m_showMesh = show;
    update();
}

void LiquifyCanvas::setShowFrozen(bool show) {
    if (m_showFrozen == show) return;
    m_showFrozen = show;
    update();
}

void LiquifyCanvas::rebuildCache() {
    if (!m_engine || !m_engine->store()) {
        m_cache = QImage();
        update();
        return;
    }
    m_cache = m_engine->renderFull();
    update();
}

QPointF LiquifyCanvas::widgetToImage(const QPointF& p) const {
    // 1:1 mapping: widget pixels == image pixels.
    return p;
}

void LiquifyCanvas::stampAt(const QPointF& imagePos, const QPointF& direction) {
    if (!m_engine || !m_engine->store()) return;

    // Brush defaults: caller (panel) feeds in size + pressure via stampExternal.
    // For direct mouse stamps we use a sensible default until the panel
    // routes through stampExternal with full options.
    constexpr qreal kDefaultRadius = 50.0;
    constexpr qreal kDefaultPressure = 0.5;
    m_engine->applyToolStamp(imagePos, kDefaultRadius, kDefaultPressure,
                             direction, LiquifyToolMode::ForwardWarp);
    QRect dirty;
    const QImage piece = m_engine->renderDirty(dirty);
    if (piece.isNull()) return;
    if (m_cache.isNull()) {
        m_cache = m_engine->renderFull();
    } else {
        QPainter painter(&m_cache);
        painter.drawImage(dirty.topLeft(), piece);
    }
    update();
}

void LiquifyCanvas::stampExternal(const QPointF& center, qreal radius,
                                  qreal pressure, const QPointF& direction,
                                  LiquifyToolMode mode) {
    if (!m_engine || !m_engine->store()) return;
    m_engine->applyToolStamp(center, radius, pressure, direction, mode);
    QRect dirty;
    const QImage piece = m_engine->renderDirty(dirty);
    if (piece.isNull()) return;
    if (m_cache.isNull()) {
        m_cache = m_engine->renderFull();
    } else {
        QPainter painter(&m_cache);
        painter.drawImage(dirty.topLeft(), piece);
    }
    update();
}

void LiquifyCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_painting = true;
    m_lastPos = widgetToImage(e->position());
    stampAt(m_lastPos, QPointF(0, 0));
}

void LiquifyCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!m_painting) return;
    const QPointF pos = widgetToImage(e->position());
    const QPointF dir = pos - m_lastPos;
    stampAt(pos, dir);
    m_lastPos = pos;
}

void LiquifyCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_painting = false;
}

void LiquifyCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // No re-layout of cache; we render at native image size. The widget
    // paints the cache at top-left, clipped to widget rect.
}

void LiquifyCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::darkGray);
    if (!m_cache.isNull()) {
        p.drawImage(0, 0, m_cache);
    }
    if (m_showMesh || m_showFrozen) {
        paintOverlay(p);
    }
}

void LiquifyCanvas::paintOverlay(QPainter& p) {
    if (!m_engine || !m_engine->store()) return;
    const auto* store = m_engine->store();
    const auto& mesh = store->mesh();

    if (m_showMesh) {
        QPen pen(Qt::cyan, 1, Qt::SolidLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // Draw the deformed mesh as line segments along each triangle's
        // edges plus the mesh vertices in their displaced positions.
        for (int i = 0; i < mesh.triangleCount(); ++i) {
            const int v0 = mesh.triangleVertex(i, 0);
            const int v1 = mesh.triangleVertex(i, 1);
            const int v2 = mesh.triangleVertex(i, 2);
            const QPointF p0 = mesh.vertexPositionAt(v0) + mesh.vertexDisplacementAt(v0);
            const QPointF p1 = mesh.vertexPositionAt(v1) + mesh.vertexDisplacementAt(v1);
            const QPointF p2 = mesh.vertexPositionAt(v2) + mesh.vertexDisplacementAt(v2);
            p.drawLine(p0, p1);
            p.drawLine(p1, p2);
            p.drawLine(p2, p0);
        }
    }

    if (m_showFrozen) {
        const QImage& mask = store->freezeMask();
        if (!mask.isNull()) {
            QImage overlay(mask.size(), QImage::Format_ARGB32);
            overlay.fill(Qt::transparent);
            for (int y = 0; y < mask.height(); ++y) {
                for (int x = 0; x < mask.width(); ++x) {
                    const int v = mask.pixelColor(x, y).red();
                    if (v > 0) {
                        overlay.setPixelColor(x, y,
                                              QColor(0, 200, 255, v / 2));
                    }
                }
            }
            p.drawImage(0, 0, overlay);
        }
    }
}

}  // namespace filters::liquify
