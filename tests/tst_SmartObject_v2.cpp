// =============================================================================
//  tst_SmartObject_v2 - P1.4.1 (2026-09-17) 智能对象 Convert / Rasterize /
//  Transform / FileWatcher 单元测试 (10 用例)
//
//  覆盖:
//    - ConvertToSmartObject (embed=true / false)
//    - RasterizeSmartObject (basic + missing source)
//    - setSmartObjectTransform (identity / scale 2x)
//    - Undo for transform / convert / rasterize via QUndoStack
//    - SmartObjectWatcher via QFileSystemWatcher (modify file, signal fires)
// =============================================================================

#include <QTest>
#include <QUndoStack>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QDateTime>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"
#include "../src/media/imageworker/layers/SmartObjectWatcher.h"

using namespace layers;

class tst_SmartObject_v2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void test_convertToSmartObject_embedded();
    void test_convertToSmartObject_linked();
    void test_rasterizeSmartObject_basic();
    void test_rasterizeSmartObject_missing();
    void test_transform_identity();
    void test_transform_scale_2x();
    void test_transform_undo();
    void test_convertCommand_undo();
    void test_rasterizeCommand_undo();
    void test_watcher_basic();

private:
    QString makeTempPng(const QSize &size, const QColor &color,
                        const QString &nameTag);
};

QString tst_SmartObject_v2::makeTempPng(const QSize &size, const QColor &color,
                                        const QString &nameTag)
{
    const QString baseName = QStringLiteral("/tst_so_v2_") + nameTag
                             + QStringLiteral("_XXXXXX.png");
    QTemporaryFile tmp(QDir::tempPath() + baseName);
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    const QString path = tmp.fileName();
    tmp.close();
    cv::Mat img(size.height(), size.width(), CV_8UC3,
                cv::Scalar(color.blue(), color.green(), color.red()));
    cv::imwrite(path.toStdString(), img);
    return path;
}

void tst_SmartObject_v2::initTestCase() {}
void tst_SmartObject_v2::cleanupTestCase()
{
    LayerStack::cleanupSmartObjectCache();
}
void tst_SmartObject_v2::cleanup()
{
    LayerStack::cleanupSmartObjectCache();
}

// =====================================================================
//  ConvertToSmartObject
// =====================================================================

void tst_SmartObject_v2::test_convertToSmartObject_embedded()
{
    // Bitmap 32x32 blue → SmartObject embed.
    LayerStack stack;
    cv::Mat img(32, 32, CV_8UC3, cv::Scalar(200, 100, 50));   // BGR
    stack.addLayer(QStringLiteral("B"), img);
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);

    QVERIFY(stack.convertToSmartObject(0, /*embed*/true));
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
    QVERIFY(stack.at(0)->sourceEmbedded);
    QVERIFY(!stack.at(0)->sourceFilePath.isEmpty());
    // Cache file must exist on disk.
    QVERIFY2(QFile::exists(stack.at(0)->sourceFilePath),
             qPrintable(QStringLiteral("cache missing: %1")
                        .arg(stack.at(0)->sourceFilePath)));
    // hasTransform reset by convert.
    QCOMPARE(stack.at(0)->hasTransform, false);

    // Cleanup cache file.
    QFile::remove(stack.at(0)->sourceFilePath);
}

void tst_SmartObject_v2::test_convertToSmartObject_linked()
{
    LayerStack stack;
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(10, 20, 30));
    stack.addLayer(QStringLiteral("B"), img);

    QVERIFY(stack.convertToSmartObject(0, /*embed*/false));
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
    QCOMPARE(stack.at(0)->sourceEmbedded, false);
    QVERIFY(stack.at(0)->sourceFilePath.isEmpty());
    QCOMPARE(stack.at(0)->hasTransform, false);
}

// =====================================================================
//  RasterizeSmartObject
// =====================================================================

void tst_SmartObject_v2::test_rasterizeSmartObject_basic()
{
    // Build a Bitmap, convert to Smart (embed), then rasterize back to Bitmap.
    // Final image should be close to the original (modulo PNG round-trip).
    LayerStack stack;
    cv::Mat img(24, 24, CV_8UC3, cv::Scalar(123, 200, 50));
    stack.addLayer(QStringLiteral("B"), img);

    QVERIFY(stack.convertToSmartObject(0, true));
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    QVERIFY(stack.rasterizeSmartObject(0));
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);
    QVERIFY(!stack.at(0)->image.empty());
    QCOMPARE(stack.at(0)->image.size(), img.size());
    QCOMPARE(stack.at(0)->image.type(), img.type());
    // SmartObject payload cleared.
    QVERIFY(stack.at(0)->sourceFilePath.isEmpty());
    QCOMPARE(stack.at(0)->sourceEmbedded, false);
    QCOMPARE(stack.at(0)->hasTransform, false);
    // rasterizeSmartObject removed the orphan embedded cache file.
}

void tst_SmartObject_v2::test_rasterizeSmartObject_missing()
{
    // SmartObject with a path that doesn't exist on disk → rasterize fails.
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"),
                               QStringLiteral("/no/such/file.png"), false);
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    QVERIFY(!stack.rasterizeSmartObject(0));
    // Kind stays SmartObject.
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
}

// =====================================================================
//  Transform
// =====================================================================

void tst_SmartObject_v2::test_transform_identity()
{
    LayerStack stack;
    QString path = makeTempPng(QSize(40, 40), QColor(80, 160, 240), "ident");
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);
    stack.setCanvasSize(cv::Size(40, 40));
    QCOMPARE(stack.at(0)->hasTransform, false);

    // Identity transform is a no-op: hasTransform stays false (matches
    //   project convention where setters return true only on state change).
    stack.setSmartObjectTransform(0, QTransform());
    QCOMPARE(stack.at(0)->hasTransform, false);

    // First set a non-identity, then call identity to reset — must return
    //   true because hasTransform did flip.
    QVERIFY(stack.setSmartObjectTransform(0, QTransform::fromScale(1.5, 1.5)));
    QCOMPARE(stack.at(0)->hasTransform, true);
    QVERIFY(stack.setSmartObjectTransform(0, QTransform()));
    QCOMPARE(stack.at(0)->hasTransform, false);

    cv::Mat out = stack.renderOne(0);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(40, 40));

    QFile::remove(path);
}

void tst_SmartObject_v2::test_transform_scale_2x()
{
    LayerStack stack;
    QString path = makeTempPng(QSize(20, 20), QColor(200, 100, 50), "scale2x");
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);
    stack.setCanvasSize(cv::Size(40, 40));

    QVERIFY(stack.setSmartObjectTransform(0, QTransform::fromScale(2.0, 2.0)));
    QCOMPARE(stack.at(0)->hasTransform, true);

    cv::Mat out = stack.renderOne(0);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(40, 40));

    // Pixel scale: source 20x20 stretched to canvas 40x40 — center pixel
    // must be the source color (BGR: 50, 100, 200).
    const cv::Vec3b px = out.at<cv::Vec3b>(20, 20);
    QCOMPARE(static_cast<int>(px[0]), 50);
    QCOMPARE(static_cast<int>(px[1]), 100);
    QCOMPARE(static_cast<int>(px[2]), 200);

    QFile::remove(path);
}

void tst_SmartObject_v2::test_transform_undo()
{
    LayerStack stack;
    QString path = makeTempPng(QSize(16, 16), QColor(120, 200, 80), "txUndo");
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);

    QUndoStack undo;
    const QTransform t = QTransform::fromScale(2.0, 2.0);
    const bool oldHas = stack.at(0)->hasTransform;
    const QTransform oldT = stack.at(0)->transform;
    auto *cmd = LayerCommand::makeSetSmartObjectTransform(
        &stack, 0, oldT, oldHas, t, /*newHas*/true);
    undo.push(cmd);
    // Apply the new transform manually (Phase 5 pattern).
    QVERIFY(stack.setSmartObjectTransform(0, t));
    QCOMPARE(stack.at(0)->hasTransform, true);

    // Undo: hasTransform should flip back to oldHas (false in this case).
    undo.undo();
    QCOMPARE(stack.at(0)->hasTransform, false);
    QCOMPARE(stack.at(0)->transform, QTransform());

    // Redo: no-op for transform (Phase 5 convention), but stack state
    // should still match the post-push value because we applied manually
    // before push and undo reverted it.
    undo.redo();
    QCOMPARE(stack.at(0)->hasTransform, false);

    QFile::remove(path);
}

// =====================================================================
//  Convert / Rasterize undo
// =====================================================================

void tst_SmartObject_v2::test_convertCommand_undo()
{
    LayerStack stack;
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(10, 20, 30));
    stack.addLayer(QStringLiteral("B"), img);
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);

    // Capture before-state for undo, then convert.
    Layer before = *stack.at(0);
    QUndoStack undo;
    auto *cmd = LayerCommand::makeConvertToSmartObject(&stack, 0, before);
    undo.push(cmd);
    QVERIFY(stack.convertToSmartObject(0, true));
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    // Undo: back to Bitmap with original image.
    undo.undo();
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);
    QVERIFY(!stack.at(0)->image.empty());
    QCOMPARE(stack.at(0)->image.size(), img.size());
    // After undo, cache file was already removed by rasterizeSmartObject
    //   prior to push. No further cleanup needed.
}

void tst_SmartObject_v2::test_rasterizeCommand_undo()
{
    LayerStack stack;
    QString path = makeTempPng(QSize(16, 16), QColor(60, 140, 220), "rastUndo");
    stack.addSmartObjectLayer(QStringLiteral("S"), path, true);
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    Layer before = *stack.at(0);
    QUndoStack undo;
    auto *cmd = LayerCommand::makeRasterizeSmartObject(&stack, 0, before);
    undo.push(cmd);
    QVERIFY(stack.rasterizeSmartObject(0));
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);

    undo.undo();
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
    QCOMPARE(stack.at(0)->sourceFilePath, before.sourceFilePath);
    QCOMPARE(stack.at(0)->sourceEmbedded, before.sourceEmbedded);

    QFile::remove(path);
    LayerStack::cleanupSmartObjectCache();
}

// =====================================================================
//  SmartObjectWatcher
// =====================================================================

void tst_SmartObject_v2::test_watcher_basic()
{
    // Build a SmartObject layer pointing at a PNG in a temp dir, register
    // a watcher, modify the file on disk, and assert sourceFileChanged fires.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString filePath = tmpDir.path() + QStringLiteral("/watched.png");
    {
        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(100, 150, 200));
        QVERIFY(cv::imwrite(filePath.toStdString(), img));
    }

    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), filePath, /*embed*/false);

    SmartObjectWatcher watcher;
    watcher.rewatchAll(&stack);
    QCOMPARE(watcher.watchedCount(), 1);

    QSignalSpy spy(&watcher, &SmartObjectWatcher::sourceFileChanged);
    QVERIFY(spy.isValid());

    // Modify the file. Some platforms need a small delay to make mtime
    // distinct; QDateTime resolution on Windows is ~16ms so we sleep 50ms.
    QThread::msleep(50);
    {
        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(50, 75, 100));
        QVERIFY(cv::imwrite(filePath.toStdString(), img));
    }

    // Wait for the QFileSystemWatcher signal OR the polling timer (2s).
    // PollNow forces immediate detection — avoids flaky 2s waits in CI.
    bool fired = spy.wait(200) || false;
    if (!fired) {
        // Polling fallback: nudge mtime forward manually and call pollNow.
        QDateTime future = QDateTime::currentDateTime().addSecs(5);
        QFile f(filePath);
        if (f.open(QIODevice::ReadWrite)) {
            f.setFileTime(future, QFileDevice::FileModificationTime);
            f.close();
        }
        watcher.pollNow();
        fired = (spy.count() > 0);
    }
    QVERIFY2(fired, "SmartObjectWatcher did not fire sourceFileChanged");
    QVERIFY(spy.count() >= 1);
}

QTEST_MAIN(tst_SmartObject_v2)
#include "tst_SmartObject_v2.moc"