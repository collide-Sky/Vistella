// =============================================================================
//  tst_FileTreeModel — FileTreeModel QAbstractItemModel 子类测试
//  需要 QApplication (QFileIconProvider 走 GUI)
//  用 QTemporaryDir 建测试目录
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "FileTreeModel.h"
#include "FileTreeItem.h"
#include "../src/core/FileExtensionRegistry.h"

class tst_FileTreeModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();       // 建测试目录
    void cleanupTestCase();    // 清理

    // 基础 API
    void test_emptyModel();
    void test_setRootPath_empty();
    void test_setRootPath_nonexistent();
    void test_setRootPath_valid();

    // 扩展名过滤 (决策 5)
    void test_extensionFilter();

    // lazy load
    void test_lazyLoad();
    void test_lazyLoad_idempotent();

    // hasChildren 行为
    void test_hasChildren();

    // 静态工具
    void test_pathForIndex();
    void test_itemForIndex_invalid();

    // 角色
    void test_dataRoles();

private:
    QTemporaryDir m_tmpDir;            // 测试根 (auto delete)
    QString       m_testRoot;          // 测试根路径

    // 建测试文件 (相对 m_testRoot)
    //   QVERIFY2 在 fail 时早 return, 函数本身用 void (不需要返回值, 调用方不依赖)
    void writeFile(const QString &rel, const QByteArray &content = QByteArrayLiteral("x"));
    void writeDir(const QString &rel);
};

void tst_FileTreeModel::writeFile(const QString &rel, const QByteArray &content)
{
    const QString abs = m_testRoot + QStringLiteral("/") + rel;
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QFile f(abs);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(abs));
    f.write(content);
    f.close();
}

void tst_FileTreeModel::writeDir(const QString &rel)
{
    const QString abs = m_testRoot + QStringLiteral("/") + rel;
    QVERIFY2(QDir().mkpath(abs), qPrintable(abs));
}

void tst_FileTreeModel::initTestCase()
{
    QVERIFY(m_tmpDir.isValid());
    m_testRoot = m_tmpDir.path();
    QVERIFY(!m_testRoot.isEmpty());
}

void tst_FileTreeModel::cleanupTestCase()
{
    // QTemporaryDir 自动删
}

// =============================================================================
//  实现
// =============================================================================

void tst_FileTreeModel::test_emptyModel()
{
    FileTreeModel m;
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.columnCount(), 1);
    // 没设 rootPath 之前, invalid index
    QVERIFY(!m.index(0, 0).isValid());
    // rootPath() 返空
    QCOMPARE(m.rootPath(), QString());
}

void tst_FileTreeModel::test_setRootPath_empty()
{
    FileTreeModel m;
    m.setRootPath(QString());   // 清空
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.rootPath(), QString());
}

void tst_FileTreeModel::test_setRootPath_nonexistent()
{
    FileTreeModel m;
    m.setRootPath(QStringLiteral("D:/this/path/does/not/exist/anywhere_2026_09_03"));
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.rootPath(), QString());   // 不存在路径 rootPath 不更新
}

void tst_FileTreeModel::test_setRootPath_valid()
{
    writeFile(QStringLiteral("a.png"));
    writeFile(QStringLiteral("b.jpg"));
    writeFile(QStringLiteral("c.exe"));   // 不支持, 不应该显示

    FileTreeModel m;
    m.setRootPath(m_testRoot);
    QCOMPARE(m.rootPath(), m_testRoot);

    // 顶层 = 1 个根目录项
    QCOMPARE(m.rowCount(), 1);
    QModelIndex root = m.index(0, 0);
    QVERIFY(root.isValid());
    QCOMPARE(m.data(root, FileTreeModel::PathRole).toString(), m_testRoot);
    QVERIFY(m.data(root, FileTreeModel::IsDirRole).toBool());

    // 目录没 expand 之前: lazy placeholder, hasChildren = true
    QVERIFY(m.hasChildren(root));
    // 但 rowCount 仍 1 (只占位)
    QCOMPARE(m.rowCount(root), 1);
}

void tst_FileTreeModel::test_extensionFilter()
{
    // 清空 + 新建测试目录
    const QString sub = m_testRoot + QStringLiteral("/subdir_2026_09_03_filter");
    QDir().mkpath(sub);
    QFile f1(sub + QStringLiteral("/img.png"));
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write("x");
    f1.close();
    QFile f2(sub + QStringLiteral("/sound.mp3"));
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("x");
    f2.close();
    QFile f3(sub + QStringLiteral("/video.mp4"));
    QVERIFY(f3.open(QIODevice::WriteOnly));
    f3.write("x");
    f3.close();
    // 不支持的扩展名
    QFile f4(sub + QStringLiteral("/script.exe"));
    QVERIFY(f4.open(QIODevice::WriteOnly));
    f4.write("x");
    f4.close();
    QFile f5(sub + QStringLiteral("/doc.docx"));
    QVERIFY(f5.open(QIODevice::WriteOnly));
    f5.write("x");
    f5.close();

    FileTreeModel m;
    m.setRootPath(m_testRoot);
    QModelIndex root = m.index(0, 0);
    QVERIFY(root.isValid());

    m.expand(root);   // 真扫
    // 顶层应该只有 1 个子目录 (subdir_2026_09_03_filter)
    const int subCount = m.rowCount(root);
    QVERIFY(subCount >= 1);

    // 找子目录节点
    QModelIndex subIdx;
    for (int i = 0; i < subCount; ++i) {
        QModelIndex ch = m.index(i, 0, root);
        if (m.data(ch, Qt::DisplayRole).toString() == QStringLiteral("subdir_2026_09_03_filter")) {
            subIdx = ch;
            break;
        }
    }
    QVERIFY(subIdx.isValid());
    m.expand(subIdx);

    // 子目录里: 3 个支持扩展 (img/sound/video) + 2 个不支持 (script/doc) → 只显示 3 个
    const int fileCount = m.rowCount(subIdx);
    QCOMPARE(fileCount, 3);   // .exe / .docx 被过滤

    // 验证 moduleIdRole 填正确
    bool foundPng = false, foundMp3 = false, foundMp4 = false;
    for (int i = 0; i < fileCount; ++i) {
        QModelIndex fi = m.index(i, 0, subIdx);
        const QString name = m.data(fi, Qt::DisplayRole).toString();
        const QString mid = m.data(fi, FileTreeModel::ModuleIdRole).toString();
        if (name == QStringLiteral("img.png")) {
            QCOMPARE(mid, QStringLiteral("imageWorker"));
            foundPng = true;
        } else if (name == QStringLiteral("sound.mp3")) {
            QCOMPARE(mid, QStringLiteral("audioWorker"));
            foundMp3 = true;
        } else if (name == QStringLiteral("video.mp4")) {
            QCOMPARE(mid, QStringLiteral("videoWorker"));
            foundMp4 = true;
        }
    }
    QVERIFY(foundPng);
    QVERIFY(foundMp3);
    QVERIFY(foundMp4);
}

void tst_FileTreeModel::test_lazyLoad()
{
    const QString sub = m_testRoot + QStringLiteral("/lazy_2026_09_03");
    QDir().mkpath(sub);
    // 必须用 registry 支持的扩展名 (.png / .jpg / .mp3 / .mp4 等), .txt 会被过滤
    QFile f(sub + QStringLiteral("/inside.png"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    FileTreeModel m;
    m.setRootPath(m_testRoot);
    QModelIndex root = m.index(0, 0);
    m.expand(root);

    // 找 lazy_2026_09_03 子目录
    QModelIndex subIdx;
    for (int i = 0; i < m.rowCount(root); ++i) {
        QModelIndex ch = m.index(i, 0, root);
        if (m.data(ch, Qt::DisplayRole).toString() == QStringLiteral("lazy_2026_09_03")) {
            subIdx = ch;
            break;
        }
    }
    QVERIFY(subIdx.isValid());

    // expand 前: 只有占位 child
    QCOMPARE(m.rowCount(subIdx), 1);
    QVERIFY(m.hasChildren(subIdx));

    // expand 触发 lazy load
    m.expand(subIdx);
    QVERIFY(m.rowCount(subIdx) >= 1);   // 真扫了, 看到 inside.png
}

void tst_FileTreeModel::test_lazyLoad_idempotent()
{
    const QString sub = m_testRoot + QStringLiteral("/lazy_idem_2026_09_03");
    QDir().mkpath(sub);
    QFile f(sub + QStringLiteral("/file.png"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    FileTreeModel m;
    m.setRootPath(m_testRoot);
    QModelIndex root = m.index(0, 0);
    m.expand(root);

    QModelIndex subIdx;
    for (int i = 0; i < m.rowCount(root); ++i) {
        QModelIndex ch = m.index(i, 0, root);
        if (m.data(ch, Qt::DisplayRole).toString() == QStringLiteral("lazy_idem_2026_09_03")) {
            subIdx = ch;
            break;
        }
    }
    QVERIFY(subIdx.isValid());

    m.expand(subIdx);
    const int firstCount = m.rowCount(subIdx);
    QVERIFY(firstCount >= 1);

    // 再 expand 一次, count 不应该变 (idempotent, no double-load)
    m.expand(subIdx);
    QCOMPARE(m.rowCount(subIdx), firstCount);
}

void tst_FileTreeModel::test_hasChildren()
{
    FileTreeModel m;
    // 空 model: hasChildren(invalid) = false
    QVERIFY(!m.hasChildren(QModelIndex()));

    // 顶层 root 还没设, hasChildren(invalid) = false
    QCOMPARE(m.rowCount(), 0);

    writeFile(QStringLiteral("only.png"));
    m.setRootPath(m_testRoot);

    QModelIndex root = m.index(0, 0);
    QVERIFY(m.hasChildren(root));   // 目录永远 hasChildren (lazy load 用)
}

void tst_FileTreeModel::test_pathForIndex()
{
    FileTreeModel m;
    // invalid index 返空
    QCOMPARE(FileTreeModel::pathForIndex(QModelIndex()), QString());
    QCOMPARE(FileTreeModel::isDirForIndex(QModelIndex()), false);
    QCOMPARE(FileTreeModel::itemForIndex(QModelIndex()), nullptr);

    writeFile(QStringLiteral("p_2026_09_03.png"));
    m.setRootPath(m_testRoot);
    QModelIndex root = m.index(0, 0);
    QCOMPARE(FileTreeModel::pathForIndex(root), m_testRoot);
    QVERIFY(FileTreeModel::isDirForIndex(root));
    QVERIFY(FileTreeModel::itemForIndex(root) != nullptr);
}

void tst_FileTreeModel::test_itemForIndex_invalid()
{
    FileTreeModel m;
    // 没设 rootPath 之前所有 index 都 invalid, 静态工具返 nullptr / 空
    QModelIndex bad;
    QCOMPARE(FileTreeModel::itemForIndex(bad), nullptr);
    QCOMPARE(FileTreeModel::pathForIndex(bad), QString());
    QCOMPARE(FileTreeModel::isDirForIndex(bad), false);
}

void tst_FileTreeModel::test_dataRoles()
{
    writeFile(QStringLiteral("role_test.png"));
    FileTreeModel m;
    m.setRootPath(m_testRoot);
    QModelIndex root = m.index(0, 0);
    QVERIFY(root.isValid());

    // DisplayRole = 根目录名 (m_testRoot 的 basename)
    const QString name = m.data(root, Qt::DisplayRole).toString();
    QVERIFY(!name.isEmpty());

    // PathRole = 绝对路径
    QCOMPARE(m.data(root, FileTreeModel::PathRole).toString(), m_testRoot);

    // IsDirRole = true
    QVERIFY(m.data(root, FileTreeModel::IsDirRole).toBool());

    // 未知 role 返 invalid QVariant
    QVERIFY(!m.data(root, Qt::UserRole + 99).isValid());
}

QTEST_MAIN(tst_FileTreeModel)
#include "tst_FileTreeModel.moc"
