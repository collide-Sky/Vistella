// SPDX-License-Identifier: MIT
//
// SelectionStrategy implementations - P0-4.2 (2026-09-10)
//
#include "SelectionStrategy.h"
#include "logger.h"

#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <algorithm>
#include <cstring>
#include <queue>

namespace selection {

// ===== Rect =====
void RectSelectionStrategy::begin(const QPointF& p)
{
    m_active = true;
    m_p0 = p;
    m_p1 = p;
}

void RectSelectionStrategy::update(const QPointF& p)
{
    if (m_active) m_p1 = p;
}

QImage RectSelectionStrategy::end(const QImage& image)
{
    if (!m_active) return QImage();
    m_active = false;
    if (image.isNull()) return QImage();
    QImage mask(image.size(), QImage::Format_Alpha8);
    mask.fill(0);
    QRect r(
        QPoint(int(std::min(m_p0.x(), m_p1.x())), int(std::min(m_p0.y(), m_p1.y()))),
        QPoint(int(std::max(m_p0.x(), m_p1.x())), int(std::max(m_p0.y(), m_p1.y())))
    );
    r = r.normalized();
    r &= QRect(QPoint(0, 0), image.size());
    if (r.isEmpty()) return mask;
    QPainter painter(&mask);
    painter.fillRect(r, QColor(255, 255, 255, 255));
    painter.end();
    LOG_DEBUG("[Rect] mask: bbox={}x{}", r.width(), r.height());
    return mask;
}

void RectSelectionStrategy::cancel()
{
    m_active = false;
    m_p0 = m_p1 = QPointF();
}

// ===== Lasso =====
void LassoSelectionStrategy::begin(const QPointF& p)
{
    m_active = true;
    m_path.clear();
    m_path << p;
}

void LassoSelectionStrategy::update(const QPointF& p)
{
    if (!m_active) return;
    // skip if last point is very close (avoid huge polygon)
    if (!m_path.isEmpty()) {
        QPointF last = m_path.last();
        const qreal dx = last.x() - p.x();
        const qreal dy = last.y() - p.y();
        if (dx*dx + dy*dy < 4.0) return;     // <2px skip
    }
    m_path << p;
}

QImage LassoSelectionStrategy::end(const QImage& image)
{
    if (!m_active) return QImage();
    m_active = false;
    if (image.isNull() || m_path.size() < 3) return QImage();
    QImage mask(image.size(), QImage::Format_Alpha8);
    mask.fill(0);
    // Use QPainterPath to test each pixel (slow but simple, OK for P0 stage)
    QPainterPath path;
    path.addPolygon(m_path);
    path.closeSubpath();
    QPainter painter(&mask);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 255));
    painter.drawPath(path);
    painter.end();
    LOG_DEBUG("[Lasso] mask: path points={}", int(m_path.size()));
    return mask;
}

void LassoSelectionStrategy::cancel()
{
    m_active = false;
    m_path.clear();
}

// ===== MagicWand (flood fill, BFS) =====
void MagicWandSelectionStrategy::begin(const QPointF& p)
{
    m_active = true;
    m_seed = p;
}

void MagicWandSelectionStrategy::update(const QPointF&)
{
    // No-op during drag
}

QImage MagicWandSelectionStrategy::end(const QImage& image)
{
    if (!m_active) return QImage();
    m_active = false;
    if (image.isNull()) return QImage();
    const int W = image.width();
    const int H = image.height();
    if (W == 0 || H == 0) return QImage();
    const int sx = int(m_seed.x());
    const int sy = int(m_seed.y());
    if (sx < 0 || sx >= W || sy < 0 || sy >= H) return QImage();

    // Use the alpha channel of QImage::Format_ARGB32_Premultiplied; for other
    // formats, fall back to grayscale via QImage::pixel()
    QImage mask(W, H, QImage::Format_Alpha8);
    mask.fill(0);
    uchar* mbits = mask.bits();
    const int mstride = mask.bytesPerLine();

    // Sample seed color (gray value, 0..255)
    const int seedGray = qGray(image.pixel(sx, sy));
    const int tol2 = m_tolerance * m_tolerance;     // squared distance

    // BFS
    std::vector<bool> visited(W * H, false);
    std::queue<QPoint> q;
    q.push(QPoint(sx, sy));
    visited[sy * W + sx] = true;
    int count = 0;
    while (!q.empty()) {
        QPoint pt = q.front();
        q.pop();
        const int x = pt.x();
        const int y = pt.y();
        const int g = qGray(image.pixel(x, y));
        const int dr = g - seedGray;
        if (dr * dr > tol2) continue;     // outside tolerance
        mbits[y * mstride + x] = 255;
        ++count;
        // 4-neighborhood BFS
        const int dx[4] = { -1, 1, 0, 0 };
        const int dy[4] = { 0, 0, -1, 1 };
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k];
            const int ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            if (visited[ny * W + nx]) continue;
            visited[ny * W + nx] = true;
            q.push(QPoint(nx, ny));
        }
    }
    LOG_DEBUG("[MagicWand] mask: seed=({},{}) tol={} pixels={}", sx, sy, m_tolerance, count);
    return mask;
}

void MagicWandSelectionStrategy::cancel()
{
    m_active = false;
    m_seed = QPointF();
}

// ===== ColorRange (P1 stub) =====
QImage ColorRangeSelectionStrategy::end(const QImage& image)
{
    // P1: implement via HSV range selection
    return QImage();
}

} // namespace selection
