#include "LayerMaskRenderer.h"

#include <QImage>
#include <QPainter>
#include <QtMath>
#include <opencv2/imgproc.hpp>

namespace layers {

cv::Mat LayerMaskRenderer::rasterizeVector(const QVector<QPainterPath>& paths,
                                           const QSize& size) {
    cv::Mat alpha(size.height(), size.width(), CV_8UC1, cv::Scalar(0));
    if (paths.isEmpty()) return alpha;

    // QPainter operates on QImage; build a grayscale QImage, fill paths,
    // copy the bytes back into the cv::Mat.
    QImage img(size, QImage::Format_Grayscale8);
    img.fill(0);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        for (const QPainterPath& path : paths) {
            p.drawPath(path);
        }
    }

    // Copy bytes from QImage to cv::Mat.
    for (int y = 0; y < size.height(); ++y) {
        const uchar* src = img.constScanLine(y);
        uchar* dst = alpha.ptr<uchar>(y);
        std::memcpy(dst, src, size.width() * sizeof(uchar));
    }
    return alpha;
}

cv::Mat LayerMaskRenderer::buildAlpha(const LayerMask& mask, const QSize& size) {
    cv::Mat alpha(size.height(), size.width(), CV_8UC1, cv::Scalar(255));
    if (!mask.hasData()) return alpha;

    if (mask.kind == LayerMask::Vector) {
        alpha = rasterizeVector(mask.vectorPaths, size);
    } else if (mask.kind == LayerMask::Pixel) {
        // Resize mask.pixel to size if dims differ; otherwise copy.
        if (mask.pixel.size() != cv::Size(size.width(), size.height())) {
            cv::Mat resized;
            cv::resize(mask.pixel, resized,
                       cv::Size(size.width(), size.height()), 0, 0,
                       cv::INTER_LINEAR);
            alpha = resized;
        } else {
            alpha = mask.pixel.clone();
        }
    }

    // density multiplier (0..1). Map to 0..255 scale.
    if (mask.density < 1.0) {
        const int keep = int(std::round(255.0 * mask.density));
        for (int y = 0; y < alpha.rows; ++y) {
            uchar* row = alpha.ptr<uchar>(y);
            for (int x = 0; x < alpha.cols; ++x) {
                row[x] = uchar((int(row[x]) * keep) / 255);
            }
        }
    }

    // feather: gaussian blur with kernel derived from feather radius.
    if (mask.feather > 0.0) {
        const int k = std::max(1, int(std::ceil(mask.feather * 2.0)) | 1);
        const double sigma = mask.feather;
        cv::GaussianBlur(alpha, alpha, cv::Size(k, k), sigma, sigma);
    }

    // invert
    if (mask.invert) {
        for (int y = 0; y < alpha.rows; ++y) {
            uchar* row = alpha.ptr<uchar>(y);
            for (int x = 0; x < alpha.cols; ++x) {
                row[x] = 255 - row[x];
            }
        }
    }
    return alpha;
}

bool LayerMaskRenderer::applyAlpha(cv::Mat& layer, const LayerMask& mask) {
    if (!mask.isActive() || layer.empty()) return false;
    // Pixel contract: only 4-channel BGRA has an alpha channel to write.
    //   A 3-channel layer getting here means the caller skipped the
    //   LayerStack normalisation, so it is rejected outright -- better to
    //   do nothing than to quietly degenerate into an RGB multiply (CP-07).
    if (layer.channels() != 4) return false;

    const cv::Mat alpha = buildAlpha(mask,
                                      QSize(layer.cols, layer.rows));
    if (alpha.empty()) return false;

    // out.a = pixel.a * maskCoverage / 255
    std::vector<cv::Mat> ch;
    cv::split(layer, ch);
    cv::multiply(ch[3], alpha, ch[3], 1.0 / 255.0);
    cv::merge(ch, layer);
    return true;
}

}  // namespace layers
