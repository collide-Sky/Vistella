#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace filters::liquify {

// One detected face + heuristically estimated feature points.
//
// P1.2.7 design note (v2):
//   Upgraded from bbox-only heuristic to Haar cascade face + eye detection.
//   The eye cascade is run on the face ROI to locate the actual left/right
//   eye midpoints; remaining anchors (nose tip, mouth center, mouth
//   corners, chin, forehead) are placed at standard face proportions
//   adjusted by the now-known eye inter-pupillary distance. This delivers
//   noticeably better anchor precision than bbox-only heuristic, without
//   requiring any external landmark model (dlib or ONNX PFLD).
struct Face {
    QRectF bbox;                  // tight bounding box (image coordinates)
    QPointF leftEyeCenter;        // mid of left eye
    QPointF rightEyeCenter;       // mid of right eye
    QPointF noseTip;              // tip of the nose
    QPointF mouthCenter;          // midpoint between mouth corners
    QPointF leftMouthCorner;
    QPointF rightMouthCorner;
    QPointF chin;
    // Source of each eye anchor: 0 = Haar eye cascade, 1 = bbox heuristic
    // (used when the eye cascade missed both eyes for this face).
    int     leftEyeSource  = 1;
    int     rightEyeSource = 1;
    float   confidence = 1.0f;
};

// OpenCV Haar cascade face + eye detector + heuristic feature-point
// estimator.
//
// Loads two cascades:
//   1. Frontal face detector (haarcascade_frontalface_default.xml)
//   2. Eye detector         (haarcascade_eye.xml)
//
// On detect(), each Face's left/right eye centers are populated from the
// eye cascade. The remaining anchors (nose tip, mouth center, mouth
// corners, chin, forehead) fall back to standard face proportions
// relative to the bbox + the now-known eye midpoint. This gives markedly
// better anchor precision than bbox-only heuristics, without needing any
// external landmark model.
//
// If the eye cascade fails to load, the detector degrades gracefully to
// bbox-only heuristic for every anchor.
class FaceDetector {
public:
    FaceDetector();
    ~FaceDetector();

    bool loadFaceCascade(const QString& xmlPath);
    bool loadEyeCascade(const QString& xmlPath);
    bool isLoaded() const;
    bool hasEyeCascade() const;

    // Convenience: load both cascades using the local OpenCV default paths
    // (D:/Collide/opencv/build/etc/haarcascades/).
    bool loadDefaults();

    // Detect faces in `image`. minSize is the smallest face to accept
    // (avoids spurious tiny matches); default 60 px.
    QVector<Face> detect(const QImage& image, int minSize = 60) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace filters::liquify
