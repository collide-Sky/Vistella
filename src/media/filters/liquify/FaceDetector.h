#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace filters::liquify {

// One detected face + heuristically estimated feature points.
//
// P1.2.7 design note:
//   We do not yet run a 68-point landmark model (would require dlib or an
//   ONNX landmark detector, neither of which is wired into this build).
//   Feature points below are placed at standard face proportions derived
//   from the detection bbox. This delivers the PS-style "Face-Aware Liquify"
//   UX (six sliders operate on these anchor points) with reasonable -- but
//   not pixel-perfect -- precision. A real 68-point pass can replace the
//   estimator later without changing the rest of the pipeline.
struct Face {
    QRectF bbox;                  // tight bounding box (image coordinates)
    QPointF leftEyeCenter;        // mid of left eye
    QPointF rightEyeCenter;       // mid of right eye
    QPointF noseTip;              // tip of the nose
    QPointF mouthCenter;          // midpoint between mouth corners
    QPointF leftMouthCorner;
    QPointF rightMouthCorner;
    QPointF chin;
    float   confidence = 0.0f;    // detection confidence (Haar has none; always 1.0)
};

// OpenCV Haar cascade face detector + heuristic feature-point estimator.
//
// The detector reads a frontal-face Haar cascade XML shipped with OpenCV.
// On detect(), it returns all faces found above the configured minimum
// size; each Face is populated with the bbox and 8 anchor points.
class FaceDetector {
public:
    FaceDetector();
    ~FaceDetector();

    // Load the Haar cascade from `xmlPath`. Returns false if the file does
    // not exist or fails to load. The default xml shipped with OpenCV is
    //   opencv/build/etc/haarcascades/haarcascade_frontalface_default.xml
    bool load(const QString& xmlPath);

    bool isLoaded() const;

    // Detect faces in `image`. minSize is the smallest face to accept
    // (avoids spurious tiny matches); default 60 px.
    QVector<Face> detect(const QImage& image, int minSize = 60) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace filters::liquify
