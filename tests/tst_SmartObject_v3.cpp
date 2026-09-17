// =============================================================================
//  tst_SmartObject_v3 - P1.4.2 (2026-09-17) 智能对象 UI/菜单集成 单元测试 (5 用例)
//
//  覆盖 P1.4.2 主菜单 / 右键菜单触发的 4 个核心路径:
//    1. Convert: convertToSmartObject + LayerCommand::makeConvertToSmartObject
//       + undo (完整还原 Bitmap + image)
//    2. Rasterize: rasterizeSmartObject + LayerCommand::makeRasterizeSmartObject
//       + undo (完整还原 SmartObject + sourceFilePath)
//    3. Relink: setSmartObjectSource + LayerCommand::makeSetSmartObject
//       + undo (还原旧路径 / embed 状态)
//    4. SmartObjectWatcher.rewatchAll 覆盖多个 SmartObject layer
//    5. SmartObjectWatcher layer 删除后 rewatchAll 触发 unwatch
//
//  注: 不构造 ImageWindow (单测不能 new ImageWindow - 需要 QApplication +
//  大量 UI 依赖, 见 tst_LayerCommand.cpp:554), 这里直接验证数据层行为,
//  MainWindow slot 是数据层 API 的薄壳, 等价.
// =============================================================================

#include <QTest>
#include <QUndoStack>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"
#include "../src/media/imageworker/layers/SmartObjectWatcher.h"

using namespace layers;

class tst_SmartObject_v3 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // P1.4.2 路径: MainWindow onSmartObjectConvert 走的数据层等价
    void test_mainwindow_convertSmartObject();
    // P1.4.2 路径: MainWindow onSmartObjectRasterize
    void test_mainwindow_rasterizeSmartObject();
    // P1.4.2 路径: MainWindow onSmartObjectRelink
    void test_mainwindow_relinkSmartObject();
    // P1.4.2: SmartObjectWatcher.rewatchAll 覆盖全部 SmartObject layer
    void test_smartWatcher_resyncFromStack();
    // P1.4.2: 删除 layer 后 rewatchAll 自动 unwatch
    void test_smartWatcher_unwatchAfterRemove();

private:
    QString makeTempPng(const QSize &size, const QString &nameTag);
};

QString tst_SmartObject_v3::makeTempPng(const QSize &size, const QString &nameTag)
{
    const QString baseName = QStringLiteral("/tst_so_v3_") + nameTag
                             + QStringLiteral("_XXXXXX.png");
    QTemporaryFile tmp(QDir::tempPath() + baseName);
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    const QString path = tmp.fileName();
    tmp.close();
    cv::Mat img(size.height(), size.width(), CV_8UC3, cv::Scalar(80, 160, 240));
    cv::imwrite(path.toStdString(), img);
    return path;
}

void tst_SmartObject_v3::initTestCase() {}
void tst_SmartObject_v3::cleanupTestCase()
{
    LayerStack::cleanupSmartObjectCache();
}
void tst_SmartObject_v3::cleanup()
{
    LayerStack::cleanupSmartObjectCache();
}

// =====================================================================
//  MainWindow onSmartObjectConvert 路径
//   slot: backup before + convertToSmartObject + push LayerCommand::makeConvertToSmartObject
//   undo: kind 回到 Bitmap + image 还原
// =====================================================================

void tst_SmartObject_v3::test_mainwindow_convertSmartObject()
{
    LayerStack stack;
    cv::Mat img(24, 24, CV_8UC3, cv::Scalar(40, 80, 160));
    stack.addLayer(QStringLiteral("B"), img);
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);

    // 模拟 onSmartObjectConvert slot 流程:
    //   1. 备份 before layer (LayerCommand 用 before 状态做还原)
    //   2. stack->convertToSmartObject(idx, embed=true)
    //   3. push makeConvertToSmartObject(stack, idx, before)
    const Layer before = *stack.at(0);
    QUndoStack undo;
    auto *cmd = LayerCommand::makeConvertToSmartObject(&stack, 0, before);
    QVERIFY(cmd != nullptr);
    undo.push(cmd);   // push 触发 LayerCommand::redo, 但我们 Phase 5 风格手动调一次
    QVERIFY(stack.convertToSmartObject(0, /*embed*/true));
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
    QVERIFY(stack.at(0)->sourceEmbedded);
    QVERIFY(!stack.at(0)->sourceFilePath.isEmpty());

    // undo 路径: slot 期望还原到 Bitmap + 原始 image
    undo.undo();
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);
    QVERIFY(!stack.at(0)->image.empty());
    QCOMPARE(stack.at(0)->image.size(), img.size());
    QCOMPARE(stack.at(0)->image.type(), img.type());

    // SmartObject payload 清除
    QVERIFY(stack.at(0)->sourceFilePath.isEmpty());
    QCOMPARE(stack.at(0)->sourceEmbedded, false);
    QCOMPARE(stack.at(0)->hasTransform, false);
}

// =====================================================================
//  MainWindow onSmartObjectRasterize 路径
//   slot: backup before + rasterizeSmartObject + push makeRasterizeSmartObject
//   undo: kind 回到 SmartObject + sourceFilePath / sourceEmbedded 还原
// =====================================================================

void tst_SmartObject_v3::test_mainwindow_rasterizeSmartObject()
{
    LayerStack stack;
    QString path = makeTempPng(QSize(32, 32), QStringLiteral("rasterize"));
    QVERIFY(!path.isEmpty());
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/false);
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);

    // 模拟 onSmartObjectRasterize slot 流程
    const Layer before = *stack.at(0);
    const QString beforePath = before.sourceFilePath;
    const bool    beforeEmbed = before.sourceEmbedded;

    QUndoStack undo;
    auto *cmd = LayerCommand::makeRasterizeSmartObject(&stack, 0, before);
    QVERIFY(cmd != nullptr);
    undo.push(cmd);
    QVERIFY(stack.rasterizeSmartObject(0));
    QCOMPARE(stack.at(0)->kind, Layer::Bitmap);
    QVERIFY(!stack.at(0)->image.empty());

    // undo: 还原 sourceFilePath / sourceEmbedded
    undo.undo();
    QCOMPARE(stack.at(0)->kind, Layer::SmartObject);
    QCOMPARE(stack.at(0)->sourceFilePath, beforePath);
    QCOMPARE(stack.at(0)->sourceEmbedded, beforeEmbed);

    QFile::remove(path);
}

// =====================================================================
//  MainWindow onSmartObjectRelink 路径
//   slot: backup oldPath/oldEmbed + setSmartObjectSource + push makeSetSmartObject
//   undo: sourceFilePath / sourceEmbedded 还原
// =====================================================================

void tst_SmartObject_v3::test_mainwindow_relinkSmartObject()
{
    LayerStack stack;
    const QString oldPath = makeTempPng(QSize(16, 16), QStringLiteral("relinkOld"));
    const QString newPath = makeTempPng(QSize(16, 16), QStringLiteral("relinkNew"));
    QVERIFY(!oldPath.isEmpty());
    QVERIFY(!newPath.isEmpty());

    stack.addSmartObjectLayer(QStringLiteral("S"), oldPath, /*embed*/false);

    // 模拟 onSmartObjectRelink slot 流程
    const QString savedOldPath = stack.at(0)->sourceFilePath;
    const bool    savedOldEmbed = stack.at(0)->sourceEmbedded;

    QUndoStack undo;
    auto *cmd = LayerCommand::makeSetSmartObject(&stack, 0, savedOldPath, savedOldEmbed);
    QVERIFY(cmd != nullptr);
    undo.push(cmd);
    QVERIFY(stack.setSmartObjectSource(0, newPath, false));
    QCOMPARE(stack.at(0)->sourceFilePath, newPath);

    // undo: 还原旧 path
    undo.undo();
    QCOMPARE(stack.at(0)->sourceFilePath, savedOldPath);
    QCOMPARE(stack.at(0)->sourceEmbedded, savedOldEmbed);

    // redo: no-op (Phase 5 convention - mainwindow slot 模式: push + 手动调 setter)
    // 实际 redo 不重做 setSmartObjectSource, 跟 v2 transform 用例一致
    undo.redo();
    QCOMPARE(stack.at(0)->sourceFilePath, savedOldPath);

    QFile::remove(oldPath);
    QFile::remove(newPath);
}

// =====================================================================
//  SmartObjectWatcher 覆盖多个 SmartObject layer
//   ImageWindow::loadFile() 末尾会 rewatchAll(stack), 期望 watchedCount = N
// =====================================================================

void tst_SmartObject_v3::test_smartWatcher_resyncFromStack()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString p1 = tmpDir.path() + QStringLiteral("/multi_a.png");
    const QString p2 = tmpDir.path() + QStringLiteral("/multi_b.png");
    const QString p3 = tmpDir.path() + QStringLiteral("/multi_c.png");
    {
        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(50, 100, 150));
        QVERIFY(cv::imwrite(p1.toStdString(), img));
        QVERIFY(cv::imwrite(p2.toStdString(), img));
        QVERIFY(cv::imwrite(p3.toStdString(), img));
    }

    LayerStack stack;
    // Bitmap + 2 SmartObject (link) + 1 SmartObject (embed)
    cv::Mat bmp(16, 16, CV_8UC3, cv::Scalar(10, 20, 30));
    stack.addLayer(QStringLiteral("B"), bmp);
    stack.addSmartObjectLayer(QStringLiteral("S1"), p1, false);
    stack.addSmartObjectLayer(QStringLiteral("S2"), p2, false);
    stack.addSmartObjectLayer(QStringLiteral("S3"), p3, true);

    SmartObjectWatcher watcher;
    watcher.rewatchAll(&stack);
    // 3 SmartObject layers should all be watched
    QCOMPARE(watcher.watchedCount(), 3);

    // Bitmap layer 不被 watch
    QVERIFY(stack.at(0)->kind == Layer::Bitmap);
    QVERIFY(stack.at(1)->kind == Layer::SmartObject);
    QVERIFY(stack.at(2)->kind == Layer::SmartObject);
    QVERIFY(stack.at(3)->kind == Layer::SmartObject);

    QFile::remove(p1);
    QFile::remove(p2);
    QFile::remove(p3);
}

// =====================================================================
//  删除 layer 后 rewatchAll 自动 unwatch
//   模拟 ImageWindow 删除 layer 流程 + MainWindow 4 slot 末尾 rewatchAll
// =====================================================================

void tst_SmartObject_v3::test_smartWatcher_unwatchAfterRemove()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QString p = tmpDir.path() + QStringLiteral("/one.png");
    {
        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(100, 200, 50));
        QVERIFY(cv::imwrite(p.toStdString(), img));
    }

    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), p, false);

    SmartObjectWatcher watcher;
    watcher.rewatchAll(&stack);
    QCOMPARE(watcher.watchedCount(), 1);

    // 删除 layer → count 减 0
    QVERIFY(stack.removeLayer(0));
    QCOMPARE(stack.count(), 0);

    // ImageWindow loadFile 末尾 / MainWindow 4 slot 末尾调 rewatchAll
    watcher.rewatchAll(&stack);
    QCOMPARE(watcher.watchedCount(), 0);

    QFile::remove(p);
}

QTEST_MAIN(tst_SmartObject_v3)
#include "tst_SmartObject_v3.moc"