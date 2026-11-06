// SPDX-License-Identifier: MIT
//
// tst_M2_2_DockLayout.cpp
//
// M2-2: the persistent dock layout (the save / restore / reset trio).
//
// Before M2-2 the "reset layout" action read a workspace_default.ini that
// nothing ever wrote, so it was a no-op that could only ever show an
// error message (audit APP-19). The panel system now uses the native
// QDockWidget + QMainWindow::saveState / restoreState pair, so the
// arrangement can actually be persisted.
//
// What this test covers:
//   - save() writes a file that restore() can read back.
//   - A changed arrangement (a dock moved to another edge) survives a
//     save / restore round trip.
//   - reset() goes back to the arrangement the user declared as default.
//   - The live layout and the default layout are two different files:
//     moving a panel and then resetting must land on the DEFAULT, not on
//     the arrangement the user just moved.
//   - The failure paths report a reason instead of silently doing nothing.
//
// The manager is tested on a bare QMainWindow with its own docks, so the
// test does not depend on the real MainWindow's dock set.

#include <QTest>
#include <QApplication>
#include <QDir>
#include <QMainWindow>
#include <QDockWidget>
#include <QTemporaryDir>

#include "../src/app/DockLayoutManager.h"

using app::DockLayoutManager;

class tst_M2_2_DockLayout : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // The three pieces.
    void save_writesAFile();
    void restore_withoutASavedLayout_fails();
    void saveThenRestore_keepsTheArrangement();
    void reset_withoutADefault_keepsTheCurrentLayout();
    void saveAsDefault_thenReset_goesBackToTheDefault();
    void reset_doesNotGoBackToTheLiveLayout();

    // Failure paths.
    void save_withoutAWindow_fails();
    void restore_withoutAWindow_fails();

    // File hygiene.
    void savedFile_isNotEmpty();
    void layoutFiles_liveInTheConfiguredDirectory();

private:
    // Build a window with two docks on opposite edges.
    static QMainWindow* makeWindow(QDockWidget** left, QDockWidget** right);
    static void destroyWindow(QMainWindow* w, QDockWidget* left, QDockWidget* right);

    // A fresh directory per test.
    //
    // The layout files live on disk, so tests that expect "no saved
    // layout" must not see the file a previous test wrote. A shared
    // temp dir would make the order of the tests decide the outcome.
    QString freshDir(const char* name);

    QTemporaryDir m_root;
};

void tst_M2_2_DockLayout::init()
{
    QVERIFY(m_root.isValid());
}

void tst_M2_2_DockLayout::cleanup()
{
}

QString tst_M2_2_DockLayout::freshDir(const char* name)
{
    const QString path = QDir(m_root.path()).filePath(QString::fromLatin1(name));
    QDir().mkpath(path);
    return path;
}

QMainWindow* tst_M2_2_DockLayout::makeWindow(QDockWidget** left, QDockWidget** right)
{
    auto* w = new QMainWindow();
    w->resize(800, 600);

    auto* l = new QDockWidget(QStringLiteral("Left"), w);
    l->setObjectName(QStringLiteral("testLeftDock"));
    l->setWidget(new QWidget());
    w->addDockWidget(Qt::LeftDockWidgetArea, l);

    auto* r = new QDockWidget(QStringLiteral("Right"), w);
    r->setObjectName(QStringLiteral("testRightDock"));
    r->setWidget(new QWidget());
    w->addDockWidget(Qt::RightDockWidgetArea, r);

    *left = l;
    *right = r;
    return w;
}

void tst_M2_2_DockLayout::destroyWindow(QMainWindow* w, QDockWidget*, QDockWidget*)
{
    delete w;
}

// ---------------------------------------------------------------------------
// The three pieces
// ---------------------------------------------------------------------------
void tst_M2_2_DockLayout::save_writesAFile()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("save_writes"));

    QVERIFY2(mgr.save(), qPrintable(mgr.lastError()));
    QVERIFY(QFileInfo::exists(DockLayoutManager::currentLayoutPath(freshDir("save_writes"))));

    destroyWindow(w, l, r);
}

void tst_M2_2_DockLayout::restore_withoutASavedLayout_fails()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("restore_empty"));

    QVERIFY(!mgr.restore());
    QVERIFY2(!mgr.lastError().isEmpty(),
             "a failed restore must say why, not fail silently");

    destroyWindow(w, l, r);
}

void tst_M2_2_DockLayout::saveThenRestore_keepsTheArrangement()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("saverestore"));

    // Move the right dock to the bottom -- an arrangement change the
    // user made.
    w->addDockWidget(Qt::BottomDockWidgetArea, r);
    QVERIFY(!mgr.save() ? false : true);

    // Now put it back and restore: the saved arrangement must win.
    w->addDockWidget(Qt::RightDockWidgetArea, r);
    QVERIFY2(mgr.restore(), qPrintable(mgr.lastError()));

    QVERIFY2(w->dockWidgetArea(r) == Qt::BottomDockWidgetArea,
             "the restored dock must be back where it was saved");

    destroyWindow(w, l, r);
}

void tst_M2_2_DockLayout::reset_withoutADefault_keepsTheCurrentLayout()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("reset_nodefault"));

    QVERIFY(!mgr.hasDefault());
    QVERIFY2(mgr.reset(), qPrintable(mgr.lastError()));
    // Nothing was declared as the default, so the built-in arrangement
    // stands -- the right dock is still on the right.
    QVERIFY(w->dockWidgetArea(r) == Qt::RightDockWidgetArea);

    destroyWindow(w, l, r);
}

void tst_M2_2_DockLayout::saveAsDefault_thenReset_goesBackToTheDefault()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("savedefault"));

    // The user declares "bottom right" as the default.
    w->addDockWidget(Qt::BottomDockWidgetArea, r);
    QVERIFY2(mgr.saveAsDefault(), qPrintable(mgr.lastError()));
    QVERIFY(mgr.hasDefault());

    // Then they move it somewhere else for the current session.
    w->addDockWidget(Qt::LeftDockWidgetArea, r);
    QVERIFY(w->dockWidgetArea(r) == Qt::LeftDockWidgetArea);

    QVERIFY2(mgr.reset(), qPrintable(mgr.lastError()));
    QVERIFY2(w->dockWidgetArea(r) == Qt::BottomDockWidgetArea,
             "reset must land on the declared default, not on the live layout");

    destroyWindow(w, l, r);
}

void tst_M2_2_DockLayout::reset_doesNotGoBackToTheLiveLayout()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("reset_vs_live"));

    // Default = bottom.
    w->addDockWidget(Qt::BottomDockWidgetArea, r);
    QVERIFY(mgr.saveAsDefault());

    // Live layout = left, and it is saved on exit.
    w->addDockWidget(Qt::LeftDockWidgetArea, r);
    QVERIFY(mgr.save());

    // Reset must go to the DEFAULT (bottom), even though the live layout
    // on disk says "left". This is the distinction that made the old
    // single-file design impossible.
    QVERIFY(mgr.reset());
    QVERIFY2(w->dockWidgetArea(r) == Qt::BottomDockWidgetArea,
             "reset went back to the live layout instead of the default");

    destroyWindow(w, l, r);
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------
void tst_M2_2_DockLayout::save_withoutAWindow_fails()
{
    DockLayoutManager mgr;
    mgr.setDirectory(freshDir("save_nowindow"));
    QVERIFY(!mgr.save());
    QVERIFY(!mgr.lastError().isEmpty());
}

void tst_M2_2_DockLayout::restore_withoutAWindow_fails()
{
    DockLayoutManager mgr;
    mgr.setDirectory(freshDir("restore_nowindow"));
    QVERIFY(!mgr.restore());
    QVERIFY(!mgr.lastError().isEmpty());
}

// ---------------------------------------------------------------------------
// File hygiene
// ---------------------------------------------------------------------------
void tst_M2_2_DockLayout::savedFile_isNotEmpty()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("file_not_empty"));
    QVERIFY(mgr.save());

    QFile f(DockLayoutManager::currentLayoutPath(freshDir("file_not_empty")));
    QVERIFY(f.exists());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray payload = f.readAll();
    f.close();
    QVERIFY2(!payload.isEmpty(), "the layout file must not be empty");
    // It must actually carry a state blob, otherwise restore() would
    // have nothing to apply.
    QVERIFY(payload.contains("state="));

    destroyWindow(w, l, r);
}

void tst_M2_2_DockLayout::layoutFiles_liveInTheConfiguredDirectory()
{
    QDockWidget *l = nullptr, *r = nullptr;
    QMainWindow* w = makeWindow(&l, &r);

    DockLayoutManager mgr;
    mgr.setWindow(w);
    mgr.setDirectory(freshDir("files_in_dir"));

    QVERIFY(mgr.save());
    QVERIFY(mgr.saveAsDefault());

    const QString cur = DockLayoutManager::currentLayoutPath(freshDir("files_in_dir"));
    const QString def = DockLayoutManager::defaultLayoutPath(freshDir("files_in_dir"));
    QVERIFY(QFileInfo::exists(cur));
    QVERIFY(QFileInfo::exists(def));
    QVERIFY2(cur != def, "the live layout and the default layout are two files");

    destroyWindow(w, l, r);
}

QTEST_MAIN(tst_M2_2_DockLayout)
#include "tst_M2_2_DockLayout.moc"
