#include "FaceDetector.h"

#include <opencv2/objdetect.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace filters::liquify {

struct FaceDetector::Impl {
    cv::CascadeClassifier face;
    cv::CascadeClassifier eye;
    bool eyeLoaded = false;
};

FaceDetector::FaceDetector() : m_impl(new Impl) {}
FaceDetector::~FaceDetector() { delete m_impl; }

bool FaceDetector::loadFaceCascade(const QString& xmlPath) {
    if (!m_impl) return false;
    return m_impl->face.load(xmlPath.toStdString());
}

bool FaceDetector::loadEyeCascade(const QString& xmlPath) {
    if (!m_impl) return false;
    m_impl->eyeLoaded = m_impl->eye.load(xmlPath.toStdString());
    return m_impl->eyeLoaded;
}

bool FaceDetector::isLoaded() const {
    return m_impl && !m_impl->face.empty();
}

bool FaceDetector::hasEyeCascade() const {
    return m_impl && m_impl->eyeLoaded;
}

bool FaceDetector::loadDefaults() {
    const QString faceXml = QStringLiteral(
        "D:/Collide/opencv/build/etc/haarcascades/haarcascade_frontalface_default.xml");
    const QString eyeXml = QStringLiteral(
        "D:/Collide/opencv/build/etc/haarcascades/haarcascade_eye.xml");
    bool ok = loadFaceCascade(faceXml);
    loadEyeCascade(eyeXml);  // optional
    return ok;
}

namespace {

cv::Mat qImageToGray(const QImage& img) {
    switch (img.format()) {
        case QImage::Format_Grayscale8:
            return cv::Mat(img.height(), img.width(), CV_8UC1,
                            (void*)img.bits(), img.bytesPerLine()).clone();
        case QImage::Format_RGB32:
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied: {
            cv::Mat rgba(img.height(), img.width(), CV_8UC4,
                         (void*)img.bits(), img.bytesPerLine());
            cv::Mat gray;
            cv::cvtColor(rgba, gray, cv::COLOR_BGRA2GRAY);
            return gray;
        }
        default: {
            const QImage conv = img.convertToFormat(QImage::Format_Grayscale8);
            return cv::Mat(conv.height(), conv.width(), CV_8UC1,
                            (void*)conv.bits(), conv.bytesPerLine()).clone();
        }
    }
}

// Standard face proportions (relative to face bbox). Used as fallback for
// any anchor that the eye cascade could not detect.
struct FaceRatios {
    qreal leftEyeX  = 0.32;
    qreal rightEyeX = 0.68;
    qreal eyeY      = 0.42;
    qreal noseX     = 0.50;
    qreal noseY     = 0.62;
    qreal mouthY    = 0.80;
    qreal mouthLeftX = 0.36;
    qreal mouthRightX = 0.64;
    qreal chinY     = 0.98;
};

void fillFromHeuristics(Face& f, const FaceRatios& r) {
    const qreal x = f.bbox.x();
    const qreal y = f.bbox.y();
    const qreal w = f.bbox.width();
    const qreal h = f.bbox.height();
    f.leftEyeCenter    = QPointF(x + r.leftEyeX  * w, y + r.eyeY * h);
    f.rightEyeCenter   = QPointF(x + r.rightEyeX * w, y + r.eyeY * h);
    f.leftEyeSource  = 1;
    f.rightEyeSource = 1;
    f.noseTip          = QPointF(x + r.noseX * w, y + r.noseY * h);
    f.mouthCenter      = QPointF(x + r.noseX * w, y + r.mouthY * h);
    f.leftMouthCorner  = QPointF(x + r.mouthLeftX  * w, y + r.mouthY * h);
    f.rightMouthCorner = QPointF(x + r.mouthRightX * w, y + r.mouthY * h);
    f.chin             = QPointF(x + r.noseX * w, y + r.chinY * h);
}

// Locate eye rectangles in `gray` restricted to the face ROI. Returns
// up to two eye rectangles (the largest, used as left + right heuristic).
std::vector<cv::Rect> detectEyesInRoi(cv::CascadeClassifier& eye,
                                      const cv::Mat& gray,
                                      const cv::Rect& roi) {
    std::vector<cv::Rect> eyes;
    if (eye.empty()) return eyes;
    cv::Mat faceRoi = gray(roi);
    eye.detectMultiScale(faceRoi, eyes,
                          1.1, 3, 0,
                          cv::Size(faceRoi.cols / 8, faceRoi.rows / 8));
    for (cv::Rect& e : eyes) {
        e.x += roi.x;
        e.y += roi.y;
    }
    return eyes;
}

}  // namespace

QVector<Face> FaceDetector::detect(const QImage& image, int minSize) const {
    QVector<Face> result;
    if (!isLoaded() || image.isNull()) return result;

    const cv::Mat gray = qImageToGray(image);
    if (gray.empty()) return result;

    std::vector<cv::Rect> rects;
    m_impl->face.detectMultiScale(gray, rects, 1.1, 3, 0,
                                  cv::Size(minSize, minSize));

    result.reserve(int(rects.size()));
    for (const cv::Rect& r : rects) {
        Face f;
        f.bbox = QRectF(qreal(r.x), qreal(r.y), qreal(r.width), qreal(r.height));

        // Try to detect eyes inside the face ROI.
        if (m_impl->eyeLoaded) {
            const std::vector<cv::Rect> eyes = detectEyesInRoi(
                m_impl->eye, gray, r);

            // Cluster by x: left eye has smaller x, right eye has larger x.
            QPointF leftEye, rightEye;
            bool haveLeft = false, haveRight = false;
            qreal bestLeftX = std::numeric_limits<qreal>::infinity();
            qreal bestRightX = -std::numeric_limits<qreal>::infinity();
            for (const cv::Rect& e : eyes) {
                const qreal cx = qreal(e.x + e.width  / 2);
                const qreal cy = qreal(e.y + e.height / 2);
                if (cx < bestLeftX) {
                    bestLeftX = cx; leftEye = QPointF(cx, cy); haveLeft = true;
                }
                if (cx > bestRightX) {
                    bestRightX = cx; rightEye = QPointF(cx, cy); haveRight = true;
                }
            }
            // Fill non-eye anchors from heuristic first; override eyes if
            // we have detections.
            fillFromHeuristics(f, FaceRatios{});
            if (haveLeft) {
                f.leftEyeCenter = leftEye;
                f.leftEyeSource = 0;
            }
            if (haveRight) {
                f.rightEyeCenter = rightEye;
                f.rightEyeSource = 0;
            }
        } else {
            fillFromHeuristics(f, FaceRatios{});
        }
        result.append(f);
    }
    return result;
}

}  // namespace filters::liquify