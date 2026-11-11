// =============================================================================
//  tst_M2_7_FileTree - the file tree after the switch to QFileSystemModel
//
//  M2-7b replaced the hand written FileTreeModel with
//  QFileSystemModel + QSortFilterProxyModel. The hand written model had
//  two defects this test pins down:
//
//    - it scanned every directory on the UI thread (two full scans plus
//      a stat() per file), so a big directory froze the UI,
//    - hasChildren() answered "yes" for EVERY directory, so an empty
//      directory kept its expand arrow forever (UI-07).
//
//  What is checked here:
//    - the extension filter hides the files the FileExtensionRegistry
//      does not know, in every level of the tree,
//    - a directory always passes the filter (the user has to be able to
//      walk into it),
//    - an empty directory has NO expand arrow once it has been read,
//    - a directory with something inside keeps its arrow,
//    - the InfoTreeDock API (setRootPath / rootPath /
//      openFileRequested) behaves the same as before.
//
//  The file system model reads the directories in a worker thread, so
//  the test waits for the directoryLoaded signal instead of assuming
//  the tree is filled synchronously.
//
//  The root index belongs to the VIEW (QAbstractItemView::setRootIndex);
//  the model has no such concept, so the test walks the tree through
//  the root index of the view.
//
//  ---------------------------------------------------------------------------
//  THE TEST NAVIGATES BY PATH, NOT BY INDEX
//  ---------------------------------------------------------------------------
//  A QModelIndex of the proxy does not survive a change of the source
//  model: QSortFilterProxyModel drops its internal mapping and rebuilds
//  it, and the index then points at freed memory. Using such an index
//  crashes with
//
//      ASSERT: "it != source_index_mapping.constEnd()"
//
//  The path is the stable identity of a tree entry, so every lookup in
//  this test goes through FileTreeProxyModel::indexForPath() and the
//  result is used right away.
// =============================================================================

#include <QTest>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeView>

#include "FileTreeProxyModel.h"
#include "infotreedock.h"

class tst_M2_7_FileTree : public QObject
{
    Q_OBJECT

private:
    // A file system model plus the proxy in front of it.
    struct Fixture {
        QFileSystemModel  fs;
        FileTreeProxyModel proxy;
        QSignalSpy        spy;

        Fixture()
            : spy(&fs, &QFileSystemModel::directoryLoaded)
        {
            proxy.setSourceModel(&fs);
        }
    };

    QTemporaryDir m_tmp;
    QString m_root;
    int m_case = 0;

    void writeFile(const QString& rel, const QByteArray& content = QByteArrayLiteral("x"));
    void writeDir(const QString& rel);

    // Wait until the file system model has read the given directory.
    bool waitForDirectory(Fixture& f, const QString& path);

    // The index of the root directory.
    QModelIndex rootIndex(Fixture& f) const;

    // The index of the child with the given name (or an invalid index).
    // The name is the FILE name, not the path.
    QModelIndex childNamed(FileTreeProxyModel& proxy, const QModelIndex& parent,
                            const QString& name) const;

    // The names of the children of an index, in the order of the model.
    QStringList childNames(FileTreeProxyModel& proxy, const QModelIndex& parent) const;

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // ----- the extension filter -----
    void unsupportedFilesAreHidden();
    void unsupportedFilesAreHiddenInEveryLevel();
    void directoriesAlwaysPassTheFilter();

    // ----- the helpers -----
    void pathAndDirectoryHelpers();
    void moduleIdIsReported();

    // ----- the expand arrow (UI-07) -----
    void emptyDirectoryHasNoExpandArrow();
    void nonEmptyDirectoryKeepsItsExpandArrow();

    // ----- the lazy fetch through the view -----
    void expandingARowInTheViewLoadsTheDirectory();

    // ----- a proxy index must not survive a change of the model -----
    void aProxyIndexDoesNotSurviveTheReadOfItsDirectory();

    // ----- the dock -----
    void dockKeepsItsApi();
    void dockEmitsTheOpenRequest();
};

void tst_M2_7_FileTree::writeFile(const QString& rel, const QByteArray& content)
{
    const QString abs = m_root + QLatin1Char('/') + rel;
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QFile f(abs);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(abs));
    f.write(content);
    f.close();
}

void tst_M2_7_FileTree::writeDir(const QString& rel)
{
    const QString abs = m_root + QLatin1Char('/') + rel;
    QVERIFY2(QDir().mkpath(abs), qPrintable(abs));
}

bool tst_M2_7_FileTree::waitForDirectory(Fixture& f, const QString& path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QStringList seen;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        for (const QList<QVariant>& args : f.spy) {
            const QString loaded = args.at(0).toString();
            if (!seen.contains(loaded)) seen << loaded;
            if (loaded == abs) return true;
        }
        QTest::qWait(20);
    }
    qWarning("waitForDirectory: %s was not loaded. Loaded so far: %s",
             qPrintable(abs), qPrintable(seen.join(QStringLiteral(", "))));
    return false;
}

QModelIndex tst_M2_7_FileTree::rootIndex(Fixture& f) const
{
    return f.proxy.indexForPath(m_root);
}

QModelIndex tst_M2_7_FileTree::childNamed(FileTreeProxyModel& proxy,
                                           const QModelIndex& parent,
                                           const QString& name) const
{
    const int n = proxy.rowCount(parent);
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = proxy.index(i, 0, parent);
        if (idx.data(Qt::DisplayRole).toString() == name) return idx;
    }
    return QModelIndex();
}

QStringList tst_M2_7_FileTree::childNames(FileTreeProxyModel& proxy,
                                           const QModelIndex& parent) const
{
    QStringList names;
    const int n = proxy.rowCount(parent);
    for (int i = 0; i < n; ++i) {
        names << proxy.index(i, 0, parent).data(Qt::DisplayRole).toString();
    }
    return names;
}

void tst_M2_7_FileTree::initTestCase()
{
    QVERIFY(QApplication::instance() != nullptr);
    QVERIFY(m_tmp.isValid());
}

void tst_M2_7_FileTree::init()
{
    // Every test case gets its own directory: otherwise the files of one
    // case show up in the tree of the next one.
    const QString dir = m_tmp.path() + QStringLiteral("/case_")
                       + QString::number(m_case++);
    QVERIFY(QDir().mkpath(dir));
    m_root = QFileInfo(dir).absoluteFilePath();
    QVERIFY(!m_root.isEmpty());
}

void tst_M2_7_FileTree::cleanup()
{
}

// =============================================================================
//  the extension filter
// =============================================================================

void tst_M2_7_FileTree::unsupportedFilesAreHidden()
{
    writeFile(QStringLiteral("visible.png"));
    writeFile(QStringLiteral("hidden.exe"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    QCOMPARE(f.proxy.rowCount(root), 1);

    const QModelIndex idx = f.proxy.index(0, 0, root);
    QVERIFY(idx.isValid());
    QCOMPARE(idx.data(Qt::DisplayRole).toString(), QStringLiteral("visible.png"));
}

void tst_M2_7_FileTree::unsupportedFilesAreHiddenInEveryLevel()
{
    writeFile(QStringLiteral("sub/inside.png"));
    writeFile(QStringLiteral("sub/script.exe"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    QCOMPARE(f.proxy.rowCount(root), 1);

    const QModelIndex sub = f.proxy.index(0, 0, root);
    QVERIFY(sub.isValid());
    QCOMPARE(sub.data(Qt::DisplayRole).toString(), QStringLiteral("sub"));

    // Read the sub directory. QFileSystemModel is lazy: the read of a
    // subdirectory starts with fetchMore().
    //
    // The fetch goes through the PATH, not through the proxy index: the
    // read changes the source model, and a proxy index may not survive
    // that (see the header of this file).
    QVERIFY(f.proxy.fetchDirectory(m_root + QStringLiteral("/sub")));
    QVERIFY(waitForDirectory(f, m_root + QStringLiteral("/sub")));

    // The index of the subdirectory is taken FRESH, after the read.
    const QModelIndex subFresh = f.proxy.indexForPath(m_root + QStringLiteral("/sub"));
    QVERIFY(subFresh.isValid());
    QCOMPARE(f.proxy.rowCount(subFresh), 1);
    QCOMPARE(childNames(f.proxy, subFresh), QStringList() << QStringLiteral("inside.png"));
}

void tst_M2_7_FileTree::directoriesAlwaysPassTheFilter()
{
    // A directory that holds nothing but unsupported files still shows up,
    // otherwise the user could not walk into it.
    writeFile(QStringLiteral("onlyunsupported/script.exe"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    QCOMPARE(f.proxy.rowCount(root), 1);

    const QModelIndex sub = f.proxy.index(0, 0, root);
    QVERIFY(sub.isValid());
    QCOMPARE(sub.data(Qt::DisplayRole).toString(), QStringLiteral("onlyunsupported"));
    QVERIFY(FileTreeProxyModel::isDirForIndex(sub));
}

// =============================================================================
//  the helpers
// =============================================================================

void tst_M2_7_FileTree::pathAndDirectoryHelpers()
{
    writeFile(QStringLiteral("helper.png"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    const QModelIndex idx = f.proxy.index(0, 0, root);
    QVERIFY(idx.isValid());

    const QString path = FileTreeProxyModel::pathForIndex(idx);
    QCOMPARE(path, QFileInfo(m_root + QStringLiteral("/helper.png")).absoluteFilePath());
    QVERIFY(!FileTreeProxyModel::isDirForIndex(idx));

    // An invalid index gives an empty path and "not a directory".
    QCOMPARE(FileTreeProxyModel::pathForIndex(QModelIndex()), QString());
    QVERIFY(!FileTreeProxyModel::isDirForIndex(QModelIndex()));
}

void tst_M2_7_FileTree::moduleIdIsReported()
{
    writeFile(QStringLiteral("module.png"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    const QModelIndex idx = f.proxy.index(0, 0, root);
    QVERIFY(idx.isValid());
    QCOMPARE(FileTreeProxyModel::moduleIdForIndex(idx), QStringLiteral("imageWorker"));
}

// =============================================================================
//  the expand arrow (UI-07)
// =============================================================================

void tst_M2_7_FileTree::emptyDirectoryHasNoExpandArrow()
{
    writeDir(QStringLiteral("empty"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    const QModelIndex sub = childNamed(f.proxy, root, QStringLiteral("empty"));
    QVERIFY(sub.isValid());

    // Before the directory has been read the arrow is optimistic: the
    // read happens in the background and the user must be able to open
    // the directory while it is loading.
    QVERIFY(f.proxy.hasChildren(sub));

    // Trigger the read and wait for it.
    QVERIFY(f.proxy.fetchDirectory(m_root + QStringLiteral("/empty")));
    QVERIFY(waitForDirectory(f, m_root + QStringLiteral("/empty")));

    // The index is taken FRESH: the read changed the source model and a
    // proxy index does not survive that.
    const QModelIndex subFresh = f.proxy.indexForPath(m_root + QStringLiteral("/empty"));
    QVERIFY(subFresh.isValid());

    // The directory turned out to be empty -> no arrow.
    QVERIFY2(!f.proxy.hasChildren(subFresh),
             "an empty directory still claims to have children");
}

void tst_M2_7_FileTree::nonEmptyDirectoryKeepsItsExpandArrow()
{
    writeFile(QStringLiteral("full/inside.png"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    const QModelIndex sub = childNamed(f.proxy, root, QStringLiteral("full"));
    QVERIFY(sub.isValid());

    QVERIFY(f.proxy.fetchDirectory(m_root + QStringLiteral("/full")));
    QVERIFY(waitForDirectory(f, m_root + QStringLiteral("/full")));

    const QModelIndex subFresh = f.proxy.indexForPath(m_root + QStringLiteral("/full"));
    QVERIFY(subFresh.isValid());

    QVERIFY2(f.proxy.hasChildren(subFresh),
             "a directory with a file in it lost its expand arrow");
    QCOMPARE(f.proxy.rowCount(subFresh), 1);
}

// =============================================================================
//  the lazy fetch through the view
// =============================================================================

void tst_M2_7_FileTree::expandingARowInTheViewLoadsTheDirectory()
{
    // This is the path the PRODUCT takes: the user clicks the arrow of
    // a row and the view asks the model for the children. The view calls
    // canFetchMore()/fetchMore() with the PROXY index; the proxy maps it
    // to the source and forwards the read.
    //
    // The test therefore goes through a real QTreeView and expands a
    // row; a crash in the model would abort the whole test binary.
    writeFile(QStringLiteral("sub/inside.png"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    QTreeView view;
    view.setModel(&f.proxy);
    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());
    view.setRootIndex(root);
    // The view has to be shown before a row can be expanded: QTreeView
    // only calls fetchMore() for an item it already knows (the items
    // are created while the view lays itself out).
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const QModelIndex sub = childNamed(f.proxy, root, QStringLiteral("sub"));
    QVERIFY(sub.isValid());

    // The view asks the model whether there is more to fetch, and then
    // fetches it. Both calls go through the proxy.
    QVERIFY(f.proxy.canFetchMore(sub));
    view.expand(sub);

    // The read happens in the worker thread of the file system model.
    QVERIFY(waitForDirectory(f, m_root + QStringLiteral("/sub")));

    // The index is taken FRESH, after the read.
    const QModelIndex subFresh = f.proxy.indexForPath(m_root + QStringLiteral("/sub"));
    QVERIFY(subFresh.isValid());
    QCOMPARE(childNames(f.proxy, subFresh), QStringList() << QStringLiteral("inside.png"));
}

// =============================================================================
//  a proxy index must not survive a change of the model
// =============================================================================

void tst_M2_7_FileTree::aProxyIndexDoesNotSurviveTheReadOfItsDirectory()
{
    // This test pins down the reason why the whole tree is navigated by
    // PATH and not by index.
    //
    // QSortFilterProxyModel keeps its row mapping in heap objects and
    // stores a raw pointer to them in the internalPointer of every proxy
    // index. When the source model changes, the proxy drops those
    // objects and rebuilds them. A proxy index that was taken BEFORE the
    // change then points at freed memory, and using it crashes with
    //
    //     ASSERT: "it != source_index_mapping.constEnd()"
    //
    // The read of a directory is exactly such a change: the file system
    // model reports the new rows with rowsInserted.
    //
    // So the test takes a proxy index, triggers the read of that very
    // directory, and then uses the index again. If the model ever gets
    // so defensive that the old index stays usable, this test would
    // still be green -- but as long as the index is unusable, EVERY
    // caller has to go through the path.
    writeFile(QStringLiteral("sub/inside.png"));

    Fixture f;
    f.fs.setRootPath(m_root);
    QVERIFY(waitForDirectory(f, m_root));

    const QModelIndex root = rootIndex(f);
    QVERIFY(root.isValid());

    // The index of the subdirectory, taken BEFORE the read.
    const QModelIndex subBefore = childNamed(f.proxy, root, QStringLiteral("sub"));
    QVERIFY(subBefore.isValid());

    // The read changes the source model.
    QVERIFY(f.proxy.fetchDirectory(m_root + QStringLiteral("/sub")));
    QVERIFY(waitForDirectory(f, m_root + QStringLiteral("/sub")));

    // The old index is not usable any more. The test does NOT touch it:
    // that is the crash. Instead it shows the way out -- the path still
    // works and gives the right answer.
    const QModelIndex subAfter = f.proxy.indexForPath(m_root + QStringLiteral("/sub"));
    QVERIFY(subAfter.isValid());
    QCOMPARE(childNames(f.proxy, subAfter), QStringList() << QStringLiteral("inside.png"));
}

// =============================================================================
//  the dock
// =============================================================================

void tst_M2_7_FileTree::dockKeepsItsApi()
{
    InfoTreeDock dock;
    QCOMPARE(dock.rootPath(), QString());

    // A path that does not exist is ignored, the old root stays.
    dock.setRootPath(QStringLiteral("D:/this/path/does/not/exist/2026_11_03"));
    QCOMPARE(dock.rootPath(), QString());

    dock.setRootPath(m_root);
    QCOMPARE(dock.rootPath(), m_root);

    // An empty path clears the root.
    dock.setRootPath(QString());
    QCOMPARE(dock.rootPath(), QString());
}

void tst_M2_7_FileTree::dockEmitsTheOpenRequest()
{
    writeFile(QStringLiteral("openthis.png"));

    InfoTreeDock dock;
    QSignalSpy spy(&dock, &InfoTreeDock::openFileRequested);

    // The proxy tells us when a directory has been read. The dock takes
    // the root index AGAIN in its directoryRead slot, because a proxy
    // index does not survive the change of the source model.
    //
    //   Waiting for THIS signal makes the test deterministic: the
    //   assertion below then always runs AFTER the slot, so a defect in
    //   the slot (a stale / cached index) is always seen. Waiting for
    //   the ROW to appear is NOT enough -- the row shows up before the
    //   slot has run, and the result then depends on the timing of the
    //   worker thread (the break check passed standalone and failed in
    //   a sequence).
    auto* proxy = dock.findChild<FileTreeProxyModel*>();
    QVERIFY(proxy != nullptr);
    QSignalSpy readSpy(proxy, &FileTreeProxyModel::directoryRead);

    dock.setRootPath(m_root);

    auto* tree = dock.findChild<QTreeView*>();
    QVERIFY(tree != nullptr);
    QVERIFY(tree->model() != nullptr);
    QVERIFY(tree->rootIndex().isValid());

    // The model reads the directory in a worker thread: wait until the
    // read is over.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && readSpy.count() < 1) {
        QTest::qWait(20);
    }
    QVERIFY2(readSpy.count() >= 1, "the directory was never read");

    // One more event loop turn, so the dock's slot (which is connected to
    // the same signal) has run.
    QTest::qWait(50);

    // The root index must still be usable after the directory was read.
    //
    //   This is the assertion that a stale (cached) index cannot
    //   satisfy: the index was taken before the source model changed,
    //   so it points at freed memory / at the wrong row.
    QVERIFY(tree->rootIndex().isValid());
    QCOMPARE(tree->model()->rowCount(tree->rootIndex()), 1);

    tree->setCurrentIndex(tree->model()->index(0, 0, tree->rootIndex()));
    QTest::keyClick(tree, Qt::Key_Return);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(),
             QFileInfo(m_root + QStringLiteral("/openthis.png")).absoluteFilePath());
}

QTEST_MAIN(tst_M2_7_FileTree)
#include "tst_M2_7_FileTree.moc"
