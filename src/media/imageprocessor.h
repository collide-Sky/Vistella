#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <opencv2/core.hpp>
#include <QImage>
#include <QString>
#include <QVector>
#include <QPolygonF>
#include <QPair>
#include <QColor>

#include <future>
#include <functional>

// P0-3.1 (2026-09-08): forward decl for EngineContext (applyLutAsync 走 background pool)
//   跟 P0-2.5 ImageProcessorAsync / LayerStack::renderAsync 同样的模式
namespace vistella::tp { class EngineContext; }

class ImageProcessor
{
public:
    // 把 OpenCV cv::Mat 写到 PNG/JPG/BMP 文件
    static bool saveImage(const cv::Mat &img, const QString &path, QString *err = nullptr);

    // 把 cv::Mat 转成 QImage 便于 QGraphicsView 显示
    static QImage matToQImage(const cv::Mat &mat);
    // 把 QImage 转成 cv::Mat (扁平化文字图层时需要) - 返回 BGR 格式
    static cv::Mat qImageToMat(const QImage &img);

    // ===== 像素级算法 (in-place 或 in->out) =====
    static void toGray(const cv::Mat &in, cv::Mat &out);
    static void toBinary(const cv::Mat &in, cv::Mat &out, double thresh = 128.0);
    static void invert(const cv::Mat &in, cv::Mat &out);
    static void brightnessContrast(const cv::Mat &in, cv::Mat &out,
                                   double alpha = 1.0, int beta = 0);

    // ===== 滤波算法 =====
    static void gaussianBlur(const cv::Mat &in, cv::Mat &out, int ksize = 5, double sigma = 0);
    static void medianBlur(const cv::Mat &in, cv::Mat &out, int ksize = 5);
    static void bilateralFilter(const cv::Mat &in, cv::Mat &out,
                                int d = 9, double sigmaColor = 75, double sigmaSpace = 75);
    static void sharpen(const cv::Mat &in, cv::Mat &out);     // 拉普拉斯锐化
    static void edgeDetect(const cv::Mat &in, cv::Mat &out,
                           double lowThresh = 80.0, double highThresh = 180.0);  // Canny
    static void emboss(const cv::Mat &in, cv::Mat &out);      // 浮雕效果
    static void resize(const cv::Mat &in, cv::Mat &out, double scale);
    static void rotate90(const cv::Mat &in, cv::Mat &out, int direction);  // 0/1/2 = 0/90/180/270 CW

    // ===== 颜色调整 =====
    // saturation: scale=1.0 原图, >1.0 更饱和, 0 灰度
    static void saturation(const cv::Mat &in, cv::Mat &out, double scale);
    // hueShift: 角度, 正负都可以
    static void hueShift(const cv::Mat &in, cv::Mat &out, double degrees);
    // 曝光 = brightness + contrast 一起调
    static void exposure(const cv::Mat &in, cv::Mat &out, double ev);   // ev in [-3, 3]

    // ===== 局部操作 =====
    // mosaic: 在 (x, y, w, h) 区域里把每个 blockSize x blockSize 块替换成平均色
    static void mosaic(cv::Mat &img, int x, int y, int w, int h, int blockSize = 10);
    // 文字叠加: 在 (x, y) 位置写 text, color 文字色, fontScale 字号, thickness 粗细
    // fontFamily 用 Qt 字体名 (空 = 自动)
    static void drawText(cv::Mat &img, const QString &text,
                         int x, int y, const QColor &color = Qt::white,
                         double fontScale = 1.0, int thickness = 2,
                         const QString &fontFamily = QString());

    // ===== 直方图 =====
    // 灰度直方图 (256 bin)
    static QVector<int> computeHistogram(const cv::Mat &gray);
    static QImage renderHistogram(const QVector<int> &hist, int w = 256, int height = 100);
    // RGB 直方图 (BGR 顺序) - 3 个 256-bin 数组
    struct RGBHist { QVector<int> b, g, r; };
    static RGBHist computeRGBHistogram(const cv::Mat &bgr);
    static QImage renderRGBHistogram(const RGBHist &h, int w = 256, int height = 100);
    // HSV 直方图 (HSV 顺序) - H 180, S 256, V 256
    struct HSVHist { QVector<int> h, s, v; };
    static HSVHist computeHSVHistogram(const cv::Mat &bgr);
    static QImage renderHSVHistogram(const HSVHist &hist, int w = 256, int height = 100);

    // ===== 统计 =====
    struct ChannelStats {
        double mean = 0;   // 0..255
        double std  = 0;
        double min  = 0;
        double max  = 0;
    };
    struct ImageStats {
        QVector<ChannelStats> channels;  // size = image.channels()
        ChannelStats luminance;          // 灰度上的统计
        int  nonBlackPixels = 0;         // 灰度 > 0 的像素数
        int  totalPixels    = 0;
    };
    static ImageStats computeStats(const cv::Mat &bgr);

    // ===== 像素查询 (鼠标 hover) =====
    struct PixelInfo {
        bool   valid = false;
        QPoint pos;             // 原图坐标
        int    b = 0, g = 0, r = 0;
        int    h = 0, s = 0, v = 0;   // HSV
        int    gray = 0;
    };
    // x/y 是原图坐标 (0..width-1, 0..height-1)
    static PixelInfo pixelAt(const cv::Mat &bgr, int x, int y);

    // ===== 信息 =====
    struct ImageInfo {
        int  width   = 0;
        int  height  = 0;
        int  channels = 0;
        QString typeName;     // "CV_8UC3" 等
        qint64 fileSize = 0;  // bytes
    };
    static ImageInfo describe(const cv::Mat &img, const QString &filePath = QString());

    // ===== P0-3.1 (2026-09-08): 色彩调整 LUT (PS 风格) =====
    // 设计原则 (一次性到位, 不留补丁):
    //   1. 所有 build* 函数返 cv::Mat (256 行 1 列 CV_8U) = 单通道 LUT
    //      - applyLut 内部用 cv::LUT, 接受 256 元素 LUT
    //      - 256x1 / 1x256 形状都支持 (applyLut 内部 reshape 到 1x256)
    //   2. applyLut 同步: 支持 CV_8U 单/三/四通道图像, 多通道逐 channel 应用
    //   3. applyLutAsync 走 EngineContext background pool (跟 P0-2.5 ImageProcessorAsync
    //      同样的 runOpEngine 模式: shared_ptr<promise> + submit_fn)
    //      - engine == nullptr 兜底走同步 (不破坏旧调用方)
    //      - 提交失败 (池满 / shutting down) → 同步兜底
    //   4. 不"简化"任何业务逻辑 — 8 个 LUT 函数 1:1 实装, 每个都是完整 PS 风格算法
    //
    // 跟 P0-3.2 (UI 接入) 关系:
    //   - P0-3.1 只建算法 + 测试, 不改 imagewindow.cpp / ImageAdjustmentPanel.cpp
    //   - P0-3.2 接 imagewindow, 替换 applyCurrentParams 同步调用为 applyLutAsync
    static cv::Mat buildCurvesLUT(const QPolygonF &controlPoints);
    static cv::Mat buildLevelsLUT(int inLow, int inHigh, double gamma, int outLow, int outHigh);
    static cv::Mat buildHueSatLUT(const QVector<QPair<int,int>> &hueSatPairs, int lightness = 0);
    static cv::Mat buildBlackWhiteLUT(const QVector<double> &rgbMixer);
    static cv::Mat buildColorBalanceLUT(int cyanRed, int magentaGreen, int yellowBlue);
    static cv::Mat buildVibranceLUT(double vibrance, double saturation);
    static cv::Mat buildPhotoFilterLUT(const QColor &tint, int density);
    // 同步 applyLut — 供测试 + ImageWindow 旧调路径 (P0-3.2 前)
    static void applyLut(const cv::Mat &in, cv::Mat &out, const cv::Mat &lut);
    // 异步 applyLut — P0-3.2 UI 入口, engine==nullptr 走同步兜底
    static std::future<cv::Mat> applyLutAsync(const cv::Mat &in, const cv::Mat &lut,
                                              vistella::tp::EngineContext *engine);
    // 异步 applyLut callback overload (P0-3.2 2026-09-08)
    //   跟 P0-2.5 LayerStack::renderAsync 同样的模式: 走 background pool,
    //   完成后调 callback(cv::Mat result). callback 在后台线程触发, 调用方
    //   必须自己用 QMetaObject::invokeMethod 切回主线程刷显示
    //   跟 std::future overload 共存, 给 P0-3.2 AdjustmentPanel 5 tab UI 用
    //   (UI 调参实时反馈, callback 模式避免 future 轮询)
    static void applyLutAsync(const cv::Mat &in, const cv::Mat &lut,
                              vistella::tp::EngineContext *engine,
                              std::function<void(cv::Mat)> callback);
};

#endif // IMAGEPROCESSOR_H
