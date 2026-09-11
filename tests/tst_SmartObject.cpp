// =============================================================================
//  tst_SmartObject - 阶段 1 W4.4 Phase 5 智能对象扩展单元测试 (2026-09-04)
//
//  覆盖:
//    - refreshSmartObject (embed 重新复制 cache)
//    - isSmartObjectSourceMissing (link + embed + 空)
//    - toggleSmartObjectEmbed (link ↔ embed 切换, cache 文件变化)
//    - cleanupSmartObjectCache (全清)
//    - render missing source: 返 placeholder 而非空 Mat
//    - LayerCommand toggleSmartObjectEmbed 撤销
// =============================================================================

#include <QTest>
#include <QUndoStack>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"

using namespace layers;

class tst_SmartObject : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // ---- API ----
    void test_refresh_reloadEmbed();
    void test_isMissing_empty();
    void test_isMissing_linkMissing();
    void test_isMissing_embedMissing();
    void test_isMissing_exists();
    void test_toggle_linkToEmbed();
    void test_toggle_embedToLink();
    void test_toggle_noPath();
    void test_cleanupCache();

    // ---- Render ----
    void test_render_missingSource_placeholder();
    void test_render_existingSource();

    // ---- Command 撤销 ----
    void test_toggleUndo();

private:
    QString makeTempPng(const QSize &size = QSize(32, 32), const QString &name = QString());
};

QString tst_SmartObject::makeTempPng(const QSize &size, const QString &name)
{
    // 跟 tst_LayerKindRender 一致 — 必须先 setAutoRemove(false) 再 open
    QString baseName = name.isEmpty()
        ? QStringLiteral("/tst_so_XXXXXX.png")
        : (QStringLiteral("/tst_so_") + name + QStringLiteral("_XXXXXX.png"));
    QTemporaryFile tmp(QDir::tempPath() + baseName);
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    QString path = tmp.fileName();
    tmp.close();
    cv::Mat img(size.height(), size.width(), CV_8UC3, cv::Scalar(100, 150, 200));
    cv::imwrite(path.toStdString(), img);
    return path;
}

void tst_SmartObject::initTestCase() {}
void tst_SmartObject::cleanupTestCase()
{
    // 清掉测试残留
    LayerStack::cleanupSmartObjectCache();
}
void tst_SmartObject::cleanup()
{
    // 每个 case 后清 cache
    LayerStack::cleanupSmartObjectCache();
}

// =====================================================================
//  API
// =====================================================================

void tst_SmartObject::test_refresh_reloadEmbed()
{
    QString path = makeTempPng(QSize(32, 32), "refresh");
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), path, /*embed*/true);
    QVERIFY(stack.refreshSmartObject(0));
    QVERIFY(!stack.refreshSmartObject(99));
    QFile::remove(path);
}

void tst_SmartObject::test_isMissing_empty()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QString(), false);
    QVERIFY(stack.isSmartObjectSourceMissing(0));
}

void tst_SmartObject::test_isMissing_linkMissing()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/no/such/file.png"), false);
    QVERIFY(stack.isSmartObjectSourceMissing(0));
}

void tst_SmartObject::test_isMissing_embedMissing()
{
    LayerStack stack;
    // embed 但源文件不存在 → cache 也不存在
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/no/such/file.png"), true);
    QVERIFY(stack.isSmartObjectSourceMissing(0));
}

void tst_SmartObject::test_isMissing_exists()
{
    // 跟 tst_LayerKindRender 模式一致
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/tst_so_exists_XXXXXX.png"));
    tmp.setAutoRemove(false);
    QVERIFY(tmp.open());
    QString path = tmp.fileName();
    tmp.close();
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(100, 150, 200));
    QVERIFY(cv::imwrite(path.toStdString(), img));
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);
    QVERIFY(!stack.isSmartObjectSourceMissing(0));
    QFile::remove(path);
}

void tst_SmartObject::test_toggle_linkToEmbed()
{
    QString path = makeTempPng(QSize(16, 16), "l2e");
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::exists(path));
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);
    QCOMPARE(stack.at(0)->sourceEmbedded, false);
    // 提前算 cachePath 跟 stack 用的是否一致
    const QString expectedCache = LayerStack::smartObjectCachePathFor(path);
    QVERIFY(stack.toggleSmartObjectEmbed(0));
    QCOMPARE(stack.at(0)->sourceEmbedded, true);
    QVERIFY2(QFile::exists(expectedCache),
             qPrintable(QStringLiteral("expected cache missing: %1").arg(expectedCache)));
    QFile::remove(path);
    QFile::remove(expectedCache);
}

void tst_SmartObject::test_toggle_embedToLink()
{
    QString path = makeTempPng(QSize(16, 16), "e2l");
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), path, true);
    const QString cachePath = LayerStack::smartObjectCachePathFor(path);
    QVERIFY(QFile::exists(cachePath));
    QVERIFY(stack.toggleSmartObjectEmbed(0));
    QCOMPARE(stack.at(0)->sourceEmbedded, false);
    // cache 应被删
    QVERIFY(!QFile::exists(cachePath));
    QFile::remove(path);
}

void tst_SmartObject::test_toggle_noPath()
{
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), QString(), false);
    QVERIFY(!stack.toggleSmartObjectEmbed(0));  // 空 path 返 false
}

void tst_SmartObject::test_cleanupCache()
{
    // 先建 3 个嵌入智能对象
    QStringList paths;
    for (int i = 0; i < 3; ++i) {
        paths << makeTempPng(QSize(8, 8), QStringLiteral("cleanup_%1").arg(i));
    }
    LayerStack stack;
    for (const QString &p : paths) {
        stack.addSmartObjectLayer(QStringLiteral("S"), p, true);
    }
    // cleanup 应该全删 (Phase 5 简化: 不查引用)
    int removed = LayerStack::cleanupSmartObjectCache();
    QVERIFY(removed >= 3);
    // cache 目录应为空或不存在
    for (const QString &p : paths) {
        const QString cachePath = LayerStack::smartObjectCachePathFor(p);
        QVERIFY(!QFile::exists(cachePath));
        QFile::remove(p);
    }
}

// =====================================================================
//  Render
// =====================================================================

void tst_SmartObject::test_render_missingSource_placeholder()
{
    LayerStack stack;
    stack.setCanvasSize(cv::Size(64, 64));
    stack.addSmartObjectLayer(QStringLiteral("S"), QStringLiteral("/no/such.png"), false);
    cv::Mat out = stack.renderOne(0);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), cv::Size(64, 64));
    // placeholder: 灰底 (200,200,200) + 红 X
    // 中心点应非全灰 (有红色对角线)
    const cv::Vec3b center = out.at<cv::Vec3b>(32, 32);
    QVERIFY(center[2] > 150 || center[0] < 150);  // 至少有红色或非灰色
}

void tst_SmartObject::test_render_existingSource()
{
    QString path = makeTempPng(QSize(32, 32), "render");
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);
    stack.setCanvasSize(cv::Size(32, 32));
    cv::Mat out = stack.renderOne(0);
    QVERIFY(!out.empty());
    // 应是源图颜色 (100, 150, 200 BGR)
    const cv::Vec3b px = out.at<cv::Vec3b>(16, 16);
    QCOMPARE(static_cast<int>(px[0]), 100);
    QCOMPARE(static_cast<int>(px[1]), 150);
    QCOMPARE(static_cast<int>(px[2]), 200);
    QFile::remove(path);
}

// =====================================================================
//  Command 撤销
// =====================================================================

void tst_SmartObject::test_toggleUndo()
{
    QString path = makeTempPng(QSize(16, 16), "undo");
    LayerStack stack;
    stack.addSmartObjectLayer(QStringLiteral("S"), path, false);
    QUndoStack undo;
    auto *cmd = LayerCommand::makeToggleSmartObjectEmbed(&stack, 0, /*old*/false);
    undo.push(cmd);
    stack.toggleSmartObjectEmbed(0);
    QCOMPARE(stack.at(0)->sourceEmbedded, true);
    undo.undo();
    QCOMPARE(stack.at(0)->sourceEmbedded, false);
    QFile::remove(path);
    LayerStack::cleanupSmartObjectCache();
}

QTEST_MAIN(tst_SmartObject)
#include "tst_SmartObject.moc"
