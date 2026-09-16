#pragma once

#include <QPointF>

#include "FaceDetector.h"
#include "LiquifyMesh.h"

namespace filters::liquify {

// Face-aware liquify: translates six face sliders into mesh displacements.
//
// All sliders live in [-1, 1]:
//   -1.0   strongest negative direction (shrink / narrow)
//    0.0   no effect
//   +1.0   strongest positive direction (enlarge / widen)
//
// Each slider applies a localized radial warp around one (or two) feature
// points with a gaussian falloff. Displacements accumulate into the mesh
// in place.
struct FaceSliders {
    qreal eyeSize    = 0.0;
    qreal noseSize   = 0.0;
    qreal noseWidth  = 0.0;
    qreal mouthSize  = 0.0;
    qreal mouthWidth = 0.0;
    qreal faceWidth  = 0.0;
};

class LiquifyFaceAware {
public:
    // Apply the slider mix for one face to `mesh`. The mesh bounds should
    // already cover the image containing the face.
    //
    // Internally calls the per-slider warp helpers; each helper takes a
    // center, falloff radius, slider value, and a "horizontal-only" flag.
    static void applyToMesh(LiquifyMesh& mesh, const Face& face,
                            const FaceSliders& s);

    // Direct per-slider warps. Public so tests can target each helper.
    static void warpSize(LiquifyMesh& mesh, const QPointF& center, qreal radius,
                         qreal slider);
    static void warpWidth(LiquifyMesh& mesh, const QPointF& center, qreal radius,
                          qreal slider);

    // Width slider affecting both eye centers (or any symmetric pair).
    static void warpWidthPair(LiquifyMesh& mesh, const QPointF& centerA,
                              const QPointF& centerB, qreal radius, qreal slider);
};

}  // namespace filters::liquify
