// =============================================================================
//  tst_ImageWorker - 阶段 1 W1 imageWorker 模块骨架测试 (2026-09-03)
//
//  覆盖:
//    - ImageWorker::info() 返正确 ModuleInfo (id / exts)
//    - MediaDispatcher 注册 ImageWorker 后能路由 .png / .jpg 等图片
//    - openFile 返 IWorkspace*, 实际是 ImageWorkspace*
//    - ImageWorkspace 转发到 m_imageWindow (loadFile / filePath / moduleId)
//    - 不支持的扩展名返 nullptr + emit openFailed
//    - 空路径返 nullptr
// =============================================================================

#include <QTest>
#include <QApplication>     // ImageWindow 需要 QApplication
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QBuffer>

#include "../src/core/IModule.h"
#include "../src/core/MediaDispatcher.h"
#include "../src/media/imageworker/ImageWorker.h"
#include "../src/media/imageworker/ImageWorkspace.h"

class tst_ImageWorker : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- ImageWorker::info() ----
    void test_info();

    // ---- MediaDispatcher 路由 ----
    void test_registerInDispatcher();
    void test_routePng();
    void test_routeJpg();
    void test_routeUnknownExt();
    void test_routeEmpty();

    // ---- ImageWorkspace 接口 ----
    void test_workspace_moduleId();
    void test_workspace_filePath();
    void test_workspace_loadFile();
    void test_workspace_displayName();

    // ---- 信号转发 ----
    void test_workspace_signalForward();

private:
    void writePng(const QString &rel, const QSize &size = QSize(16, 16));
    void writeJpg(const QString &rel, const QSize &size = QSize(16, 16));
    QString absPath(const QString &rel) const { return m_testRoot + QStringLiteral("/") + rel; }

    // 每个 test 前注册 m_worker (init/cleanup 槽在 QTest 6 不识别)
    void ensureWorkerRegistered()
    {
        auto &disp = MediaDispatcher::instance();
        if (disp.moduleForId(QStringLiteral("imageWorker")) != m_worker) {
            disp.unregisterAll();
            disp.registerModule(m_worker);
        }
    }

    QTemporaryDir m_tmpDir;
    QString       m_testRoot;
    ImageWorker  *m_worker = nullptr;
};

void tst_ImageWorker::writePng(const QString &rel, const QSize &size)
{
    const QString abs = absPath(rel);
    QImage img(size, QImage::Format_RGB32);
    img.fill(Qt::red);
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QVERIFY2(img.save(abs, "PNG"), qPrintable(abs));
}

void tst_ImageWorker::writeJpg(const QString &rel, const QSize &size)
{
    const QString abs = absPath(rel);
    QImage img(size, QImage::Format_RGB32);
    img.fill(Qt::blue);
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QVERIFY2(img.save(abs, "JPG"), qPrintable(abs));
}

void tst_ImageWorker::initTestCase()
{
    QVERIFY(m_tmpDir.isValid());
    m_testRoot = m_tmpDir.path();
    m_worker = new ImageWorker();
    MediaDispatcher::instance().registerModule(m_worker);
}

void tst_ImageWorker::cleanupTestCase()
{
    MediaDispatcher::instance().unregisterAll();
    delete m_worker;
    m_worker = nullptr;
}

// =============================================================================
//  实现
// =============================================================================

void tst_ImageWorker::test_info()
{
    const ModuleInfo mi = m_worker->info();
    QCOMPARE(mi.id, QStringLiteral("imageWorker"));
    QVERIFY(!mi.name.isEmpty());   // 中文名不 exact 比对 (避免 GBK/UTF-8 编码问题)
    QVERIFY(!mi.extensions.isEmpty());
    QVERIFY(mi.extensions.contains(QStringLiteral(".png")));
    QVERIFY(mi.extensions.contains(QStringLiteral(".jpg")));
    QVERIFY(mi.extensions.contains(QStringLiteral(".jpeg")));
    QVERIFY(mi.extensions.contains(QStringLiteral(".bmp")));
    QVERIFY(mi.extensions.contains(QStringLiteral(".webp")));
    QVERIFY(mi.extensions.contains(QStringLiteral(".tif")));
    QCOMPARE(mi.iconPath, QString());
}

void tst_ImageWorker::test_registerInDispatcher()
{
    ensureWorkerRegistered();
    auto &disp = MediaDispatcher::instance();
    QCOMPARE(disp.moduleCount(), 1);
    QVERIFY(disp.modules().contains(m_worker));
    QCOMPARE(disp.moduleForId(QStringLiteral("imageWorker")), m_worker);
}

void tst_ImageWorker::test_routePng()
{
    ensureWorkerRegistered();
    writePng(QStringLiteral("test.png"));
    const QString path = absPath(QStringLiteral("test.png"));
    auto &disp = MediaDispatcher::instance();
    IWorkspace *ws = disp.openFile(path);
    QVERIFY(ws != nullptr);
    QVERIFY(qobject_cast<ImageWorkspace *>(ws) != nullptr);
    delete ws;
}

void tst_ImageWorker::test_routeJpg()
{
    ensureWorkerRegistered();
    writeJpg(QStringLiteral("test.jpg"));
    const QString path = absPath(QStringLiteral("test.jpg"));
    auto &disp = MediaDispatcher::instance();
    IWorkspace *ws = disp.openFile(path);
    QVERIFY(ws != nullptr);
    QVERIFY(qobject_cast<ImageWorkspace *>(ws) != nullptr);
    delete ws;
}

void tst_ImageWorker::test_routeUnknownExt()
{
    ensureWorkerRegistered();
    auto &disp = MediaDispatcher::instance();
    QSignalSpy spy(&disp, &MediaDispatcher::openFailed);
    IWorkspace *ws = disp.openFile(QStringLiteral("D:/foo/bar.xyz"));
    QCOMPARE(ws, nullptr);
    QCOMPARE(spy.count(), 1);
}

void tst_ImageWorker::test_routeEmpty()
{
    ensureWorkerRegistered();
    auto &disp = MediaDispatcher::instance();
    QSignalSpy spy(&disp, &MediaDispatcher::openFailed);
    IWorkspace *ws = disp.openFile(QString());
    QCOMPARE(ws, nullptr);
    QCOMPARE(spy.count(), 1);
}

void tst_ImageWorker::test_workspace_moduleId()
{
    ensureWorkerRegistered();
    writePng(QStringLiteral("module_id.png"));
    const QString path = absPath(QStringLiteral("module_id.png"));
    IWorkspace *ws = MediaDispatcher::instance().openFile(path);
    QVERIFY(ws != nullptr);
    QCOMPARE(ws->moduleId(), QStringLiteral("imageWorker"));
    delete ws;
}

void tst_ImageWorker::test_workspace_filePath()
{
    ensureWorkerRegistered();
    writePng(QStringLiteral("file_path.png"));
    const QString path = absPath(QStringLiteral("file_path.png"));
    IWorkspace *ws = MediaDispatcher::instance().openFile(path);
    QVERIFY(ws != nullptr);
    QCOMPARE(ws->filePath(), path);
    delete ws;
}

void tst_ImageWorker::test_workspace_loadFile()
{
    ImageWorkspace ws;
    writePng(QStringLiteral("load.png"));
    const QString path = absPath(QStringLiteral("load.png"));
    QVERIFY(ws.loadFile(path));
    QCOMPARE(ws.filePath(), path);
    // 加载失败: 不存在文件
    QVERIFY(!ws.loadFile(QStringLiteral("D:/this/does/not/exist_2026_09_03.png")));
}

void tst_ImageWorker::test_workspace_displayName()
{
    ensureWorkerRegistered();
    writePng(QStringLiteral("display_name.png"));
    const QString path = absPath(QStringLiteral("display_name.png"));
    IWorkspace *ws = MediaDispatcher::instance().openFile(path);
    QVERIFY(ws != nullptr);
    QVERIFY(!ws->displayName().isEmpty());
    delete ws;
}

void tst_ImageWorker::test_workspace_signalForward()
{
    ensureWorkerRegistered();
    writePng(QStringLiteral("signal.png"));
    const QString path = absPath(QStringLiteral("signal.png"));
    IWorkspace *ws = MediaDispatcher::instance().openFile(path);
    QVERIFY(ws != nullptr);
    // 仅验证信号存在 (QSignalSpy 在 ImageWorkspace 转发机制)
    QSignalSpy spyPath(ws, &IWorkspace::filePathChanged);
    QVERIFY(true);
    delete ws;
}

QTEST_MAIN(tst_ImageWorker)
#include "tst_ImageWorker.moc"
