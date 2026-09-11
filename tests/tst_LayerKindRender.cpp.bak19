// =============================================================================
//  tst_LayerKindRender - 阶段 1 W4.3 Phase 3 单元测试 (2026-09-04)
//
//  覆盖:
//    - 4 种 per-kind addLayer 工厂 (Text / Vector / SmartObject / Adjustment)
//    - per-kind setter (setText / setTextFont / setVectorPaths /
//      setSmartObjectSource / setAdjustmentType / setAdjustmentLut)
//    - render() 混合 5 种 kind (含 Adjustment 在中间)
//    - renderOne() per-kind
//    - canvasSize / setCanvasSize
// =============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QPainterPath>
#include <QTemporaryFile>
#include <QDir>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"

using namespace layers;

class tst_LayerKindRender : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- Factory ----
    void test_addTextLayer();
    void test_addVectorLayer();
    void test_addSmartObjectLayer();
    void test_addAdjustmentLayer_identityLut();

    // ---- Setter ----
    void test_setText();
    void test_setTextFont();
    void test_setVectorPaths();
    void test_setSmartObjectSource();
    void test_setAdjustmentType();
    void test_setAdjustmentLut();

    // ---- Render ----
    void test_render_textLayer();
    void test_render_vectorLayer();
    void test_render_adjustmentLayer();
    void test_render_mixedAllKinds();

    // ---- renderOne ----
    void test_renderOne_text();
    void test_renderOne_vector();
    void test_renderOne_adjustment();
    void test_renderOne_smartObject_missing();

    // ---- Canvas ----
    void test_canvasSize_base();
    void test_setCanvasSize();
    void test_canvasSize_noBase();

private:
    cv::Mat makeMat(int w = 8, int h = 8, cv::Scalar bgr = cv::Scalar(50, 100, 200))
    {
        return cv::Mat(h, w, CV_8UC3, bgr);
    }
};

void tst_LayerKindRender::initTestCase()
{
}

void tst_LayerKindRender::cleanupTestCase()
{
}

// =====================================================================
//  Factory
// =====================================================================

void tst_LayerKindRender::test_addTextLayer()
{
    LayerStack stack;
    int idx = stack.addTextLayer(QStringLiteral("MyText"),
                                  QStringLiteral("Hello"), 36, Qt::red, QStringLiteral("Arial"));
    QCOMPARE(idx, 0);
    QCOMPARE(stack.count(), 1);
    auto l = stack.at(0);
    QVERIFY(l);
    QCOMPARE(l->kind, Layer::Text);
    QCOMPARE(l->text, QStringLiteral("Hello"));
    QCOMPARE(l->fontSize, 36);
    QCOMPARE(l->textColor, QColor(Qt::red));
    QCOMPARE(l->fontFamily, QStringLiteral("Arial"));
}

void tst_LayerKindRender::test_addVectorLayer()
{
    LayerStack stack;
    QPainterPath p;
    p.addRect(QRectF(10, 20, 30, 40));
    int idx = stack.addVectorLayer(QStringLiteral("MyVec"),
                                    QVector<QPainterPath>{p},
                                    QVector<QColor>{QColor(0, 255, 0)});
    QCOMPARE(idx, 0);
    auto l = stack.at(0);
    QVERIFY(l);
    QCOMPARE(l->kind, Layer::Vector);
    QCOMPARE(l->vectorPaths.size(), 1);
    QCOMPARE(l->vectorFillColors.size(), 1);
    QCOMPARE(l->vectorFillColors[0], QColor(0, 255, 0));
    QCOMPARE(l->vectorStrokeWidths.size(), 1);
    QCOMPARE(l->vectorStrokeWidths[0], 0.0);
}

void tst_LayerKindRender::test_addSmartObjectLayer()
{
    // 准备临时文件
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/test_smart_XXXXXX.png"));
    tmp.setAutoRemove(false);
    QVERIFY(tmp.open());
    QString path = tmp.fileName();
    tmp.close();
    cv::Mat img = makeMat(32, 32, cv::Scalar(100, 150, 200));
    QVERIFY(cv::imwrite(path.toStdString(), img));

    LayerStack stack;
    int idx = stack.addSmartObjectLayer(QStringLiteral("MySO"), path, false);
    QCOMPARE(idx, 0);
    auto l = stack.at(0);
    QVERIFY(l);
    QCOMPARE(l->kind, Layer::SmartObject);
    QCOMPARE(l->sourceFilePath, path);
    QVERIFY(!l->sourceEmbedded);
    QFile::remove(path);
}

void tst_LayerKindRender::test_addAdjustmentLayer_identityLut()
{
    LayerStack stack;
    int idx = stack.addAdjustmentLayer(QStringLiteral("MyAdj"), QStringLiteral("curves"));
    QCOMPARE(idx, 0);
    auto l = stack.at(0);
    QVERIFY(l);
    QCOMPARE(l->kind, Layer::Adjustment);
    QCOMPARE(l->adjustmentType, QStringLiteral("curves"));
    QCOMPARE(l->adjustmentLut.rows, 256);
    QCOMPARE(l->adjustmentLut.cols, 1);
    QCOMPARE(l->adjustmentLut.type(), CV_8U);
    // identity: lut[i] = i
    const uchar *p = l->adjustmentLut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) {
        QCOMPARE(static_cast<int>(p[i]), i);
    }
}

// =====================================================================
//  Setter
// =====================================================================

void tst_LayerKindRender::test_setText()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("old"), 24, Qt::white, QStringLiteral("Arial"));
    QVERIFY(stack.setText(0, QStringLiteral("new")));
    QCOMPARE(stack.at(0)->text, QStringLiteral("new"));
    // 错 kind: 应返 false
    stack.addLayer(QStringLiteral("B"), makeMat());
    QVERIFY(!stack.setText(1, QStringLiteral("x")));
    // 越界
    QVERIFY(!stack.setText(99, QStringLiteral("x")));
}

void tst_LayerKindRender::test_setTextFont()
{
    LayerStack stack;
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("text"), 24, Qt::white, QStringLiteral("Arial"));
    QVERIFY(stack.setTextFont(0, QStringLiteral("Times"), 48, Qt::yellow));
    auto l = stack.at(0);
    QCOMPARE(l->fontFamily, QStringLiteral("Times"));
    QCOMPARE(l->fontSize, 48);
    QCOMPARE(l->textColor, QColor(Qt::yellow));
    // 错 kind
    stack.addLayer(QStringLiteral("B"), makeMat());
    QVERIFY(!stack.setTextFont(1, QStringLiteral("X"), 12, Qt::black));
}

void tst_LayerKindRender::test_setVectorPaths()
{
    LayerStack stack;
    QPainterPath p1; p1.addRect(QRectF(0, 0, 10, 10));
    QPainterPath p2; p2.addEllipse(QRectF(20, 20, 30, 30));
    stack.addVectorLayer(QStringLiteral("V"),
                         QVector<QPainterPath>{p1},
                         QVector<QColor>{Qt::red});
    QVERIFY(stack.setVectorPaths(0,
                                 QVector<QPainterPath>{p1, p2},
                                 QVector<QColor>{Qt::blue, Qt::green}));
    auto l = stack.at(0);
    QCOMPARE(l->vectorPaths.size(), 2);
    QCOMPARE(l->vectorFillColors.size(), 2);
    QCOMPARE(l->vectorFillColors[1], QColor(Qt::green));
    // strokes 缺省 → 自动补 0
    QCOMPARE(l->vectorStrokeWidths.size(), 2);
    QCOMPARE(l->vectorStrokeWidths[0], 0.0);
    // 错 kind
    stack.addLayer(QStringLiteral("B"), makeMat());
    QVERIFY(!stack.setVectorPaths(1, {}, {}));
}

void tst_LayerKindRender::test_setSmartObjectSource()
{
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/test_so_set_XXXXXX.png"));
    tmp.setAutoRemove(false);
    QVERIFY(tmp.open());
    QString path = tmp.fileName();
    tmp.close();
    cv::Mat img = makeMat(16, 16, cv::Scalar(0, 0, 255));
    cv::imwrite(path.toStdString(), img);

    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/old/path.png"), false);
    QVERIFY(stack.setSmartObjectSource(0, path, true));
    auto l = stack.at(0);
    QCOMPARE(l->sourceFilePath, path);
    QVERIFY(l->sourceEmbedded);
    QFile::remove(path);
}

void tst_LayerKindRender::test_setAdjustmentType()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    QVERIFY(stack.setAdjustmentType(0, QStringLiteral("levels")));
    QCOMPARE(stack.at(0)->adjustmentType, QStringLiteral("levels"));
    stack.addLayer(QStringLiteral("B"), makeMat());
    QVERIFY(!stack.setAdjustmentType(1, QStringLiteral("x")));
}

void tst_LayerKindRender::test_setAdjustmentLut()
{
    LayerStack stack;
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    cv::Mat lut(256, 1, CV_8U);
    uchar *p = lut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(255 - i);  // invert
    QVERIFY(stack.setAdjustmentLut(0, lut));
    auto l = stack.at(0);
    const uchar *q = l->adjustmentLut.ptr<uchar>();
    QCOMPARE(static_cast<int>(q[0]), 255);
    QCOMPARE(static_cast<int>(q[255]), 0);
    // 错 kind
    stack.addLayer(QStringLiteral("B"), makeMat());
    QVERIFY(!stack.setAdjustmentLut(1, lut));
}

// =====================================================================
//  Render
// =====================================================================

void tst_LayerKindRender::test_render_textLayer()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(64, 64, cv::Scalar(0, 0, 0)));  // 黑底
    int idx = stack.addTextLayer(QStringLiteral("T"), QStringLiteral("Hi"), 24, Qt::red);
    Q_UNUSED(idx);
    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(64, 64));
    // 至少 1 像素 ≠ 黑色 (文字至少染了一处)
    bool found = false;
    for (int y = 0; y < out.rows && !found; ++y) {
        const cv::Vec3b *row = out.ptr<cv::Vec3b>(y);
        for (int x = 0; x < out.cols && !found; ++x) {
            if (row[x] != cv::Vec3b(0, 0, 0)) found = true;
        }
    }
    QVERIFY(found);
}

void tst_LayerKindRender::test_render_vectorLayer()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(64, 64, cv::Scalar(255, 255, 255)));  // 白底
    QPainterPath rect;
    rect.addRect(QRectF(20, 20, 60, 60));
    stack.addVectorLayer(QStringLiteral("V"),
                         QVector<QPainterPath>{rect},
                         QVector<QColor>{QColor(255, 0, 0)});
    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(64, 64));
    // 检查至少有一个红像素
    bool foundRed = false;
    for (int y = 0; y < out.rows && !foundRed; ++y) {
        const cv::Vec3b *row = out.ptr<cv::Vec3b>(y);
        for (int x = 0; x < out.cols && !foundRed; ++x) {
            if (row[x][2] > 200 && row[x][0] < 50) foundRed = true;
        }
    }
    QVERIFY(foundRed);
}

void tst_LayerKindRender::test_render_adjustmentLayer()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(100, 100, 100)));
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    // identity LUT: 输出应仍是 100,100,100
    const cv::Vec3b px = out.at<cv::Vec3b>(16, 16);
    QCOMPARE(static_cast<int>(px[0]), 100);
    QCOMPARE(static_cast<int>(px[1]), 100);
    QCOMPARE(static_cast<int>(px[2]), 100);
    // 用 invert LUT 应得 155,155,155
    cv::Mat lut(256, 1, CV_8U);
    uchar *p = lut.ptr<uchar>();
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(255 - i);
    // 注意: setBaseLayer 在 index 0, addAdjustmentLayer 在 index 1
    stack.setAdjustmentLut(1, lut);
    cv::Mat out2 = stack.render();
    const cv::Vec3b px2 = out2.at<cv::Vec3b>(16, 16);
    QCOMPARE(static_cast<int>(px2[0]), 155);
    QCOMPARE(static_cast<int>(px2[1]), 155);
    QCOMPARE(static_cast<int>(px2[2]), 155);
}

void tst_LayerKindRender::test_render_mixedAllKinds()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(48, 48, cv::Scalar(50, 50, 50)));
    // text
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("X"), 18, Qt::white);
    // vector
    QPainterPath rect; rect.addRect(QRectF(0, 0, 30, 30));
    stack.addVectorLayer(QStringLiteral("V"),
                         QVector<QPainterPath>{rect},
                         QVector<QColor>{QColor(0, 255, 0)});
    // adjustment (identity)
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    cv::Mat out = stack.render();
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(48, 48));
    // 总层数
    QCOMPARE(stack.count(), 4);   // base + text + vector + adj
}

// =====================================================================
//  renderOne
// =====================================================================

void tst_LayerKindRender::test_renderOne_text()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(0, 0, 0)));
    stack.addTextLayer(QStringLiteral("T"), QStringLiteral("Hi"), 18, Qt::red);
    cv::Mat out = stack.renderOne(1);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(32, 32));
}

void tst_LayerKindRender::test_renderOne_vector()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32));
    QPainterPath p; p.addRect(QRectF(10, 10, 20, 20));
    stack.addVectorLayer(QStringLiteral("V"),
                         QVector<QPainterPath>{p},
                         QVector<QColor>{Qt::red});
    cv::Mat out = stack.renderOne(1);
    QVERIFY(!out.empty());
}

void tst_LayerKindRender::test_renderOne_adjustment()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(32, 32, cv::Scalar(100, 100, 100)));
    stack.addAdjustmentLayer(QStringLiteral("A"), QStringLiteral("curves"));
    cv::Mat out = stack.renderOne(1);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(32, 32));
}

void tst_LayerKindRender::test_renderOne_smartObject_missing()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/nonexistent/file.png"), false);
    // 阶段 1 W4.4 Phase 5 (2026-09-04): 缺源时 renderOne 返 placeholder (灰底 + 红 X)
    //   不再返空 Mat, 跟 PS 行为一致
    stack.setCanvasSize(cv::Size(32, 32));
    cv::Mat out = stack.renderOne(0);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(32, 32));
}

// =====================================================================
//  Canvas
// =====================================================================

void tst_LayerKindRender::test_canvasSize_base()
{
    LayerStack stack;
    stack.setBaseLayer(makeMat(100, 200));
    QCOMPARE(stack.canvasSize(), cv::Size(100, 200));
}

void tst_LayerKindRender::test_setCanvasSize()
{
    LayerStack stack;
    stack.setCanvasSize(cv::Size(123, 456));
    QCOMPARE(stack.canvasSize(), cv::Size(123, 456));
    // 非法 size 不改
    stack.setCanvasSize(cv::Size(-1, 0));
    QCOMPARE(stack.canvasSize(), cv::Size(123, 456));
}

void tst_LayerKindRender::test_canvasSize_noBase()
{
    LayerStack stack;
    // 无 base: 走 fallback 800x600
    QCOMPARE(stack.canvasSize(), cv::Size(800, 600));
}

QTEST_MAIN(tst_LayerKindRender)
#include "tst_LayerKindRender.moc"
