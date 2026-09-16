#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace filters::liquify {

// One detected face + 106-point landmark anchor subset.
//
// v3 (P1.2.7 upgrade v2): uses InsightFace buffalo_l ONNX models for both
// detection (det_10g.onnx, RetinaFace-10GF) and landmark localisation
// (2d106det.onnx, 106-point 2D).
//
// Anchor mapping (InsightFace 106-point indices):
//   leftEyeCenter    = average of indices 42..47
//   rightEyeCenter   = average of indices 36..41
//   noseTip          = index 33
//   mouthCenter      = average of indices 68..81 (inner lip ring)
//   leftMouthCorner  = index 48
//   rightMouthCorner = index 54
//   chin             = index 16 (jawline lowest)
//   forehead         = index 18 (top of eyebrow start, approximate)
struct Face {
    QRectF bbox;                  // tight bounding box (image coordinates)
    QPointF leftEyeCenter;
    QPointF rightEyeCenter;
    QPointF noseTip;
    QPointF mouthCenter;
    QPointF leftMouthCorner;
    QPointF rightMouthCorner;
    QPointF chin;
    QPointF forehead;
    // 0 = InsightFace DNN, 1 = bbox heuristic fallback.
    int     source = 0;
    float   confidence = 1.0f;
};

namespace landmark_indices {
constexpr int FOREHEAD          = 18;
constexpr int LEFT_EYE_FIRST    = 42;
constexpr int LEFT_EYE_LAST     = 47;
constexpr int RIGHT_EYE_FIRST   = 36;
constexpr int RIGHT_EYE_LAST    = 41;
constexpr int NOSE_TIP          = 33;
constexpr int MOUTH_LEFT        = 48;
constexpr int MOUTH_RIGHT       = 54;
constexpr int MOUTH_INNER_FIRST = 68;
constexpr int MOUTH_INNER_LAST  = 81;
constexpr int CHIN              = 16;
}  // namespace landmark_indices

// InsightFace buffalo_l face detector + landmark localiser.
//
// Loads two ONNX models from InsightFace's buffalo_l pack:
//   1. det_10g.onnx    - face detection (RetinaFace-10GF)
//   2. 2d106det.onnx   - 106-point landmark detection
//
// Both models are loaded via OpenCV's DNN module (cv::dnn::Net). The
// detector runs end-to-end:
//
//   QImage -> preprocess -> detectNet -> boxes
//            for each box:
//                crop -> preprocess -> landmarkNet -> 106 x,y in [0,1]
//                map back to image coordinates -> Face (8 anchors)
//
// If only the detection model is available (no landmark model), faces are
// still detected but anchors fall back to bbox heuristic.
class FaceDetector {
public:
    FaceDetector();
    ~FaceDetector();

    // Convenience: load both buffalo_l ONNX models from the project's
    // third_party/landmark folder.
    bool loadDefaults();

    // Load individual models. Both default to third_party/landmark/.
    bool loadDetectModel(const QString& onnxPath);
    bool loadLandmarkModel(const QString& onnxPath);

    bool isLoaded() const;
    bool hasLandmarkModel() const;

    // Detect faces in `image`. minSize is the smallest face to accept
    // (avoids spurious tiny matches); default 60 px.
    QVector<Face> detect(const QImage& image, int minSize = 60) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace filters::liquify