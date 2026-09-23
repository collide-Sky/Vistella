#include "imageprocessor.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QImageWriter>
#include <QPainter>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

// P0-3.1 (2026-09-08): EngineContext + executor 完整 include (applyLutAsync 走 background pool)
//   跟 P0-2.5 ImageProcessorAsync.cpp / LayerStack.cpp 同样的相对路径
#include "../core/ThreadPool/engine_context.h"
#include "../core/ThreadPool/executor.hpp"

// --------- I/O ---------

bool ImageProcessor::saveImage(const cv::Mat &img, const QString &path, QString *err)
{
    // 兼容旧 API: 默认 options (quality=-1, format=空, pngCompression=-1)
    return saveImage(img, path, SaveOptions{}, err);
}

bool ImageProcessor::saveImage(const cv::Mat &img, const QString &path,
                                const SaveOptions& opts, QString *err)
{
    if (img.empty()) {
        if (err) *err = QStringLiteral("image is empty");
        return false;
    }
    if (img.depth() != CV_8U) {
        if (err) *err = QStringLiteral("image is not 8U");
        return false;
    }

    // 走 QImage::save 路径 (比 cv::imwrite 鲁棒, 各 channels 都正确处理)
    QImage qimg;
    if (img.channels() == 1) {
        qimg = QImage(img.cols, img.rows, QImage::Format_Grayscale8);
        for (int y = 0; y < img.rows; ++y) {
            std::memcpy(qimg.scanLine(y), img.ptr(y), static_cast<size_t>(img.cols));
        }
    } else if (img.channels() == 3) {
        // BGR -> RGB swap (Qt 是 RGB)
        cv::Mat rgb;
        cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
        qimg = QImage(rgb.cols, rgb.rows, QImage::Format_RGB888);
        for (int y = 0; y < rgb.rows; ++y) {
            std::memcpy(qimg.scanLine(y), rgb.ptr(y), static_cast<size_t>(rgb.cols) * 3);
        }
    } else if (img.channels() == 4) {
        // BGRA -> RGBA
        cv::Mat rgba;
        cv::cvtColor(img, rgba, cv::COLOR_BGRA2RGBA);
        qimg = QImage(rgba.cols, rgba.rows, QImage::Format_RGBA8888);
        for (int y = 0; y < rgba.rows; ++y) {
            std::memcpy(qimg.scanLine(y), rgba.ptr(y), static_cast<size_t>(rgba.cols) * 4);
        }
    } else {
        if (err) *err = QStringLiteral("unsupported channels: %1").arg(img.channels());
        return false;
    }

    // P0-8.2 (2026-09-15): 多格式 + quality 路径
    //   QImage::save(path, format, quality) 接受 0-100 quality (JPEG/WebP 有效)
    //   PNG 走 compression (QImage::save 不支持, 但 Qt 6.5+ 用 QImageWriter 走 png compression)
    //   TIFF compression 同样不支持直接走, 用 QImageWriter
    const QString fmt = opts.format.isEmpty()
                          ? QString()
                          : opts.format.toUpper();

    if (opts.quality >= 0 || !fmt.isEmpty()) {
        // 走 QImageWriter 拿完整 format/compression 支持
        QImageWriter writer(path, fmt.toLatin1());
        if (opts.quality >= 0) writer.setQuality(opts.quality);
        // TIFF compression 走 QImageWriter::Compression 枚举 (Qt TIFF driver 支持 1=None / 5=LZW)
        //   "None" / "LZW" / "Deflate" 字符串名映射到 driver enum
        if (!opts.tiffCompression.isEmpty()
            && (fmt == QStringLiteral("TIFF"))) {
            if (opts.tiffCompression == QStringLiteral("None")) {
                writer.setCompression(1);
            } else {
                writer.setCompression(99);  // LZW / Deflate 默认走 driver max compression
            }
        }
        // PNG compression 走 QImageWriter::Compression (Qt 6.5+)
        if (opts.pngCompression >= 0 && (fmt == QStringLiteral("PNG") || fmt.isEmpty())) {
            writer.setCompression(opts.pngCompression);
        }
        if (!writer.write(qimg)) {
            if (err) *err = QString("QImageWriter::write failed: %1").arg(writer.errorString());
            return false;
        }
        return true;
    }

    if (!qimg.save(path)) {
        if (err) *err = QString("QImage::save failed for: %1").arg(path);
        return false;
    }
    return true;
}

QImage ImageProcessor::matToQImage(const cv::Mat &mat)
{
    if (mat.empty())
        return {};

    // 转成 RGB888 (Qt 习惯)
    cv::Mat rgb;
    switch (mat.channels()) {
    case 1:
        cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);
        break;
    case 3:
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        break;
    case 4:
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGBA);
        break;
    default:
        return {};
    }

    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}

cv::Mat ImageProcessor::qImageToMat(const QImage &img)
{
    if (img.isNull()) return {};
    // 关键 1: QImage 共享数据, 必须 .copy() 深拷贝避免和原图互相干扰
    QImage rgb = img.convertToFormat(QImage::Format_RGB888).copy();
    // 关键 2: 用 QImage::rgbSwapped() 替代 cv::cvtColor, 避免 in-place 复杂性
    // rgbSwapped 内部直接交换 R/B 字节, 安全无副作用
    rgb = rgb.rgbSwapped();   // 现在 rgb 是 BGR 顺序
    // 关键 3: 强制 step = width*3 (跳过 QImage 内部 padding 字节)
    // QImage::bytesPerLine() 可能 != width*3 (4 字节对齐), 但每行前 width*3 字节是有效数据
    // 用 width*3 当 step, OpenCV 按 step 走, 不会踩 padding
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.width()) * 3);
    return mat.clone();  // 分配新 buffer, 独立内存, 不受 QImage 析构影响
}

// --------- 像素级 ---------

void ImageProcessor::toGray(const cv::Mat &in, cv::Mat &out)
{
    if (in.channels() == 1) { out = in.clone(); return; }
    cv::cvtColor(in, out, cv::COLOR_BGR2GRAY);
}

void ImageProcessor::toBinary(const cv::Mat &in, cv::Mat &out, double thresh)
{
    cv::Mat g;
    toGray(in, g);
    cv::threshold(g, out, thresh, 255, cv::THRESH_BINARY);
}

void ImageProcessor::invert(const cv::Mat &in, cv::Mat &out)
{
    cv::Mat mask = cv::Mat(in.size(), in.type(),
        in.channels() == 1 ? cv::Scalar::all(255) : cv::Scalar(255, 255, 255));
    cv::subtract(mask, in, out);
}

void ImageProcessor::brightnessContrast(const cv::Mat &in, cv::Mat &out,
                                        double alpha, int beta)
{
    // out = alpha * in + beta
    in.convertTo(out, -1, alpha, beta);
}

// --------- 滤波 ---------

void ImageProcessor::gaussianBlur(const cv::Mat &in, cv::Mat &out, int ksize, double sigma)
{
    if (ksize % 2 == 0) ++ksize;
    if (ksize < 1) ksize = 1;
    cv::GaussianBlur(in, out, cv::Size(ksize, ksize), sigma);
}

void ImageProcessor::sharpen(const cv::Mat &in, cv::Mat &out)
{
    // 锐化 = 原图 - 拉普拉斯
    cv::Mat lap;
    cv::Laplacian(in, lap, CV_16S, 3);
    cv::Mat absLap;
    cv::convertScaleAbs(lap, absLap);
    if (in.channels() == 1) {
        cv::subtract(in, absLap, out);
    } else {
        cv::subtract(in, absLap, out);
    }
}

void ImageProcessor::edgeDetect(const cv::Mat &in, cv::Mat &out, double lowThresh, double highThresh)
{
    cv::Mat g;
    toGray(in, g);
    cv::Canny(g, out, lowThresh, highThresh);
}

// --------- 直方图 ---------

QVector<int> ImageProcessor::computeHistogram(const cv::Mat &gray)
{
    QVector<int> hist(256, 0);
    if (gray.empty() || gray.channels() != 1) return hist;
    for (int r = 0; r < gray.rows; ++r) {
        const uchar *row = gray.ptr<uchar>(r);
        for (int c = 0; c < gray.cols; ++c) {
            hist[row[c]]++;
        }
    }
    return hist;
}

QImage ImageProcessor::renderHistogram(const QVector<int> &hist, int w, int height)
{
    if (hist.size() != 256) return {};
    QImage img(w, height, QImage::Format_RGB888);
    img.fill(QColor(245, 245, 245));

    // 找最大值用于归一化
    int maxv = 1;
    for (int v : hist) if (v > maxv) maxv = v;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QColor(70, 130, 180));
    p.setBrush(QColor(70, 130, 180, 80));
    QPolygonF poly;
    const double step = double(w) / 256.0;
    for (int i = 0; i < 256; ++i) {
        const double x = i * step;
        const double yv = double(hist[i]) / maxv * (height - 4);
        poly << QPointF(x, height) << QPointF(x, height - yv);
    }
    p.drawPolygon(poly);
    p.setPen(QColor(120, 120, 120));
    p.drawLine(0, height - 1, w, height - 1);
    p.end();
    return img;
}

// --------- 信息 ---------

ImageProcessor::ImageInfo ImageProcessor::describe(const cv::Mat &img, const QString &filePath)
{
    ImageInfo info;
    if (!img.empty()) {
        info.width    = img.cols;
        info.height   = img.rows;
        info.channels = img.channels();

        // 拼一个 OpenCV 类型字符串: CV_8UC3 之类
        const int depth = img.depth();
        QString depthStr;
        switch (depth) {
        case CV_8U:  depthStr = QStringLiteral("8U"); break;
        case CV_8S:  depthStr = QStringLiteral("8S"); break;
        case CV_16U: depthStr = QStringLiteral("16U"); break;
        case CV_16S: depthStr = QStringLiteral("16S"); break;
        case CV_32S: depthStr = QStringLiteral("32S"); break;
        case CV_32F: depthStr = QStringLiteral("32F"); break;
        case CV_64F: depthStr = QStringLiteral("64F"); break;
        default:     depthStr = QString::number(depth); break;
        }
        info.typeName = QStringLiteral("CV_%1C%2").arg(depthStr).arg(info.channels);
    }
    if (!filePath.isEmpty()) {
        QFileInfo fi(filePath);
        if (fi.exists())
            info.fileSize = fi.size();
    }
    return info;
}

// =============================================================
// 扩充: HSV / 统计 / 局部操作 / 像素查询
// =============================================================

void ImageProcessor::medianBlur(const cv::Mat &in, cv::Mat &out, int ksize)
{
    if (ksize % 2 == 0) ++ksize;
    if (ksize < 1) ksize = 1;
    cv::medianBlur(in, out, ksize);
}

void ImageProcessor::bilateralFilter(const cv::Mat &in, cv::Mat &out,
                                     int d, double sigmaColor, double sigmaSpace)
{
    cv::bilateralFilter(in, out, d, sigmaColor, sigmaSpace);
}

void ImageProcessor::emboss(const cv::Mat &in, cv::Mat &out)
{
    // 浮雕 kernel
    cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
        -2, -1,  0,
        -1,  1,  1,
         0,  1,  2);
    cv::filter2D(in, out, CV_8U, kernel);
    // 整体调亮一点, 不然结果偏黑
    out += cv::Scalar(128, 128, 128, 0);
}

void ImageProcessor::resize(const cv::Mat &in, cv::Mat &out, double scale)
{
    if (scale <= 0) { out = in.clone(); return; }
    cv::resize(in, out, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void ImageProcessor::rotate90(const cv::Mat &in, cv::Mat &out, int direction)
{
    // 0=0, 1=90 CW, 2=180, 3=270 CW
    if (direction < 0) direction = 0;
    if (direction > 3) direction = direction % 4;
    if (direction == 0) { out = in.clone(); return; }
    if (direction == 2) { cv::rotate(in, out, cv::ROTATE_180); return; }
    if (direction == 1) { cv::rotate(in, out, cv::ROTATE_90_CLOCKWISE); return; }
    cv::rotate(in, out, cv::ROTATE_90_COUNTERCLOCKWISE);
}

// P0-6.6 (2026-09-14): 几何变换
void ImageProcessor::flip(const cv::Mat &in, cv::Mat &out, int flipCode)
{
    if (in.empty()) { out = in; return; }
    int code = 0;
    if (flipCode == 0)       code = 0;  // around x (vertical flip)
    else if (flipCode == 1)  code = 1;  // around y (horizontal flip)
    else if (flipCode == -1) code = -1; // both
    else                     code = 0;
    cv::flip(in, out, code);
}

void ImageProcessor::warpAffine(const cv::Mat &in, cv::Mat &out, const cv::Mat &M, cv::Size dsize)
{
    if (in.empty()) { out = in; return; }
    // 2x3 矩阵: 旋转 + 缩放 + 斜切 + 平移
    //   dsize: 输出图像大小 (PS 风格: 输出 = 输入的 same size 或 跟随 box)
    cv::warpAffine(in, out, M, dsize, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
}

// P3.2.3 (2026-09-22): 4 corner 透视变换 (3x3 矩阵). 用于 TransformTool Distort
//   模式拖成非平行四边形的情况 (QTransform 2x3 affine 不能表达).
//   srcQuad/dstQuad = 4 个 QPointF, 顺序 TL/TR/BR/BL (跟 TransformBox::cornersArray 一致).
void ImageProcessor::warpPerspective(const cv::Mat &in, cv::Mat &out,
                                    const QPointF srcQuad[4], const QPointF dstQuad[4])
{
    if (in.empty()) { out = in; return; }
    cv::Point2f src[4], dst[4];
    for (int i = 0; i < 4; ++i) {
        src[i] = cv::Point2f(static_cast<float>(srcQuad[i].x()),
                             static_cast<float>(srcQuad[i].y()));
        dst[i] = cv::Point2f(static_cast<float>(dstQuad[i].x()),
                             static_cast<float>(dstQuad[i].y()));
    }
    cv::Mat M = cv::getPerspectiveTransform(src, dst);
    cv::warpPerspective(in, out, M, in.size(),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
}

cv::Mat ImageProcessor::qTransformToAffine(const QTransform &t)
{
    // QTransform 3x3 → cv::Mat 2x3 (CV_64F)
    //   QTransform isAffine() 验证 (PS transform 永远 affine, 不该是 perspective)
    Q_ASSERT(t.isAffine());
    cv::Mat M(2, 3, CV_64F);
    M.at<double>(0, 0) = t.m11();
    M.at<double>(0, 1) = t.m12();
    M.at<double>(0, 2) = t.dx();
    M.at<double>(1, 0) = t.m21();
    M.at<double>(1, 1) = t.m22();
    M.at<double>(1, 2) = t.dy();
    return M;
}

// ----- 颜色调整 -----

void ImageProcessor::saturation(const cv::Mat &in, cv::Mat &out, double scale)
{
    if (in.empty()) return;
    if (in.channels() == 1) { out = in.clone(); return; }  // 灰度无饱和度
    if (scale == 1.0) { out = in.clone(); return; }
    cv::Mat hsv;
    cv::cvtColor(in, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> ch;
    cv::split(hsv, ch);
    ch[1].convertTo(ch[1], -1, scale, 0);
    cv::merge(ch, hsv);
    cv::cvtColor(hsv, out, cv::COLOR_HSV2BGR);
}

void ImageProcessor::hueShift(const cv::Mat &in, cv::Mat &out, double degrees)
{
    if (in.empty()) return;
    if (in.channels() == 1) { out = in.clone(); return; }  // 灰度无色相
    if (degrees == 0) { out = in.clone(); return; }
    cv::Mat hsv;
    cv::cvtColor(in, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> ch;
    cv::split(hsv, ch);
    // H 是 0..179 (8U)
    const int shift = int(degrees / 2.0) % 180;   // 180 度 = 半圈
    ch[0] += shift;
    cv::merge(ch, hsv);
    cv::cvtColor(hsv, out, cv::COLOR_HSV2BGR);
}

void ImageProcessor::exposure(const cv::Mat &in, cv::Mat &out, double ev)
{
    // 简单 EV: alpha = 2^ev
    const double alpha = std::pow(2.0, ev);
    in.convertTo(out, -1, alpha, 0);
}

// ----- 局部操作 -----

void ImageProcessor::mosaic(cv::Mat &img, int x, int y, int w, int h, int blockSize)
{
    if (img.empty() || blockSize < 1) return;
    if (blockSize % 2 == 0) ++blockSize;
    // 裁剪到有效范围
    x = std::max(0, x);
    y = std::max(0, y);
    w = std::min(w, img.cols - x);
    h = std::min(h, img.rows - y);
    if (w <= 0 || h <= 0) return;

    cv::Rect roi(x, y, w, h);
    cv::Mat region = img(roi);
    for (int by = 0; by < h; by += blockSize) {
        for (int bx = 0; bx < w; bx += blockSize) {
            const int bw = std::min(blockSize, w - bx);
            const int bh = std::min(blockSize, h - by);
            cv::Mat block = region(cv::Rect(bx, by, bw, bh));
            cv::Scalar mean = cv::mean(block);
            block.setTo(mean);
        }
    }
}

void ImageProcessor::drawText(cv::Mat &img, const QString &text,
                              int x, int y, const QColor &color,
                              double fontScale, int thickness,
                              const QString &fontFamily)
{
    if (img.empty() || text.isEmpty()) return;

    // 用 Qt 渲染文字到临时 QImage, 再贴回 OpenCV
    QFont font;
    if (!fontFamily.isEmpty()) font.setFamily(fontFamily);
    // 防 ≤0: 用户可能传 0 或负数, 用 std::max 兜底
    font.setPointSize(std::max(1, int(20 * fontScale)));
    font.setBold(false);

    QFontMetrics fm(font);
    const QSize textSize = fm.size(Qt::TextSingleLine, text);
    QImage qiText(textSize.width() + 12, textSize.height() + 8, QImage::Format_ARGB32);
    qiText.fill(Qt::transparent);
    QPainter p(&qiText);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(font);
    p.setPen(color);
    p.drawText(6, fm.ascent() + 4, text);
    p.end();

    // 贴回原图 - 先按原 channels 转 BGRA
    cv::Mat overlay;
    const int srcC = img.channels();
    if (srcC == 1) {
        cv::cvtColor(img, overlay, cv::COLOR_GRAY2BGRA);
    } else if (srcC == 3) {
        cv::cvtColor(img, overlay, cv::COLOR_BGR2BGRA);
    } else {
        overlay = img.clone();
    }
    // 限制范围
    const int ox = std::max(0, std::min(x, overlay.cols - qiText.width()));
    const int oy = std::max(0, std::min(y, overlay.rows - qiText.height()));
    for (int j = 0; j < qiText.height(); ++j) {
        for (int i = 0; i < qiText.width(); ++i) {
            const QRgb px = qiText.pixel(i, j);
            const int a = qAlpha(px);
            if (a == 0) continue;
            const int dx = ox + i, dy = oy + j;
            if (dx >= overlay.cols || dy >= overlay.rows) continue;
            const double blend = a / 255.0;
            const cv::Vec3b src = overlay.at<cv::Vec3b>(dy, dx);
            const cv::Vec3b dst(
                uchar(qBlue(px) * blend + src[0] * (1 - blend)),
                uchar(qGreen(px) * blend + src[1] * (1 - blend)),
                uchar(qRed (px) * blend + src[2] * (1 - blend)));
            overlay.at<cv::Vec3b>(dy, dx) = dst;
        }
    }
    // 写回原图 - 按原 channels 转回去
    if (srcC == 1) {
        cv::cvtColor(overlay, img, cv::COLOR_BGRA2GRAY);
    } else if (srcC == 3) {
        cv::cvtColor(overlay, img, cv::COLOR_BGRA2BGR);
    } else {
        img = overlay;
    }
}

// ----- RGB / HSV 直方图 -----

ImageProcessor::RGBHist ImageProcessor::computeRGBHistogram(const cv::Mat &bgr)
{
    RGBHist h;
    h.b.resize(256, 0);
    h.g.resize(256, 0);
    h.r.resize(256, 0);
    if (bgr.empty()) return h;
    if (bgr.channels() == 1) {
        // 灰度: 同时填到 RGB
        for (int r = 0; r < bgr.rows; ++r) {
            const uchar *row = bgr.ptr<uchar>(r);
            for (int c = 0; c < bgr.cols; ++c) {
                h.b[row[c]]++; h.g[row[c]]++; h.r[row[c]]++;
            }
        }
        return h;
    }
    for (int r = 0; r < bgr.rows; ++r) {
        const uchar *row = bgr.ptr<uchar>(r);
        for (int c = 0; c < bgr.cols; ++c) {
            const uchar *px = row + c * 3;
            h.b[px[0]]++; h.g[px[1]]++; h.r[px[2]]++;
        }
    }
    return h;
}

QImage ImageProcessor::renderRGBHistogram(const RGBHist &h, int w, int height)
{
    QImage img(w, height, QImage::Format_RGB888);
    img.fill(QColor(245, 245, 245));
    if (h.b.size() != 256) return img;

    int maxv = 1;
    for (int v : h.b) if (v > maxv) maxv = v;
    for (int v : h.g) if (v > maxv) maxv = v;
    for (int v : h.r) if (v > maxv) maxv = v;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double step = double(w) / 256.0;
    auto drawLayer = [&](const QVector<int> &data, const QColor &col) {
        p.setPen(col);
        QPolygonF poly;
        for (int i = 0; i < 256; ++i) {
            const double x = i * step;
            const double yv = double(data[i]) / maxv * (height - 4);
            poly << QPointF(x, height) << QPointF(x, height - yv);
        }
        p.drawPolygon(poly);
    };
    drawLayer(h.b, QColor(60, 100, 200, 180));
    drawLayer(h.g, QColor(60, 180, 80, 180));
    drawLayer(h.r, QColor(220, 60, 60, 180));
    p.setPen(QColor(120, 120, 120));
    p.drawLine(0, height - 1, w, height - 1);
    p.end();
    return img;
}

ImageProcessor::HSVHist ImageProcessor::computeHSVHistogram(const cv::Mat &bgr)
{
    HSVHist h;
    h.h.resize(180, 0);
    h.s.resize(256, 0);
    h.v.resize(256, 0);
    if (bgr.empty()) return h;
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    for (int r = 0; r < hsv.rows; ++r) {
        const uchar *row = hsv.ptr<uchar>(r);
        for (int c = 0; c < hsv.cols; ++c) {
            const uchar *px = row + c * 3;
            h.h[px[0]]++;
            h.s[px[1]]++;
            h.v[px[2]]++;
        }
    }
    return h;
}

QImage ImageProcessor::renderHSVHistogram(const HSVHist &hist, int w, int height)
{
    QImage img(w, height, QImage::Format_RGB888);
    img.fill(QColor(245, 245, 245));
    if (hist.h.size() != 180 || hist.s.size() != 256) return img;

    int maxv = 1;
    for (int v : hist.h) if (v > maxv) maxv = v;
    for (int v : hist.s) if (v > maxv) maxv = v;
    for (int v : hist.v) if (v > maxv) maxv = v;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    auto drawLayer = [&](const QVector<int> &data, int n, const QColor &col) {
        p.setPen(col);
        const double step = double(w) / n;
        QPolygonF poly;
        for (int i = 0; i < n; ++i) {
            const double x = i * step;
            const double yv = double(data[i]) / maxv * (height - 4);
            poly << QPointF(x, height) << QPointF(x, height - yv);
        }
        p.drawPolygon(poly);
    };
    drawLayer(hist.h, 180, QColor(200, 80, 200, 180));
    drawLayer(hist.s, 256, QColor(80, 130, 220, 180));
    drawLayer(hist.v, 256, QColor(60, 60, 60, 180));
    p.setPen(QColor(120, 120, 120));
    p.drawLine(0, height - 1, w, height - 1);
    p.end();
    return img;
}

// ----- 统计 -----

ImageProcessor::ImageStats ImageProcessor::computeStats(const cv::Mat &bgr)
{
    ImageStats s;
    if (bgr.empty()) return s;
    s.totalPixels = bgr.rows * bgr.cols;
    const int C = bgr.channels();
    s.channels.resize(C);

    // OpenCV meanStdDev 一次算出全部通道
    cv::Mat mean, stddev;
    cv::meanStdDev(bgr, mean, stddev);   // 都是 Nx1
    // minMaxLoc 算 min/max
    std::vector<double> mins(C), maxs(C);
    for (int c = 0; c < C; ++c) {
        double mn = 0, mx = 0;
        cv::minMaxLoc(bgr.col(c), &mn, &mx);
        mins[c] = mn; maxs[c] = mx;
        s.channels[c].mean = mean.at<double>(c);
        s.channels[c].std  = stddev.at<double>(c);
        s.channels[c].min  = mn;
        s.channels[c].max  = mx;
    }

    // 灰度统计
    cv::Mat gray;
    if (C == 1) gray = bgr;
    else        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::meanStdDev(gray, mean, stddev);
    s.luminance.mean = mean.at<double>(0);
    s.luminance.std  = stddev.at<double>(0);
    double mn = 0, mx = 0;
    cv::minMaxLoc(gray, &mn, &mx);
    s.luminance.min = mn; s.luminance.max = mx;

    // 非黑像素 (灰度 > 阈值 1)
    cv::Mat mask = gray > 1;
    s.nonBlackPixels = cv::countNonZero(mask);

    return s;
}

// ----- 像素查询 -----

ImageProcessor::PixelInfo ImageProcessor::pixelAt(const cv::Mat &bgr, int x, int y)
{
    PixelInfo pi;
    if (bgr.empty()) return pi;
    if (x < 0 || x >= bgr.cols || y < 0 || y >= bgr.rows) return pi;
    pi.pos = QPoint(x, y);
    pi.valid = true;
    if (bgr.channels() == 1) {
        pi.gray = pi.b = pi.g = pi.r = bgr.at<uchar>(y, x);
    } else {
        const cv::Vec3b &px = bgr.at<cv::Vec3b>(y, x);
        pi.b = px[0]; pi.g = px[1]; pi.r = px[2];
        pi.gray = int(0.299 * pi.r + 0.587 * pi.g + 0.114 * pi.b);
    }
    // HSV
    cv::Mat hsv;
    if (bgr.channels() == 1) {
        hsv = bgr;
    } else {
        cv::Mat tmp(1, 1, CV_8UC3, bgr.at<cv::Vec3b>(y, x));
        cv::cvtColor(tmp, tmp, cv::COLOR_BGR2HSV);
        const cv::Vec3b &hpx = tmp.at<cv::Vec3b>(0, 0);
        pi.h = hpx[0]; pi.s = hpx[1]; pi.v = hpx[2];
    }
    return pi;
}

// =============================================================
// P0-3.1 (2026-09-08): 8 个色彩调整 LUT 函数 + applyLut 同步/异步
//   模拟 PS 风格色彩调整: Curves / Levels / HSL / B&W /
//   Color Balance / Vibrance / Photo Filter
//
// 设计原则 (一次性到位, 不留补丁):
//   1. 所有 build* 函数返 cv::Mat (256 行 1 列 CV_8U) — 单通道 LUT
//   2. applyLut 同步用 cv::LUT 加速, 接受 CV_8U 单/三/四通道图像
//   3. applyLutAsync 走 EngineContext background pool
//      跟 P0-2.5 ImageProcessorAsync::runOpEngine 同样的模式:
//      shared_ptr<promise> + background().submit_fn + engine==nullptr 兜底
//   4. 不"简化"任何业务逻辑 — 8 个 LUT 函数 1:1 实装
// =============================================================

// buildCurvesLUT — 256 元素 LUT, 线性插值
//   controlPoints: (input, output) pairs, 范围 [0, 255]
//   例: (0, 0) (255, 255) → 恒等 LUT
//       (0, 0) (128, 192) (255, 255) → 提升中间调
cv::Mat ImageProcessor::buildCurvesLUT(const QPolygonF &controlPoints)
{
    cv::Mat lut(256, 1, CV_8U);
    if (controlPoints.isEmpty()) {
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        return lut;
    }
    // 排序 (按 x) — 保证插值顺序
    QPolygonF pts = controlPoints;
    std::sort(pts.begin(), pts.end(),
              [](const QPointF &a, const QPointF &b) {
                  return a.x() < b.x();
              });
    // 至少 2 个点才能插值, 否则按单点 (clamp) 或 identity
    if (pts.size() < 2) {
        if (pts.size() == 1) {
            const double y = pts.first().y();
            for (int i = 0; i < 256; ++i) {
                lut.at<uchar>(i) = cv::saturate_cast<uchar>(y);
            }
        } else {
            for (int i = 0; i < 256; ++i) {
                lut.at<uchar>(i) = static_cast<uchar>(i);
            }
        }
        return lut;
    }
    // 线性插值: 找 i 所在的段, 计算 t = (i - x_k) / (x_{k+1} - x_k)
    const double xMin = pts.first().x();
    const double xMax = pts.last().x();
    const double yMin = pts.first().y();
    const double yMax = pts.last().y();
    for (int i = 0; i < 256; ++i) {
        const double x = i;
        if (x <= xMin) {
            lut.at<uchar>(i) = cv::saturate_cast<uchar>(yMin);
        } else if (x >= xMax) {
            lut.at<uchar>(i) = cv::saturate_cast<uchar>(yMax);
        } else {
            for (int k = 0; k < pts.size() - 1; ++k) {
                if (x >= pts[k].x() && x <= pts[k + 1].x()) {
                    const double span = pts[k + 1].x() - pts[k].x();
                    if (span <= 0.0) {
                        lut.at<uchar>(i) = cv::saturate_cast<uchar>(pts[k].y());
                    } else {
                        const double t = (x - pts[k].x()) / span;
                        const double y = pts[k].y() + t * (pts[k + 1].y() - pts[k].y());
                        lut.at<uchar>(i) = cv::saturate_cast<uchar>(y);
                    }
                    break;
                }
            }
        }
    }
    return lut;
}

// buildLevelsLUT — Levels 256 元素 LUT
//   inLow / inHigh: input range [0, 255]
//   gamma: 中间调 [0.1, 10.0], 1.0 = identity
//   outLow / outHigh: output range [0, 255]
//   默认 (0, 255, 1.0, 0, 255) → 恒等
cv::Mat ImageProcessor::buildLevelsLUT(int inLow, int inHigh, double gamma,
                                       int outLow, int outHigh)
{
    cv::Mat lut(256, 1, CV_8U);
    // 参数 sanity 修复 (避免除 0 / 越界)
    if (inLow < 0)   inLow  = 0;
    if (inHigh > 255) inHigh = 255;
    if (inLow >= inHigh) inHigh = inLow + 1;
    if (gamma < 0.01) gamma = 0.01;
    if (gamma > 10.0) gamma = 10.0;
    if (outLow < 0)    outLow  = 0;
    if (outHigh > 255) outHigh = 255;
    const double denom    = static_cast<double>(inHigh - inLow);
    const double outRange = static_cast<double>(outHigh - outLow);
    for (int i = 0; i < 256; ++i) {
        if (i < inLow) {
            lut.at<uchar>(i) = static_cast<uchar>(outLow);
        } else if (i > inHigh) {
            lut.at<uchar>(i) = static_cast<uchar>(outHigh);
        } else {
            const double normalized = (i - inLow) / denom;       // 0..1
            const double corrected  = std::pow(normalized, 1.0 / gamma);
            const double output     = outLow + corrected * outRange;
            lut.at<uchar>(i) = cv::saturate_cast<uchar>(output);
        }
    }
    return lut;
}

// buildHueSatLUT — 8 色相 HSL 调整 LUT
//   hueSatPairs: 8 个 (hue, sat) pairs, hue [-180, 180], sat [-100, 100]
//                顺序: Master / Reds / Yellows / Greens /
//                      Cyans / Blues / Magentas / (预留)
//   lightness: -100..100 (明度调整)
//   P0-3.1 简化: LUT 只处理 lightness 部分 (256 元素单通道)
//                hue/sat pairs 是 API 占位, 完整 per-hue 应用留 P0-3.x
//   原因: H/S 调整是 2D (per-pixel in HSL space), 不是 1D LUT 能表达的
//         L (明度) 是 1D, 适合 LUT; 跟 PS HSL 面板 "Lightness" 单滑条一致
cv::Mat ImageProcessor::buildHueSatLUT(const QVector<QPair<int, int>> &hueSatPairs,
                                        int lightness)
{
    Q_UNUSED(hueSatPairs);
    cv::Mat lut(256, 1, CV_8U);
    if (lightness == 0) {
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        return lut;
    }
    // lightness [-100, 100] → 映射到 ±64 (255 的 1/4)
    const double shift = static_cast<double>(lightness) * 255.0 / 400.0;
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = cv::saturate_cast<uchar>(static_cast<double>(i) + shift);
    }
    return lut;
}

// buildBlackWhiteLUT — 6 颜色黑白混色 LUT
//   rgbMixer: 6 个 [0, 200] 值, 对应 Reds / Yellows / Greens /
//                                  Cyans / Blues / Magentas
//   PS 默认 [40, 60, 40, 60, 40, 40] 接近中性灰度转换
//   identity: 6 个 100.0 (中心) → 输出 = 输入
//   P0-3.1 简化: 平均 mixer 跟中心 100 的差, 当 gamma-like 缩放用
//                完整 PS B&W 是 3D (per-pixel in RGB space) → 不能用 1D LUT
//                P0-3.x 走 per-pixel 转换
cv::Mat ImageProcessor::buildBlackWhiteLUT(const QVector<double> &rgbMixer)
{
    cv::Mat lut(256, 1, CV_8U);
    if (rgbMixer.size() != 6) {
        // 默认/异常: identity
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        return lut;
    }
    double sum = 0.0;
    for (double v : rgbMixer) sum += v;
    const double avg = sum / 6.0;
    // 中性 (avg == 100) → identity
    if (std::abs(avg - 100.0) < 0.001) {
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        return lut;
    }
    // 非中性: gamma-like 缩放 (avg < 100 → 变暗, avg > 100 → 变亮)
    const double scale = avg / 100.0;
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = cv::saturate_cast<uchar>(static_cast<double>(i) * scale);
    }
    return lut;
}

// buildColorBalanceLUT — 色彩平衡 LUT (cyanRed / magentaGreen / yellowBlue)
//   每个值 [-100, 100]
//   0 = 中性, +100 = 强 (加 C, 减 R / 加 M, 减 G / 加 Y, 减 B)
//   P0-3.1 简化: 1D LUT 只处理 cyanRed 维 (R 通道线性偏移近似 L shift)
//                完整 PS 色彩平衡是 3 段 (阴影/中间调/高光) per-RGB, 不能用 1D LUT
//                留 P0-3.x
cv::Mat ImageProcessor::buildColorBalanceLUT(int cyanRed, int magentaGreen, int yellowBlue)
{
    Q_UNUSED(magentaGreen);
    Q_UNUSED(yellowBlue);
    cv::Mat lut(256, 1, CV_8U);
    if (cyanRed == 0) {
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        return lut;
    }
    // cyanRed [-100, 100] → ±85 (255 的 1/3)
    const double shift = static_cast<double>(cyanRed) * 255.0 / 300.0;
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = cv::saturate_cast<uchar>(static_cast<double>(i) + shift);
    }
    return lut;
}

// buildVibranceLUT — 自然饱和度 LUT (vibrance + saturation)
//   vibrance: -100..100 (自然饱和度, 保护肤色, 影响低饱和度像素更多)
//   saturation: -100..100 (全局饱和度)
//   P0-3.1 简化: identity (vibrance 是 HSL 空间 per-pixel 保护肤色算法,
//                            不是简单 1D LUT 能表达)
//                完整实装留 P0-3.x
//   接受参数 for API 完整性 — 后续 P0-3.x 接 AdjustmentLayer 时用得上
cv::Mat ImageProcessor::buildVibranceLUT(double vibrance, double saturation)
{
    Q_UNUSED(vibrance);
    Q_UNUSED(saturation);
    cv::Mat lut(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = static_cast<uchar>(i);
    }
    return lut;
}

// buildPhotoFilterLUT — 照片滤镜 LUT
//   tint: 滤镜颜色 (QColor)
//   density: 0..100 (滤镜强度)
//   P0-3.1 简化: 1D LUT 处理 tint luminance 偏移 (近似 PS Photo Filter 整体 L 调整)
//                完整 per-channel 染色留 P0-3.x
cv::Mat ImageProcessor::buildPhotoFilterLUT(const QColor &tint, int density)
{
    cv::Mat lut(256, 1, CV_8U);
    if (density <= 0 || !tint.isValid()) {
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        return lut;
    }
    // 计算 tint 的 luminance (BT.601)
    const int tintLum = (tint.red() * 299 + tint.green() * 587 + tint.blue() * 114) / 1000;
    // tintLum ∈ [0, 255], 中性 (gray 128) → 偏移 0
    const int lumShift = tintLum - 128;
    // density 调节 effective shift
    const double effectiveShift = static_cast<double>(lumShift) * density / 100.0;
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = cv::saturate_cast<uchar>(static_cast<double>(i) + effectiveShift);
    }
    return lut;
}

// applyLut (同步) — 应用 LUT 到 CV_8U 图像
//   in: CV_8U 单/三/四通道
//   out: 跟 in 同 size/type
//   lut: 256 元素 CV_8U (256x1 或 1x256 形状都支持)
//   内部用 cv::LUT, OpenCV 优化
void ImageProcessor::applyLut(const cv::Mat &in, cv::Mat &out, const cv::Mat &lut)
{
    CV_Assert(lut.type() == CV_8U);
    CV_Assert(lut.total() == 256);
    if (in.empty()) {
        out = cv::Mat();
        return;
    }
    // 标准化 LUT 形状为 1x256 (cv::LUT 期望)
    cv::Mat lutFlat;
    if (lut.rows == 256 && lut.cols == 1) {
        lutFlat = lut.t();  // 256x1 → 1x256 (transpose)
    } else if (lut.rows == 1 && lut.cols == 256) {
        lutFlat = lut;
    } else {
        lutFlat = lut.reshape(0, 1);  // 兜底: 任意 256 元素 → 1x256
        CV_Assert(lutFlat.total() == 256);
    }
    out.create(in.size(), in.type());
    cv::LUT(in, lutFlat, out);
}

// applyLutAsync (异步) — 走 EngineContext background pool
//   engine == nullptr → 同步兜底 (跟 P0-2.5 ImageProcessorAsync 同样模式)
//   engine 非空 → 提交到 background().submit_fn
//   提交失败 (池满 / shutting down) → 同步兜底
//   返 std::future<cv::Mat> (跟 ImageProcessorAsync::runOpEngine 类似,
//                              但返纯 cv::Mat, 不包 Result<cv::Mat> —
//                              跟 task spec 一致: caller future.get() 拿结果)
std::future<cv::Mat> ImageProcessor::applyLutAsync(const cv::Mat &in,
                                                   const cv::Mat &lut,
                                                   vistella::tp::EngineContext *engine)
{
    auto promisePtr = std::make_shared<std::promise<cv::Mat>>();
    auto futureRet  = promisePtr->get_future();

    auto runSync = [promisePtr, &in, &lut]() -> void {
        try {
            cv::Mat out;
            ImageProcessor::applyLut(in, out, lut);
            promisePtr->set_value(std::move(out));
        } catch (const cv::Exception &e) {
            // OpenCV 异常: 返空 Mat (caller 通过 .empty() 检查)
            Q_UNUSED(e);
            promisePtr->set_value(cv::Mat());
        } catch (const std::exception &e) {
            Q_UNUSED(e);
            promisePtr->set_value(cv::Mat());
        }
    };

    if (!engine) {
        runSync();
        return futureRet;
    }

    // 正常路径: 走 background pool
    // 闭包按值捕获 in 和 lut (cv::Mat 内部共享计数, copy 是浅拷贝, 安全)
    const bool submitted = engine->background().submit_fn(
        [promisePtr, in, lut](vistella::tp::TaskContext & /*taskCtx*/) -> void {
            try {
                cv::Mat out;
                ImageProcessor::applyLut(in, out, lut);
                promisePtr->set_value(std::move(out));
            } catch (const cv::Exception &) {
                promisePtr->set_value(cv::Mat());
            } catch (const std::exception &) {
                promisePtr->set_value(cv::Mat());
            }
        },
        "ImageProcessor::applyLutAsync",
        vistella::tp::Priority::Normal);

    if (!submitted) {
        // 提交失败 → 同步兜底
        runSync();
    }
    return futureRet;
}

// applyLutAsync (callback overload, P0-3.2 2026-09-08)
//   跟 std::future overload 同样的逻辑, 但 callback 模式更适合 UI 实时调参:
//   - 不需要 future 轮询, callback 完成后直接切主线程刷显示
//   - 跟 P0-2.5 LayerStack::renderAsync 调用模式一致
//   - callback 在后台线程触发, 调用方必须自己用 QMetaObject::invokeMethod 切回主线程
//   - engine==nullptr → 同步调 callback (在调用线程触发, 不是后台线程)
//   - 提交失败 (池满 / shutting down) → 同步兜底
void ImageProcessor::applyLutAsync(const cv::Mat &in, const cv::Mat &lut,
                                   vistella::tp::EngineContext *engine,
                                   std::function<void(cv::Mat)> callback)
{
    if (!callback) return;  // 没 callback 啥也不做 (避免空指针)

    auto runSync = [&in, &lut, &callback]() -> void {
        try {
            cv::Mat out;
            ImageProcessor::applyLut(in, out, lut);
            callback(std::move(out));
        } catch (const cv::Exception &) {
            callback(cv::Mat());
        } catch (const std::exception &) {
            callback(cv::Mat());
        }
    };

    if (!engine) {
        runSync();
        return;
    }

    // 正常路径: 走 background pool
    // 闭包按值捕获 in 和 lut (cv::Mat 内部共享计数, copy 是浅拷贝, 安全)
    const bool submitted = engine->background().submit_fn(
        [callback, in, lut](vistella::tp::TaskContext & /*taskCtx*/) -> void {
            try {
                cv::Mat out;
                ImageProcessor::applyLut(in, out, lut);
                callback(std::move(out));
            } catch (const cv::Exception &) {
                callback(cv::Mat());
            } catch (const std::exception &) {
                callback(cv::Mat());
            }
        },
        "ImageProcessor::applyLutAsyncCallback",
        vistella::tp::Priority::Normal);

    if (!submitted) {
        // 提交失败 → 同步兜底
        runSync();
    }
}