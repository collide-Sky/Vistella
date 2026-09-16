#include "LiquifyFaceAware.h"

#include <QtMath>
#include <cmath>

namespace filters::liquify {

namespace {

// Local gaussian falloff. Same shape as LiquifyMesh::falloff but kept
// private here so LiquifyFaceAware does not depend on LiquifyMesh internals.
inline qreal localFalloff(qreal distance, qreal radius) {
    if (radius <= 0.0) return 0.0;
    if (distance >= radius) return 0.0;
    const qreal sigma = radius / 3.0;
    const qreal s2 = sigma * sigma;
    return std::exp(-(distance * distance) / (2.0 * s2));
}

inline qreal eyeRadius(const Face& f)    { return 0.45 * f.bbox.width(); }
inline qreal noseRadius(const Face& f)   { return 0.35 * f.bbox.width(); }
inline qreal mouthRadius(const Face& f)  { return 0.50 * f.bbox.width(); }
inline qreal faceRadius(const Face& f)   { return 0.80 * f.bbox.width(); }

}  // namespace

void LiquifyFaceAware::warpSize(LiquifyMesh& mesh, const QPointF& center,
                                qreal radius, qreal slider) {
    if (slider == 0.0 || radius <= 0.0) return;
    const qreal scale = slider * 0.5;  // slider +-1 -> +-50% size at center
    for (int idx = 0; idx < mesh.vertexCount(); ++idx) {
        const int r = idx / mesh.cols();
        const int c = idx % mesh.cols();
        const QPointF pos = mesh.vertexPosition(r, c);
        const qreal dx = pos.x() - center.x();
        const qreal dy = pos.y() - center.y();
        const qreal dist = std::hypot(dx, dy);
        if (dist >= radius) continue;
        const qreal fall = localFalloff(dist, radius);
        const qreal factor = 1.0 + scale * fall;
        const QPointF newPos(center.x() + dx * factor,
                             center.y() + dy * factor);
        const QPointF delta = newPos - pos;
        mesh.addVertexDisplacement(r, c, delta);
    }
}

void LiquifyFaceAware::warpWidth(LiquifyMesh& mesh, const QPointF& center,
                                 qreal radius, qreal slider) {
    if (slider == 0.0 || radius <= 0.0) return;
    const qreal scale = slider * 0.5;
    for (int idx = 0; idx < mesh.vertexCount(); ++idx) {
        const int r = idx / mesh.cols();
        const int c = idx % mesh.cols();
        const QPointF pos = mesh.vertexPosition(r, c);
        const qreal dx = pos.x() - center.x();
        const qreal dy = pos.y() - center.y();
        const qreal dist = std::hypot(dx, dy);
        if (dist >= radius) continue;
        const qreal fall = localFalloff(dist, radius);
        const qreal factor = 1.0 + scale * fall;
        const QPointF newPos(center.x() + dx * factor, pos.y());
        const QPointF delta = newPos - pos;
        mesh.addVertexDisplacement(r, c, delta);
    }
}

void LiquifyFaceAware::warpWidthPair(LiquifyMesh& mesh, const QPointF& centerA,
                                     const QPointF& centerB, qreal radius,
                                     qreal slider) {
    if (slider == 0.0 || radius <= 0.0) return;
    const qreal scale = slider * 0.5;
    for (int idx = 0; idx < mesh.vertexCount(); ++idx) {
        const int r = idx / mesh.cols();
        const int c = idx % mesh.cols();
        const QPointF pos = mesh.vertexPosition(r, c);
        const QPointF relA = pos - centerA;
        const QPointF relB = pos - centerB;
        const qreal distA = std::hypot(relA.x(), relA.y());
        const qreal distB = std::hypot(relB.x(), relB.y());
        if (distA >= radius && distB >= radius) continue;

        const qreal fallA = localFalloff(distA, radius);
        const qreal fallB = localFalloff(distB, radius);
        QPointF center, rel;
        qreal fall;
        if (fallA >= fallB) {
            center = centerA; rel = relA; fall = fallA;
        } else {
            center = centerB; rel = relB; fall = fallB;
        }
        const qreal factor = 1.0 + scale * fall;
        const QPointF newPos(center.x() + rel.x() * factor, pos.y());
        const QPointF delta = newPos - pos;
        mesh.addVertexDisplacement(r, c, delta);
    }
}

void LiquifyFaceAware::applyToMesh(LiquifyMesh& mesh, const Face& face,
                                   const FaceSliders& s) {
    // Eye Size: enlarge/shrink both eyes (uniform radial).
    warpSize(mesh, face.leftEyeCenter,  eyeRadius(face),  s.eyeSize);
    warpSize(mesh, face.rightEyeCenter, eyeRadius(face),  s.eyeSize);

    // Nose Size: enlarge/shrink nose tip.
    warpSize(mesh, face.noseTip,        noseRadius(face), s.noseSize);

    // Nose Width: change nose width (horizontal only).
    warpWidth(mesh, face.noseTip,       noseRadius(face), s.noseWidth);

    // Mouth Size: enlarge/shrink mouth center.
    warpSize(mesh, face.mouthCenter,    mouthRadius(face), s.mouthSize);

    // Mouth Width: change mouth width (horizontal only).
    warpWidthPair(mesh, face.leftMouthCorner, face.rightMouthCorner,
                  mouthRadius(face) * 0.6, s.mouthWidth);

    // Face Width: horizontal scale around the face bbox center.
    const QPointF faceCenter(face.bbox.x() + face.bbox.width() * 0.5,
                             face.bbox.y() + face.bbox.height() * 0.5);
    warpWidth(mesh, faceCenter, faceRadius(face), s.faceWidth);
}

}  // namespace filters::liquify
