// SPDX-License-Identifier: MIT
//
// SelectionStrategy implementations - P0-4.2 (2026-09-10)
//
#include "SelectionStrategy.h"
#include "../masks/ColorRange.h"
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

    // P2.1 (2026-09-22): paint path with optional anti-alias + feathering
    QPainterPath path;
    path.addPolygon(m_path);
    path.closeSubpath();
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, m_antiAlias);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 255));
        painter.drawPath(path);
        painter.end();
    }

    // P2.1 (2026-09-22): feathering — box blur on alpha channel then threshold
    //   feather=0 means crisp mask (current PS default); feather>0 softens edge
    //   by blurring the alpha channel within [r] pixels then re-thresholding at 127.
    if (m_featherRadius > 0 && !mask.isNull()) {
        const int R = m_featherRadius;
        const int W = mask.width();
        const int H = mask.height();
        // Build integral image for fast box blur (constant time per pixel)
        //   sum[y][x] = sum of mask bytes in [0..y) x [0..x)
        std::vector<int> sum((W + 1) * (H + 1), 0);
        for (int y = 0; y < H; ++y) {
            const uchar* row = mask.constScanLine(y);
            int rowSum = 0;
            for (int x = 0; x < W; ++x) {
                rowSum += row[x];
                sum[(y + 1) * (W + 1) + (x + 1)] = sum[y * (W + 1) + (x + 1)] + rowSum;
            }
        }
        // Apply (2R+1) box blur (horizontal+vertical separable but integral makes it O(1))
        QImage blurred(W, H, QImage::Format_Alpha8);
        blurred.fill(0);
        uchar* dst = blurred.bits();
        const int dstStride = blurred.bytesPerLine();
        for (int y = 0; y < H; ++y) {
            const int ya = std::max(0, y - R);
            const int yb = std::min(H - 1, y + R);
            for (int x = 0; x < W; ++x) {
                const int xa = std::max(0, x - R);
                const int xb = std::min(W - 1, x + R);
                // sum over [ya..yb] x [xa..xb]
                const int a = sum[ya * (W + 1) + xa];
                const int b = sum[ya * (W + 1) + (xb + 1)];
                const int c = sum[(yb + 1) * (W + 1) + xa];
                const int d = sum[(yb + 1) * (W + 1) + (xb + 1)];
                const int area = (xb - xa + 1) * (yb - ya + 1);
                const int avg = (d - b - c + a) / area;
                dst[y * dstStride + x] = static_cast<uchar>(avg);
            }
        }
        // Threshold: any pixel > 127 stays (preserves shape); softened edge stays partial alpha
        //   PS feathering keeps partial-alpha; we keep the partial-alpha too
        mask = std::move(blurred);
    }

    LOG_DEBUG("[Lasso] mask: path points={} feather={} aa={}",
              int(m_path.size()), m_featherRadius, m_antiAlias);
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

    QImage mask(W, H, QImage::Format_Alpha8);
    mask.fill(0);
    uchar* mbits = mask.bits();
    const int mstride = mask.bytesPerLine();

    // Sample seed color (gray value, 0..255)
    const int seedGray = qGray(image.pixel(sx, sy));
    const int tol2 = m_tolerance * m_tolerance;     // squared distance

    if (m_contiguous) {
        // P0-4.5: BFS flood-fill 4-neighborhood, only connected same-color region
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
        LOG_DEBUG("[MagicWand] mask: contiguous seed=({},{}) tol={} pixels={}",
                  sx, sy, m_tolerance, count);
    } else {
        // P2.1 (2026-09-22): non-contiguous — full image scan within tolerance
        //   Selects all pixels matching the seed color anywhere in the image,
        //   not just the connected region. PS Equivalent of unchecking Contiguous.
        int count = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int g = qGray(image.pixel(x, y));
                const int dr = g - seedGray;
                if (dr * dr > tol2) continue;
                mbits[y * mstride + x] = 255;
                ++count;
            }
        }
        LOG_DEBUG("[MagicWand] mask: non-contiguous seed=({},{}) tol={} pixels={}",
                  sx, sy, m_tolerance, count);
    }
    return mask;
}

void MagicWandSelectionStrategy::cancel()
{
    m_active = false;
    m_seed = QPointF();
}

// ===== ColorRange (P2.4 2026-09-22) =====
//
// PS-style Color Range selection: user drops 1+ sample points, mask = pixels
//   within fuzziness (HSV distance) of any sample. Implemented via
//   masks::ColorRange::computeMask (P1.3.7 already covers HSV distance math).
//
// Strategy lifecycle:
//   - begin(p)   records first sample point
//   - update(p)  appends additional sample points (PS-style — multi-sample)
//   - end(image) runs computeMask and returns Format_Alpha8 QImage
//
// SelectionModel::setMask compositing (Replace/Add/Subtract/Intersect) is
// applied by the caller (tool layer) — same as MagicWand/Lasso.
//
void ColorRangeSelectionStrategy::setSamplePoints(const QVector<QPointF>& pts)
{
    m_samplePoints = pts;
}

void ColorRangeSelectionStrategy::begin(const QPointF& p)
{
    m_samplePoints.clear();
    m_samplePoints.append(p);
}

void ColorRangeSelectionStrategy::update(const QPointF& p)
{
    // PS allows multi-sample: each click adds a new sample point.
    // Skip if last point is identical (avoid duplicates from same click event).
    if (!m_samplePoints.isEmpty() && m_samplePoints.last() == p) return;
    m_samplePoints.append(p);
}

void ColorRangeSelectionStrategy::cancel()
{
    m_samplePoints.clear();
}

QImage ColorRangeSelectionStrategy::end(const QImage& image)
{
    if (image.isNull()) return QImage();
    if (m_samplePoints.isEmpty()) return QImage();

    // Build ColorRangeParams from current state.
    masks::ColorRangeParams params;
    params.fuzziness = m_fuzziness;
    params.invert    = m_invert;
    params.samplePoints.reserve(m_samplePoints.size());
    for (const QPointF& p : m_samplePoints) {
        params.samplePoints.append(QPoint(int(p.x()), int(p.y())));
    }

    // Delegate HSV-distance computation to masks::ColorRange (P1.3.7).
    const cv::Mat cvMask = masks::ColorRange::computeMask(image, params);
    if (cvMask.empty()) return QImage();

    // Convert cv::Mat (CV_8UC1) -> QImage (Format_Alpha8).
    // masks::ColorRange guarantees same size as input image.
    QImage mask(cvMask.cols, cvMask.rows, QImage::Format_Alpha8);
    if (mask.size() != image.size()) {
        // size mismatch — should not happen, but guard
        LOG_WARN("[ColorRange] mask size {}x{} != image {}x{}",
                 mask.width(), mask.height(), image.width(), image.height());
        return QImage();
    }
    uchar* dst = mask.bits();
    const int dstStride = mask.bytesPerLine();
    const uchar* src = cvMask.data;
    const int srcStride = static_cast<int>(cvMask.step);
    for (int y = 0; y < cvMask.rows; ++y) {
        std::memcpy(dst + y * dstStride, src + y * srcStride, cvMask.cols);
    }

    LOG_DEBUG("[ColorRange] mask {}x{}, fuzziness={}, invert={}, samples={}",
              mask.width(), mask.height(), m_fuzziness, m_invert,
              int(m_samplePoints.size()));
    return mask;
}

} // namespace selection
