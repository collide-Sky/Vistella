#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace filters::liquify {

// Tool modes that drive LiquifyMesh::applyTool().
enum class LiquifyToolMode {
    ForwardWarp,    // push pixels in cursor direction (W)
    Reconstruct,    // reset displacement with gaussian falloff (R)
    Smooth,         // average neighbor displacements within brush (S)
    Twirl,          // rotate pixels around center, angle from pressure (C)
    Pucker,         // pull pixels toward center (B)
    Bloat,          // push pixels outward from center (V)
};

struct LiquifyVertex {
    QPointF position;
    QPointF displacement;
};

// Triangular mesh deformation field covering an image rect.
//
// PS Liquify model: a regular NxN grid of vertices, each carrying a 2D
// displacement vector. The image plane is partitioned into 2*(N-1)*(N-1)
// triangles. Output pixel (x, y) maps to source (x - d_x, y - d_y) where
// (d_x, d_y) is the barycentric interpolation of the containing triangle's
// three vertex displacements.
//
// All coordinates are in image pixel space; bounds() defines the mesh extent.
// Vertex (0, 0) sits at the top-left of bounds; vertex (rows-1, cols-1) sits
// at the bottom-right.
class LiquifyMesh {
public:
    LiquifyMesh() = default;

    // Build a rows x cols mesh covering bounds.
    // rows/cols must both be >= 2 (a mesh of size 1 has no triangles).
    // Existing state is discarded.
    void build(int rows, int cols, const QRectF& bounds);

    // Reset every vertex displacement to (0, 0). Geometry is preserved.
    void reset();

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    const QRectF& bounds() const { return m_bounds; }
    int vertexCount() const { return m_vertices.size(); }
    int triangleCount() const { return m_triangles.size() / 3; }

    // Vertex access (row-major: index = row * cols + col).
    const QPointF& vertexPosition(int row, int col) const;
    QPointF vertexDisplacement(int row, int col) const;
    void setVertexDisplacement(int row, int col, const QPointF& disp);
    void addVertexDisplacement(int row, int col, const QPointF& delta);

    // Direct index-based access (for overlays / iteration).
    const QPointF& vertexPositionAt(int idx) const { return m_vertices[idx].position; }
    QPointF vertexDisplacementAt(int idx) const { return m_vertices[idx].displacement; }

    // Triangle vertex lookup.
    // triangleIdx in [0, triangleCount()), localIdx in {0, 1, 2}.
    // Returns the global vertex row*cols+col for that corner.
    int triangleVertex(int triangleIdx, int localIdx) const;

    // Locate the triangle containing pixel (x, y).
    // Returns triangleIdx in [0, triangleCount()) on hit, -1 if outside bounds.
    // O(1) by row scan: compute cell row/col from y/x, test both triangles.
    int findContainingTriangle(qreal x, qreal y) const;

    // Interpolated displacement at (x, y) using barycentric weights.
    // Returns zero displacement if (x, y) is outside the mesh.
    QPointF displacementAt(qreal x, qreal y) const;

    // Apply one brush stamp.
    // center: brush center in image pixels.
    // radius: brush radius in image pixels (vertices beyond this are untouched).
    // pressure: tool strength in [0, 1]; semantics depend on mode.
    // direction: cursor motion vector (used by ForwardWarp);
    //            for Twirl, sign(direction.y()) picks CW/CCW rotation;
    //            for radial tools (Bloat/Pucker) and Smooth/Reconstruct
    //            this parameter is ignored.
    // mode: see LiquifyToolMode.
    //
    // For each vertex v within radius, contribution is gaussian-falloff scaled.
    void applyTool(const QPointF& center, qreal radius, qreal pressure,
                   const QPointF& direction, LiquifyToolMode mode);

    // Gaussian falloff: 1.0 at distance=0, exp(-4.5) ~= 0.011 at distance=radius.
    // Used by all tools to give the brush a soft edge.
    static qreal falloff(qreal distance, qreal radius);

    // Barycentric coordinates of p with respect to triangle (a, b, c).
    // After return, p == u*a + v*b + w*c and u + v + w == 1.
    // If the triangle is degenerate, all weights collapse to 0.
    static void barycentric(const QPointF& a, const QPointF& b, const QPointF& c,
                            const QPointF& p, qreal& u, qreal& v, qreal& w);

    // Inverse warp: given destination (x, y), return the source pixel
    // (x - dx, y - dy) where (dx, dy) is the mesh displacement at (x, y).
    // Used by the renderer to sample the original image at the warped location.
    QPointF inverseWarp(qreal x, qreal y) const;

private:
    int m_rows = 0;
    int m_cols = 0;
    QRectF m_bounds;
    QVector<LiquifyVertex> m_vertices;
    QVector<int> m_triangles;  // 3 ints per triangle, indexing m_vertices
};

}  // namespace filters::liquify
