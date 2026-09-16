#include "LiquifyEngine.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace filters::liquify {

QColor LiquifyEngine::bilinearSample(const QImage& img, qreal x, qreal y) {
    if (img.isNull()) return QColor();
    const int w = img.width();
    const int h = img.height();
    if (w == 0 || h == 0) return QColor();

    // Clamp sampling coordinates to the image.
    x = std::clamp(x, qreal(0.0), qreal(w - 1));
    y = std::clamp(y, qreal(0.0), qreal(h - 1));

    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const qreal fx = x - x0;
    const qreal fy = y - y0;

    const QColor c00 = img.pixelColor(x0, y0);
    const QColor c10 = img.pixelColor(x1, y0);
    const QColor c01 = img.pixelColor(x0, y1);
    const QColor c11 = img.pixelColor(x1, y1);

    auto blend = [=](int q00, int q10, int q01, int q11) {
        const qreal top = q00 * (1.0 - fx) + q10 * fx;
        const qreal bot = q01 * (1.0 - fx) + q11 * fx;
        return int(std::lround(top * (1.0 - fy) + bot * fy));
    };

    return QColor(
        std::clamp(blend(c00.red(),   c10.red(),   c01.red(),   c11.red()),   0, 255),
        std::clamp(blend(c00.green(), c10.green(), c01.green(), c11.green()), 0, 255),
        std::clamp(blend(c00.blue(),  c10.blue(),  c01.blue(),  c11.blue()),  0, 255),
        std::clamp(blend(c00.alpha(), c10.alpha(), c01.alpha(), c11.alpha()), 0, 255)
    );
}

qreal LiquifyEngine::maxDisplacementMagnitude() const {
    if (!m_store) return 0.0;
    const auto& mesh = m_store->mesh();
    qreal maxMag = 0.0;
    for (int i = 0; i < mesh.rows(); ++i) {
        for (int j = 0; j < mesh.cols(); ++j) {
            const QPointF d = mesh.vertexDisplacement(i, j);
            const qreal m = std::hypot(d.x(), d.y());
            if (m > maxMag) maxMag = m;
        }
    }
    return maxMag;
}

QColor LiquifyEngine::samplePixel(qreal x, qreal y) const {
    if (!m_store || m_store->snapshot().isNull()) return QColor();
    if (m_store->isFrozen(int(x), int(y))) {
        return m_store->snapshot().pixelColor(int(x), int(y));
    }
    const QPointF src = m_store->mesh().inverseWarp(x, y);
    return bilinearSample(m_store->snapshot(), src.x(), src.y());
}

QImage LiquifyEngine::renderFull() const {
    if (!m_store || m_store->snapshot().isNull()) return QImage();
    const QImage& snapshot = m_store->snapshot();
    QImage result(snapshot.size(), snapshot.format());
    result.fill(Qt::transparent);

    const int w = snapshot.width();
    const int h = snapshot.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (m_store->isFrozen(x, y)) {
                result.setPixelColor(x, y, snapshot.pixelColor(x, y));
            } else {
                const QPointF src = m_store->mesh().inverseWarp(qreal(x), qreal(y));
                result.setPixelColor(x, y, bilinearSample(snapshot, src.x(), src.y()));
            }
        }
    }
    return result;
}

QImage LiquifyEngine::renderDirty(QRect& outDirtyRect) const {
    if (!m_store || m_store->snapshot().isNull()) return QImage();
    outDirtyRect = m_store->dirtyRect();
    if (outDirtyRect.isEmpty()) return QImage();

    const QImage& snapshot = m_store->snapshot();
    const qreal padding = std::max(maxDisplacementMagnitude(), qreal(2.0));
    const QRect grown = outDirtyRect.adjusted(
        int(-std::ceil(padding)),
        int(-std::ceil(padding)),
        int(std::ceil(padding)),
        int(std::ceil(padding))
    ).intersected(snapshot.rect());

    QImage result(grown.size(), snapshot.format());
    result.fill(Qt::transparent);

    for (int oy = 0; oy < grown.height(); ++oy) {
        const int y = grown.y() + oy;
        for (int ox = 0; ox < grown.width(); ++ox) {
            const int x = grown.x() + ox;
            if (m_store->isFrozen(x, y)) {
                result.setPixelColor(ox, oy, snapshot.pixelColor(x, y));
            } else {
                const QPointF src = m_store->mesh().inverseWarp(qreal(x), qreal(y));
                result.setPixelColor(ox, oy, bilinearSample(snapshot, src.x(), src.y()));
            }
        }
    }
    outDirtyRect = grown;
    return result;
}

void LiquifyEngine::applyToolStamp(const QPointF& center, qreal radius, qreal pressure,
                                   const QPointF& direction, LiquifyToolMode mode) {
    if (!m_store) return;
    if (radius <= 0.0 || pressure <= 0.0) return;

    m_store->mesh().applyTool(center, radius, pressure, direction, mode);

    // Grow dirty rect to cover the brush footprint plus the displacement
    // padding needed for inverse-warp sampling at the boundary.
    const qreal pad = std::max(maxDisplacementMagnitude(), qreal(2.0));
    const QRect affected(
        QPoint(int(std::floor(center.x() - radius - pad)),
               int(std::floor(center.y() - radius - pad))),
        QPoint(int(std::ceil (center.x() + radius + pad)),
               int(std::ceil (center.y() + radius + pad)))
    );
    m_store->markDirty(affected);
}

}  // namespace filters::liquify
