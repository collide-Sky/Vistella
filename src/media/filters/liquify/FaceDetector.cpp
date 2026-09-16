#include "FaceDetector.h"

#include <opencv2/objdetect.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace filters::liquify {

struct FaceDetector::Impl {
    cv::CascadeClassifier cascade;
};

FaceDetector::FaceDetector() : m_impl(new Impl) {}

FaceDetector::~FaceDetector() {
    delete m_impl;
}

bool FaceDetector::load(const QString& xmlPath) {
    if (!m_impl) return false;
    const cv::String path = xmlPath.toStdString();
    if (!m_impl->cascade.load(path)) {
        return false;
    }
    return true;
}

bool FaceDetector::isLoaded() const {
    return m_impl && !m_impl->cascade.empty();
}

// Convert a QImage (assumed ARGB32 / RGB32 / Grayscale8) to a single-channel
// 8-bit cv::Mat. QImage data is not copied -- the cv::Mat points into the
// QImage's storage, so the QImage must outlive the returned cv::Mat.
static cv::Mat qImageToGray(const QImage& img) {
    switch (img.format()) {
        case QImage::Format_Grayscale8: {
            return cv::Mat(img.height(), img.width(), CV_8UC1,
                            (void*)img.bits(), img.bytesPerLine()).clone();
        }
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

// Estimate feature points from a face bbox using standard face proportions.
// Origin (0,0) at top-left of bbox, units in pixels.
static void fillFeaturePoints(Face& f) {
    const qreal x = f.bbox.x();
    const qreal y = f.bbox.y();
    const qreal w = f.bbox.width();
    const qreal h = f.bbox.height();

    f.leftEyeCenter      = QPointF(x + 0.32 * w, y + 0.42 * h);
    f.rightEyeCenter     = QPointF(x + 0.68 * w, y + 0.42 * h);
    f.noseTip            = QPointF(x + 0.50 * w, y + 0.62 * h);
    f.mouthCenter        = QPointF(x + 0.50 * w, y + 0.80 * h);
    f.leftMouthCorner    = QPointF(x + 0.36 * w, y + 0.80 * h);
    f.rightMouthCorner   = QPointF(x + 0.64 * w, y + 0.80 * h);
    f.chin               = QPointF(x + 0.50 * w, y + 0.98 * h);
    f.confidence         = 1.0f;
}

QVector<Face> FaceDetector::detect(const QImage& image, int minSize) const {
    QVector<Face> result;
    if (!isLoaded() || image.isNull()) return result;

    cv::Mat gray = qImageToGray(image);
    if (gray.empty()) return result;

    std::vector<cv::Rect> rects;
    m_impl->cascade.detectMultiScale(
        gray, rects,
        1.1,        // scaleFactor
        3,          // minNeighbors
        0,          // flags
        cv::Size(minSize, minSize)
    );

    result.reserve(int(rects.size()));
    for (const cv::Rect& r : rects) {
        Face f;
        f.bbox = QRectF(qreal(r.x), qreal(r.y), qreal(r.width), qreal(r.height));
        fillFeaturePoints(f);
        result.append(f);
    }
    return result;
}

}  // namespace filters::liquify
