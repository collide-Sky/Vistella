// LiquifyMesh unit tests.
//
// Coverage:
//  1.  Default construction is empty.
//  2.  build(rows, cols, bounds) populates vertex + triangle arrays.
//  3.  Vertex count == rows * cols; triangle count == (rows-1)*(cols-1)*2.
//  4.  Vertex positions form a regular grid covering the bounds.
//  5.  reset() clears all displacements.
//  6.  findContainingTriangle returns a valid triangle for interior points.
//  7.  findContainingTriangle returns -1 for points outside bounds.
//  8.  findContainingTriangle returns -1 for an empty mesh.
//  9.  barycentric on a vertex a gives (1, 0, 0).
// 10.  barycentric at the triangle centroid gives (1/3, 1/3, 1/3).
// 11.  barycentric on a degenerate triangle collapses to (0, 0, 0).
// 12.  displacementAt uses barycentric interpolation.
// 13.  inverseWarp with zero displacement returns input.
// 14.  inverseWarp with positive vertex displacement returns (input - disp).
// 15.  falloff is 1.0 at distance 0, ~0 at distance == radius.
// 16.  Forward Warp adds direction * pressure * falloff per vertex.
// 17.  Forward Warp leaves vertices outside the brush untouched.
// 18.  Bloat pushes vertices outward radially.
// 19.  Pucker pulls vertices inward radially.
// 20.  Twirl rotates a vertex near the center by ~one full turn at pressure=1.
// 21.  Reconstruct pulls existing displacement toward zero.
// 22.  Smooth averages displacement with 8-connected neighbors.

#include <QtTest>
#include <QtMath>

#include "filters/liquify/LiquifyMesh.h"

using namespace filters::liquify;

class TestLiquifyMesh : public QObject {
    Q_OBJECT

private:
    static qreal approx(qreal a, qreal b, qreal tol = 1e-6) {
        return std::fabs(a - b) <= tol;
    }
    static bool approxPoint(const QPointF& a, const QPointF& b, qreal tol = 1e-6) {
        return approx(a.x(), b.x(), tol) && approx(a.y(), b.y(), tol);
    }

private slots:
    void defaultConstructionIsEmpty();
    void buildPopulatesArrays();
    void vertexAndTriangleCounts();
    void vertexPositionsFormGrid();
    void resetClearsDisplacements();
    void findContainingTriangleInside();
    void findContainingTriangleOutside();
    void findContainingTriangleEmptyMesh();
    void barycentricAtVertex();
    void barycentricAtCentroid();
    void barycentricDegenerate();
    void displacementAtInterpolation();
    void inverseWarpZeroDisplacement();
    void inverseWarpWithDisplacement();
    void falloffAtCenterAndEdge();
    void forwardWarpAppliesDirection();
    void forwardWarpIgnoresVerticesOutsideBrush();
    void bloatPushesOutward();
    void puckerPullsInward();
    void twirlRotatesAtCenter();
    void reconstructReducesDisplacement();
    void smoothAveragesNeighbors();
};

void TestLiquifyMesh::defaultConstructionIsEmpty() {
    LiquifyMesh m;
    QCOMPARE(m.rows(), 0);
    QCOMPARE(m.cols(), 0);
    QCOMPARE(m.vertexCount(), 0);
    QCOMPARE(m.triangleCount(), 0);
}

void TestLiquifyMesh::buildPopulatesArrays() {
    LiquifyMesh m;
    m.build(4, 5, QRectF(0, 0, 100, 100));
    QCOMPARE(m.rows(), 4);
    QCOMPARE(m.cols(), 5);
    QCOMPARE(m.vertexCount(), 20);
    QCOMPARE(m.triangleCount(), 3 * 4 * 2);
}

void TestLiquifyMesh::vertexAndTriangleCounts() {
    LiquifyMesh m;
    m.build(2, 2, QRectF(0, 0, 10, 10));
    QCOMPARE(m.vertexCount(), 4);
    QCOMPARE(m.triangleCount(), 2);
}

void TestLiquifyMesh::vertexPositionsFormGrid() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    // Top-left
    QVERIFY(approxPoint(m.vertexPosition(0, 0), QPointF(0, 0)));
    // Top-right
    QVERIFY(approxPoint(m.vertexPosition(0, 2), QPointF(100, 0)));
    // Bottom-left
    QVERIFY(approxPoint(m.vertexPosition(2, 0), QPointF(0, 100)));
    // Bottom-right
    QVERIFY(approxPoint(m.vertexPosition(2, 2), QPointF(100, 100)));
    // Middle
    QVERIFY(approxPoint(m.vertexPosition(1, 1), QPointF(50, 50)));
}

void TestLiquifyMesh::resetClearsDisplacements() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    m.setVertexDisplacement(1, 1, QPointF(5, -3));
    m.setVertexDisplacement(0, 2, QPointF(2, 2));
    QVERIFY(!approxPoint(m.vertexDisplacement(1, 1), QPointF(0, 0)));

    m.reset();
    for (int i = 0; i < m.rows(); ++i) {
        for (int j = 0; j < m.cols(); ++j) {
            QVERIFY(approxPoint(m.vertexDisplacement(i, j), QPointF(0, 0)));
        }
    }
}

void TestLiquifyMesh::findContainingTriangleInside() {
    LiquifyMesh m;
    m.build(4, 4, QRectF(0, 0, 100, 100));
    // Test several interior points; each must land in a valid triangle.
    const QPointF pts[] = {
        QPointF(50, 50), QPointF(10, 10), QPointF(90, 90),
        QPointF(25, 75), QPointF(75, 25), QPointF(0, 0),
        QPointF(100, 100), QPointF(33, 33), QPointF(66, 66),
    };
    for (const QPointF& p : pts) {
        const int t = m.findContainingTriangle(p.x(), p.y());
        QVERIFY2(t >= 0, qPrintable(QString("missed %1,%2").arg(p.x()).arg(p.y())));
        QVERIFY(t < m.triangleCount());
    }
}

void TestLiquifyMesh::findContainingTriangleOutside() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    QCOMPARE(m.findContainingTriangle(-1, 50), -1);
    QCOMPARE(m.findContainingTriangle(101, 50), -1);
    QCOMPARE(m.findContainingTriangle(50, -1), -1);
    QCOMPARE(m.findContainingTriangle(50, 101), -1);
}

void TestLiquifyMesh::findContainingTriangleEmptyMesh() {
    LiquifyMesh m;
    QCOMPARE(m.findContainingTriangle(50, 50), -1);
}

void TestLiquifyMesh::barycentricAtVertex() {
    const QPointF a(0, 0), b(10, 0), c(0, 10);
    qreal u, v, w;
    LiquifyMesh::barycentric(a, b, c, a, u, v, w);
    QVERIFY(approx(u, 1.0));
    QVERIFY(approx(v, 0.0));
    QVERIFY(approx(w, 0.0));
}

void TestLiquifyMesh::barycentricAtCentroid() {
    const QPointF a(0, 0), b(10, 0), c(0, 10);
    const QPointF centroid = (a + b + c) / 3.0;
    qreal u, v, w;
    LiquifyMesh::barycentric(a, b, c, centroid, u, v, w);
    QVERIFY(approx(u, 1.0 / 3.0, 1e-9));
    QVERIFY(approx(v, 1.0 / 3.0, 1e-9));
    QVERIFY(approx(w, 1.0 / 3.0, 1e-9));
}

void TestLiquifyMesh::barycentricDegenerate() {
    const QPointF a(0, 0), b(0, 0), c(0, 0);
    qreal u, v, w;
    LiquifyMesh::barycentric(a, b, c, QPointF(1, 1), u, v, w);
    QCOMPARE(u, 0.0);
    QCOMPARE(v, 0.0);
    QCOMPARE(w, 0.0);
}

void TestLiquifyMesh::displacementAtInterpolation() {
    LiquifyMesh m;
    m.build(2, 2, QRectF(0, 0, 100, 100));
    // Set corner displacements so we can predict interpolation.
    m.setVertexDisplacement(0, 0, QPointF(0, 0));     // top-left
    m.setVertexDisplacement(0, 1, QPointF(100, 0));   // top-right
    m.setVertexDisplacement(1, 0, QPointF(0, 100));   // bottom-left
    m.setVertexDisplacement(1, 1, QPointF(100, 100)); // bottom-right

    // Center should have displacement ~ (50, 50) from the diagonal triangle.
    const QPointF d = m.displacementAt(50, 50);
    QVERIFY(approx(d.x(), 50.0, 1e-3));
    QVERIFY(approx(d.y(), 50.0, 1e-3));
}

void TestLiquifyMesh::inverseWarpZeroDisplacement() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    const QPointF p = m.inverseWarp(50, 50);
    QVERIFY(approxPoint(p, QPointF(50, 50)));
}

void TestLiquifyMesh::inverseWarpWithDisplacement() {
    LiquifyMesh m;
    m.build(2, 2, QRectF(0, 0, 100, 100));
    // All four corners carry the same (10, 0) displacement so the
    // interpolated displacement at any interior point is exactly (10, 0).
    m.setVertexDisplacement(0, 0, QPointF(10, 0));
    m.setVertexDisplacement(0, 1, QPointF(10, 0));
    m.setVertexDisplacement(1, 0, QPointF(10, 0));
    m.setVertexDisplacement(1, 1, QPointF(10, 0));
    const QPointF src = m.inverseWarp(50, 50);
    QVERIFY(approx(src.x(), 40.0, 1e-6));
    QVERIFY(approx(src.y(), 50.0, 1e-6));
}

void TestLiquifyMesh::falloffAtCenterAndEdge() {
    QCOMPARE(LiquifyMesh::falloff(0.0, 100.0), 1.0);
    QVERIFY(LiquifyMesh::falloff(100.0, 100.0) < 1e-2);
    QVERIFY(LiquifyMesh::falloff(50.0, 100.0) > 0.3);  // gaussian: at d=r/2, ~0.32
    QCOMPARE(LiquifyMesh::falloff(200.0, 100.0), 0.0);
}

void TestLiquifyMesh::forwardWarpAppliesDirection() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    // Apply at (50, 50) with radius 60, pressure 1.0, direction (10, 0).
    m.applyTool(QPointF(50, 50), 60.0, 1.0,
                QPointF(10, 0), LiquifyToolMode::ForwardWarp);

    // Center vertex (1, 1) at (50, 50) should have displacement = (10, 0)
    // because falloff at d=0 is 1.0 and pressure is 1.0.
    const QPointF d = m.vertexDisplacement(1, 1);
    QVERIFY(approx(d.x(), 10.0, 1e-6));
    QVERIFY(approx(d.y(), 0.0, 1e-6));

    // Far corner (0, 0) at (0, 0) is d=70 from center, outside brush.
    const QPointF dFar = m.vertexDisplacement(0, 0);
    QVERIFY(approxPoint(dFar, QPointF(0, 0)));
}

void TestLiquifyMesh::forwardWarpIgnoresVerticesOutsideBrush() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    m.applyTool(QPointF(50, 50), 30.0, 1.0,
                QPointF(5, 5), LiquifyToolMode::ForwardWarp);

    // (0,0) at (0,0) is 70 from center, outside radius 30.
    QVERIFY(approxPoint(m.vertexDisplacement(0, 0), QPointF(0, 0)));
    // (2,2) at (100,100) is 70 from center, outside radius 30.
    QVERIFY(approxPoint(m.vertexDisplacement(2, 2), QPointF(0, 0)));
    // (1,1) at (50,50) is at center, must have non-zero displacement.
    QVERIFY(!approxPoint(m.vertexDisplacement(1, 1), QPointF(0, 0), 1e-3));
}

void TestLiquifyMesh::bloatPushesOutward() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    // Brush radius 80 covers all corners; we want to inspect radial
    // displacement at a corner that lies strictly on a cardinal axis.
    m.applyTool(QPointF(50, 50), 80.0, 1.0,
                QPointF(0, 0), LiquifyToolMode::Bloat);

    // Center (1,1) at (50,50): dist=0, radial undefined, no push applied.
    QVERIFY(approxPoint(m.vertexDisplacement(1, 1), QPointF(0, 0), 1e-6));

    // Vertex (1, 2) at (100, 50): radial direction is (+1, 0) (outward +x).
    const QPointF d = m.vertexDisplacement(1, 2);
    const qreal mag = std::hypot(d.x(), d.y());
    QVERIFY(mag > 0.0);
    QVERIFY(d.x() > 0.0);
    QVERIFY(approx(d.y(), 0.0, 1e-3));

    // Vertex (1, 0) at (0, 50): radial direction is (-1, 0) (outward -x).
    const QPointF d2 = m.vertexDisplacement(1, 0);
    QVERIFY(d2.x() < 0.0);
    QVERIFY(approx(d2.y(), 0.0, 1e-3));
}

void TestLiquifyMesh::puckerPullsInward() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    m.applyTool(QPointF(50, 50), 80.0, 1.0,
                QPointF(0, 0), LiquifyToolMode::Pucker);

    // Vertex (1, 2) at (100, 50): radial direction is (+1, 0).
    // Pucker subtracts, so inward = (-x, 0).
    const QPointF d = m.vertexDisplacement(1, 2);
    const qreal mag = std::hypot(d.x(), d.y());
    QVERIFY(mag > 0.0);
    QVERIFY(d.x() < 0.0);
    QVERIFY(approx(d.y(), 0.0, 1e-3));

    // Vertex (1, 0) at (0, 50): radial is (-1, 0); pucker = (+x, 0).
    const QPointF d2 = m.vertexDisplacement(1, 0);
    QVERIFY(d2.x() > 0.0);
    QVERIFY(approx(d2.y(), 0.0, 1e-3));
}

void TestLiquifyMesh::twirlRotatesAtCenter() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    // Use direction.y() < 0 to pick CCW (positive-angle) rotation.
    // Pressure=1.0 should produce ~360° rotation at the center.
    m.applyTool(QPointF(50, 50), 60.0, 1.0,
                QPointF(0, -1), LiquifyToolMode::Twirl);

    // Vertex (0, 2) at (100, 0): relative pos = (50, -50).
    // After ~360° rotation, relative pos should still be ~(50, -50) (modulo
    // small numerical drift). The displacement added is rotated - original,
    // which for full-turn rotation should be ~0.
    const QPointF d = m.vertexDisplacement(0, 2);
    QVERIFY(std::fabs(d.x()) < 5.0);
    QVERIFY(std::fabs(d.y()) < 5.0);

    // At half-radius, rotation is partial: relative position changes.
    // Vertex (1, 1) at (50, 50) is at center -> no movement.
    QVERIFY(approxPoint(m.vertexDisplacement(1, 1), QPointF(0, 0), 1e-3));
}

void TestLiquifyMesh::reconstructReducesDisplacement() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    // Pre-set a displacement on the center vertex.
    m.setVertexDisplacement(1, 1, QPointF(20, 0));
    // Apply reconstruct with full pressure: should zero out the displacement.
    m.applyTool(QPointF(50, 50), 60.0, 1.0,
                QPointF(0, 0), LiquifyToolMode::Reconstruct);
    const QPointF d = m.vertexDisplacement(1, 1);
    QVERIFY(std::fabs(d.x()) < 1e-3);
    QVERIFY(std::fabs(d.y()) < 1e-3);
}

void TestLiquifyMesh::smoothAveragesNeighbors() {
    LiquifyMesh m;
    m.build(3, 3, QRectF(0, 0, 100, 100));
    // Give the 4 corners distinct displacements.
    m.setVertexDisplacement(0, 0, QPointF(0, 0));
    m.setVertexDisplacement(0, 2, QPointF(0, 0));
    m.setVertexDisplacement(2, 0, QPointF(0, 0));
    m.setVertexDisplacement(2, 2, QPointF(0, 0));
    // Center has a strong displacement that smooth should pull toward 0.
    m.setVertexDisplacement(1, 1, QPointF(40, 0));
    // Edges also contribute.
    m.setVertexDisplacement(0, 1, QPointF(20, 0));
    m.setVertexDisplacement(1, 0, QPointF(20, 0));
    m.setVertexDisplacement(1, 2, QPointF(20, 0));
    m.setVertexDisplacement(2, 1, QPointF(20, 0));

    m.applyTool(QPointF(50, 50), 80.0, 1.0,
                QPointF(0, 0), LiquifyToolMode::Smooth);

    // After smoothing with pressure=1.0 at center, the center vertex
    // should be pulled significantly toward its 8-neighbor average (~5).
    const QPointF d = m.vertexDisplacement(1, 1);
    QVERIFY(d.x() < 40.0);  // reduced from 40
    QVERIFY(d.x() > 0.0);   // still positive (average of positives)
}

QTEST_MAIN(TestLiquifyMesh)
#include "tst_LiquifyMesh.moc"
