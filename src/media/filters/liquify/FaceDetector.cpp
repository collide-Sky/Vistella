#include "FaceDetector.h"

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

namespace filters::liquify {

struct FaceDetector::Impl {
    cv::dnn::Net detectNet;
    cv::dnn::Net landmarkNet;
    bool detectLoaded = false;
    bool landmarkLoaded = false;
};

FaceDetector::FaceDetector() : m_impl(new Impl) {}
FaceDetector::~FaceDetector() { delete m_impl; }

bool FaceDetector::loadDetectModel(const QString& onnxPath) {
    if (!m_impl) return false;
    try {
        m_impl->detectNet = cv::dnn::readNetFromONNX(onnxPath.toStdString());
    } catch (const cv::Exception&) {
        return false;
    }
    if (m_impl->detectNet.empty()) return false;
    m_impl->detectLoaded = true;
    return true;
}

bool FaceDetector::loadLandmarkModel(const QString& onnxPath) {
    if (!m_impl) return false;
    try {
        m_impl->landmarkNet = cv::dnn::readNetFromONNX(onnxPath.toStdString());
    } catch (const cv::Exception&) {
        return false;
    }
    if (m_impl->landmarkNet.empty()) return false;
    m_impl->landmarkLoaded = true;
    return true;
}

bool FaceDetector::isLoaded() const {
    return m_impl && m_impl->detectLoaded;
}

bool FaceDetector::hasLandmarkModel() const {
    return m_impl && m_impl->landmarkLoaded;
}

bool FaceDetector::loadDefaults() {
    const QString detectPath = QStringLiteral(
        "D:/Collide/MyCode/QT6Projects/MultiDoc/third_party/landmark/det_10g.onnx");
    const QString landmarkPath = QStringLiteral(
        "D:/Collide/MyCode/QT6Projects/MultiDoc/third_party/landmark/2d106det.onnx");
    bool ok = loadDetectModel(detectPath);
    loadLandmarkModel(landmarkPath);  // optional, but try
    return ok;
}

namespace {

cv::Mat qImageToBgr(const QImage& img) {
    switch (img.format()) {
        case QImage::Format_RGB32:
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied: {
            cv::Mat rgba(img.height(), img.width(), CV_8UC4,
                         (void*)img.bits(), img.bytesPerLine());
            cv::Mat bgr;
            cv::cvtColor(rgba, bgr, cv::COLOR_BGRA2BGR);
            return bgr;
        }
        case QImage::Format_Grayscale8: {
            cv::Mat gray(img.height(), img.width(), CV_8UC1,
                         (void*)img.bits(), img.bytesPerLine());
            cv::Mat bgr;
            cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
            return bgr;
        }
        default: {
            const QImage conv = img.convertToFormat(QImage::Format_ARGB32);
            cv::Mat rgba(conv.height(), conv.width(), CV_8UC4,
                         (void*)conv.bits(), conv.bytesPerLine());
            cv::Mat bgr;
            cv::cvtColor(rgba, bgr, cv::COLOR_BGRA2BGR);
            return bgr;
        }
    }
}

// Average a range of (x, y) pairs from a flat landmark array.
QPointF avgPair(const QVector<float>& flat, int first, int last) {
    QPointF sum(0, 0);
    const int n = last - first + 1;
    for (int i = first; i <= last; ++i) {
        sum.rx() += flat[2 * i];
        sum.ry() += flat[2 * i + 1];
    }
    return QPointF(sum.x() / n, sum.y() / n);
}

// Map face bbox + 106 landmarks (in [0,1] within the face ROI) to absolute
// image coordinates.
void fillFaceAnchors(Face& f, const QVector<float>& lm,
                     const cv::Rect& box) {
    auto mapPt = [&](int idx) {
        return QPointF(box.x + lm[2 * idx]     * box.width,
                       box.y + lm[2 * idx + 1] * box.height);
    };

    QPointF sumLE(0, 0), sumRE(0, 0);
    for (int i = landmark_indices::LEFT_EYE_FIRST;
         i <= landmark_indices::LEFT_EYE_LAST; ++i) {
        sumLE += mapPt(i);
    }
    f.leftEyeCenter = sumLE / 6.0;

    for (int i = landmark_indices::RIGHT_EYE_FIRST;
         i <= landmark_indices::RIGHT_EYE_LAST; ++i) {
        sumRE += mapPt(i);
    }
    f.rightEyeCenter = sumRE / 6.0;

    f.noseTip = mapPt(landmark_indices::NOSE_TIP);
    f.leftMouthCorner  = mapPt(landmark_indices::MOUTH_LEFT);
    f.rightMouthCorner = mapPt(landmark_indices::MOUTH_RIGHT);

    QPointF sumMouth(0, 0);
    const int innerCount = landmark_indices::MOUTH_INNER_LAST
                         - landmark_indices::MOUTH_INNER_FIRST + 1;
    for (int i = landmark_indices::MOUTH_INNER_FIRST;
         i <= landmark_indices::MOUTH_INNER_LAST; ++i) {
        sumMouth += mapPt(i);
    }
    f.mouthCenter = sumMouth / innerCount;

    f.chin      = mapPt(landmark_indices::CHIN);
    f.forehead  = mapPt(landmark_indices::FOREHEAD);
    f.source    = 0;
    f.confidence = 1.0f;
}

// Bbox heuristic fallback (used when the landmark model is missing).
struct BboxRatios {
    qreal eyeY      = 0.42;
    qreal noseX     = 0.50;
    qreal noseY     = 0.62;
    qreal mouthY    = 0.80;
    qreal mouthLeftX  = 0.36;
    qreal mouthRightX = 0.64;
};

void fillFromHeuristics(Face& f) {
    const qreal x = f.bbox.x();
    const qreal y = f.bbox.y();
    const qreal w = f.bbox.width();
    const qreal h = f.bbox.height();
    const BboxRatios r;
    f.leftEyeCenter    = QPointF(x + 0.32 * w, y + r.eyeY * h);
    f.rightEyeCenter   = QPointF(x + 0.68 * w, y + r.eyeY * h);
    f.noseTip          = QPointF(x + r.noseX * w, y + r.noseY * h);
    f.mouthCenter      = QPointF(x + r.noseX * w, y + r.mouthY * h);
    f.leftMouthCorner  = QPointF(x + r.mouthLeftX  * w, y + r.mouthY * h);
    f.rightMouthCorner = QPointF(x + r.mouthRightX * w, y + r.mouthY * h);
    f.chin             = QPointF(x + r.noseX * w, y + 0.98 * h);
    f.forehead         = QPointF(x + r.noseX * w, y + 0.10 * h);
    f.source = 1;
}

}  // namespace

QVector<Face> FaceDetector::detect(const QImage& image, int minSize) const {
    QVector<Face> result;
    if (!isLoaded() || image.isNull()) return result;

    const cv::Mat bgr = qImageToBgr(image);
    if (bgr.empty()) return result;

    // ---- Detection ----
    constexpr int detSize = 640;
    const cv::Mat detBlob = cv::dnn::blobFromImage(
        bgr, 1.0 / 128.0, cv::Size(detSize, detSize),
        cv::Scalar(127.5, 127.5, 127.5), true);
    m_impl->detectNet.setInput(detBlob);
    cv::Mat detOut = m_impl->detectNet.forward();

    // InsightFace det_10g.onnx output (1, N, 15): per-row [x1, y1, x2, y2,
    // score, kx1, ky1, ... kx5, ky5] already in original-image scale.
    // Filter rows by score and minSize, then collect boxes.
    std::vector<cv::Rect> boxes;
    if (detOut.dims == 3 && detOut.size[2] >= 5) {
        const float* data = detOut.ptr<float>();
        const int rows = detOut.size[1];
        const int cols = detOut.size[2];
        const float scaleX = float(bgr.cols) / float(detSize);
        const float scaleY = float(bgr.rows) / float(detSize);
        for (int i = 0; i < rows; ++i) {
            const float* row = data + i * cols;
            const float score = row[4];
            if (score < 0.5f) continue;
            const int x1 = int(row[0] * scaleX);
            const int y1 = int(row[1] * scaleY);
            const int x2 = int(row[2] * scaleX);
            const int y2 = int(row[3] * scaleY);
            const int w = x2 - x1;
            const int h = y2 - y1;
            if (w < minSize || h < minSize) continue;
            boxes.emplace_back(x1, y1, w, h);
        }
    }

    result.reserve(int(boxes.size()));
    for (const cv::Rect& box : boxes) {
        Face f;
        f.bbox = QRectF(qreal(box.x), qreal(box.y),
                        qreal(box.width), qreal(box.height));

        if (m_impl->landmarkLoaded) {
            // Crop face ROI with a 10% margin for better landmark accuracy.
            const int marginX = box.width  / 10;
            const int marginY = box.height / 10;
            const cv::Rect crop(
                std::max(0, box.x - marginX),
                std::max(0, box.y - marginY),
                std::min(bgr.cols - std::max(0, box.x - marginX),
                         box.width  + 2 * marginX),
                std::min(bgr.rows - std::max(0, box.y - marginY),
                         box.height + 2 * marginY));
            if (crop.width > 0 && crop.height > 0) {
                cv::Mat faceRoi = bgr(crop);
                constexpr int lmSize = 192;
                const cv::Mat lmBlob = cv::dnn::blobFromImage(
                    faceRoi, 1.0 / 128.0, cv::Size(lmSize, lmSize),
                    cv::Scalar(127.5, 127.5, 127.5), true);
                m_impl->landmarkNet.setInput(lmBlob);
                cv::Mat lmOut = m_impl->landmarkNet.forward();
                // lmOut shape: (1, 212, 1, 1) -> 106 x,y in [0,1] of face crop.
                QVector<float> lm(212, 0.0f);
                if (lmOut.total() >= 212) {
                    const float* p = lmOut.ptr<float>();
                    for (int i = 0; i < 212; ++i) lm[i] = p[i];
                }
                fillFaceAnchors(f, lm, crop);
            } else {
                fillFromHeuristics(f);
            }
        } else {
            fillFromHeuristics(f);
        }
        result.append(f);
    }
    return result;
}

}  // namespace filters::liquify