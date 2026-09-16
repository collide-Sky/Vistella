#include "LiquifyMesh.h"

#include <QtMath>
#include <cmath>

namespace filters::liquify {

void LiquifyMesh::build(int rows, int cols, const QRectF& bounds) {
    m_rows = rows;
    m_cols = cols;
    m_bounds = bounds;
    m_vertices.clear();
    m_triangles.clear();

    if (rows < 2 || cols < 2) {
        // A mesh with fewer than 2 rows or cols has no triangles.
        // Leave empty; callers should validate.
        return;
    }

    m_vertices.reserve(rows * cols);
    for (int i = 0; i < rows; ++i) {
        const qreal ty = (rows == 1) ? 0.0
                                     : qreal(i) / qreal(rows - 1);
        const qreal y = bounds.y() + ty * bounds.height();
        for (int j = 0; j < cols; ++j) {
            const qreal tx = (cols == 1) ? 0.0
                                         : qreal(j) / qreal(cols - 1);
            const qreal x = bounds.x() + tx * bounds.width();
            m_vertices.append({QPointF(x, y), QPointF(0.0, 0.0)});
        }
    }

    m_triangles.reserve((rows - 1) * (cols - 1) * 6);
    for (int i = 0; i < rows - 1; ++i) {
        for (int j = 0; j < cols - 1; ++j) {
            const int tl = i * cols + j;
            const int tr = i * cols + (j + 1);
            const int bl = (i + 1) * cols + j;
            const int br = (i + 1) * cols + (j + 1);
            // Diagonal split from top-left to bottom-right.
            m_triangles << tl << tr << br;  // upper-right triangle
            m_triangles << tl << br << bl;  // lower-left triangle
        }
    }
}

void LiquifyMesh::reset() {
    for (auto& v : m_vertices) {
        v.displacement = QPointF(0.0, 0.0);
    }
}

const QPointF& LiquifyMesh::vertexPosition(int row, int col) const {
    return m_vertices[row * m_cols + col].position;
}

QPointF LiquifyMesh::vertexDisplacement(int row, int col) const {
    return m_vertices[row * m_cols + col].displacement;
}

void LiquifyMesh::setVertexDisplacement(int row, int col, const QPointF& disp) {
    m_vertices[row * m_cols + col].displacement = disp;
}

void LiquifyMesh::addVertexDisplacement(int row, int col, const QPointF& delta) {
    m_vertices[row * m_cols + col].displacement += delta;
}

int LiquifyMesh::triangleVertex(int triangleIdx, int localIdx) const {
    return m_triangles[triangleIdx * 3 + localIdx];
}

int LiquifyMesh::findContainingTriangle(qreal x, qreal y) const {
    if (m_rows < 2 || m_cols < 2) return -1;
    if (x < m_bounds.left() || x > m_bounds.right()
        || y < m_bounds.top() || y > m_bounds.bottom()) {
        return -1;
    }

    // Edge case: x == bounds.right() -> j would map to (cols-1) which is the
    // last vertex, not a cell. Clamp into the valid cell range.
    const qreal w = m_bounds.width();
    const qreal h = m_bounds.height();
    int j = 0;
    int i = 0;
    if (w > 0.0) {
        const qreal u = (x - m_bounds.x()) / w;
        j = qBound(0, int(u * (m_cols - 1)), m_cols - 2);
    }
    if (h > 0.0) {
        const qreal v = (y - m_bounds.y()) / h;
        i = qBound(0, int(v * (m_rows - 1)), m_rows - 2);
    }

    const int tl = i * m_cols + j;
    const int tr = i * m_cols + (j + 1);
    const int br = (i + 1) * m_cols + (j + 1);
    const int bl = (i + 1) * m_cols + j;
    const int triBase = (i * (m_cols - 1) + j) * 2;

    constexpr qreal kEps = -1e-9;
    const QPointF p(x, y);
    qreal a, b, c;

    // Triangle 0: tl, tr, br
    barycentric(m_vertices[tl].position, m_vertices[tr].position,
                m_vertices[br].position, p, a, b, c);
    if (a >= kEps && b >= kEps && c >= kEps) return triBase;

    // Triangle 1: tl, br, bl
    barycentric(m_vertices[tl].position, m_vertices[br].position,
                m_vertices[bl].position, p, a, b, c);
    if (a >= kEps && b >= kEps && c >= kEps) return triBase + 1;

    return -1;
}

QPointF LiquifyMesh::displacementAt(qreal x, qreal y) const {
    const int tri = findContainingTriangle(x, y);
    if (tri < 0) return QPointF(0.0, 0.0);

    const int v0 = m_triangles[tri * 3 + 0];
    const int v1 = m_triangles[tri * 3 + 1];
    const int v2 = m_triangles[tri * 3 + 2];

    qreal a, b, c;
    barycentric(m_vertices[v0].position, m_vertices[v1].position,
                m_vertices[v2].position, QPointF(x, y), a, b, c);

    return a * m_vertices[v0].displacement
         + b * m_vertices[v1].displacement
         + c * m_vertices[v2].displacement;
}

void LiquifyMesh::barycentric(const QPointF& a, const QPointF& b, const QPointF& c,
                              const QPointF& p, qreal& u, qreal& v, qreal& w) {
    const QPointF v0 = b - a;
    const QPointF v1 = c - a;
    const QPointF v2 = p - a;
    const qreal d00 = QPointF::dotProduct(v0, v0);
    const qreal d01 = QPointF::dotProduct(v0, v1);
    const qreal d11 = QPointF::dotProduct(v1, v1);
    const qreal d20 = QPointF::dotProduct(v2, v0);
    const qreal d21 = QPointF::dotProduct(v2, v1);
    const qreal denom = d00 * d11 - d01 * d01;
    if (qFuzzyIsNull(denom)) {
        u = v = w = 0.0;
        return;
    }
    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0 - v - w;
}

qreal LiquifyMesh::falloff(qreal distance, qreal radius) {
    if (radius <= 0.0) return 0.0;
    if (distance >= radius) return 0.0;
    // sigma = radius / 3 so falloff reaches ~exp(-4.5) ~= 0.011 at distance=radius.
    const qreal sigma = radius / 3.0;
    const qreal s2 = sigma * sigma;
    return std::exp(-(distance * distance) / (2.0 * s2));
}

QPointF LiquifyMesh::inverseWarp(qreal x, qreal y) const {
    const QPointF d = displacementAt(x, y);
    return QPointF(x - d.x(), y - d.y());
}

void LiquifyMesh::applyTool(const QPointF& center, qreal radius, qreal pressure,
                            const QPointF& direction, LiquifyToolMode mode) {
    if (radius <= 0.0 || pressure <= 0.0) return;

    for (int idx = 0; idx < m_vertices.size(); ++idx) {
        const QPointF pos = m_vertices[idx].position;
        const qreal dx = pos.x() - center.x();
        const qreal dy = pos.y() - center.y();
        const qreal dist = std::hypot(dx, dy);
        if (dist >= radius) continue;

        const qreal fall = falloff(dist, radius);
        const qreal scale = pressure * fall;

        QPointF contrib(0.0, 0.0);

        switch (mode) {
            case LiquifyToolMode::ForwardWarp:
                contrib = direction * scale;
                break;

            case LiquifyToolMode::Bloat: {
                if (dist > 1e-6) {
                    const QPointF radial(dx / dist, dy / dist);
                    contrib = radial * scale;
                }
                break;
            }

            case LiquifyToolMode::Pucker: {
                if (dist > 1e-6) {
                    const QPointF radial(dx / dist, dy / dist);
                    contrib = -radial * scale;
                }
                break;
            }

            case LiquifyToolMode::Twirl: {
                if (dist > 1e-6) {
                    // Pressure=1 maps to one full turn (2*PI).
                    // direction.y() >= 0 rotates CW (image-space).
                    const qreal sign = (direction.y() >= 0.0) ? -1.0 : 1.0;
                    const qreal angle = pressure * 2.0 * M_PI * fall * sign;
                    const qreal ca = std::cos(angle);
                    const qreal sa = std::sin(angle);
                    const QPointF rotated(ca * dx - sa * dy,
                                          sa * dx + ca * dy);
                    contrib = rotated - QPointF(dx, dy);
                }
                break;
            }

            case LiquifyToolMode::Smooth: {
                // Average this vertex's displacement with its 8-connected
                // neighbors that also fall inside the brush. Blend toward
                // the average by (pressure * fall).
                QPointF sum = m_vertices[idx].displacement;
                int count = 1;
                const int r = idx / m_cols;
                const int c = idx % m_cols;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) continue;
                        const int nr = r + dr;
                        const int nc = c + dc;
                        if (nr < 0 || nr >= m_rows || nc < 0 || nc >= m_cols) continue;
                        const int ni = nr * m_cols + nc;
                        const QPointF npos = m_vertices[ni].position;
                        const qreal ndx = npos.x() - center.x();
                        const qreal ndy = npos.y() - center.y();
                        const qreal ndist = std::hypot(ndx, ndy);
                        if (ndist >= radius) continue;
                        sum += m_vertices[ni].displacement;
                        ++count;
                    }
                }
                const QPointF avg = sum / qreal(count);
                contrib = (avg - m_vertices[idx].displacement) * scale;
                break;
            }

            case LiquifyToolMode::Reconstruct:
                // Pull displacement toward zero with gaussian falloff.
                contrib = -m_vertices[idx].displacement * scale;
                break;
        }

        m_vertices[idx].displacement += contrib;
    }
}

}  // namespace filters::liquify
