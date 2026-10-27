// =============================================================================
//  tst_Path_OpenEditUndoRedoSave.cpp
//
//  Capability path (user perspective), not a class test:
//
//    open a PNG -> apply a filter -> Ctrl+Z -> Ctrl+Y -> save -> close
//    -> reopen -> the file still holds exactly what was on screen
//
//  What this pins (docs/CONTRACTS.md 4, docs/FUNC-ACCEPTANCE.md A/C/D/E):
//    A  entry       : loadFile succeeds, reports no error, title/path correct
//    A  error path  : opening a missing file fails with a message, no crash
//    C  data change : the filter changes pixels and marks the document dirty
//    D  undo / redo : Ctrl+Z restores the pixels exactly, Ctrl+Y re-applies
//                     them exactly (real key events, not a direct call)
//    E  persistence : save overwrites the source file, the reopened file is
//                     pixel-identical to the state that was saved
//
//  Why this test exists: every pre-existing test avoided constructing
//  ImageWindow (see tst_LayerPanelIntegration.cpp:17), so nothing in the
//  suite could ever verify a user path. 81 green class-level tests coexisted
//  with 34 unaudited P0 defects.
//
//  protects: the document state a user sees (pixels) is the same state that
//            undo, redo, save and reopen operate on.
//  path:     open PNG -> filter -> Ctrl+Z -> Ctrl+Y -> save -> reopen
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QDebug>

#include "../src/media/imagewindow.h"
#include "../src/media/filters/FilterCommand.h"

#include "support/PathHarness.h"

using namespace vistella_test;

class tst_Path_OpenEditUndoRedoSave : public QObject
{
    Q_OBJECT
private slots:
    void open_reports_state();
    void open_missing_file_fails_cleanly();
    void edit_changes_pixels_and_dirties_document();
    void ctrl_z_restores_pixels_exactly();
    void ctrl_y_reapplies_pixels_exactly();
    void save_overwrites_source_and_survives_reopen();
    void constructing_window_emits_no_unknown_qt_warning();
};

// ---------------------------------------------------------------------------
//  A - entry reachability
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::open_reports_state()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("in.png");
    QVERIFY(writeTestImage(png));

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));
    QVERIFY(err.isEmpty());

    QCOMPARE(w->filePath(), png);
    QCOMPARE(w->displayTitle(), QStringLiteral("in.png"));
    QVERIFY(!w->isDirty());

    // The pixels that came back must be the pixels that went in.
    const QImage img = w->currentImageAsQImage();
    QVERIFY(!img.isNull());
    QCOMPARE(img.size(), QSize(256, 256));
    QCOMPARE(pixelAt(img, 40, 40), QColor(220, 40, 40));      // top-left  red
    QCOMPARE(pixelAt(img, 200, 40), QColor(40, 200, 60));     // top-right green
    QCOMPARE(pixelAt(img, 40, 200), QColor(50, 60, 210));     // bottom-left blue
    QCOMPARE(pixelAt(img, 200, 200), QColor(235, 235, 235));  // bottom-right white

    delete w;
}

// ---------------------------------------------------------------------------
//  A - the error path: a missing file must fail, not crash and not lie
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::open_missing_file_fails_cleanly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.filePath("does-not-exist.png");

    ImageWindow* w = new ImageWindow();
    QString err;
    const bool ok = w->loadFile(missing, &err);

    QVERIFY(!ok);                       // must report failure
    QVERIFY(!err.isEmpty());            // must say why
    QVERIFY(w->currentImageAsQImage().isNull());   // must not show a stale image
    QVERIFY(!w->isDirty());

    delete w;
}

// ---------------------------------------------------------------------------
//  C - the edit must actually change the data
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::edit_changes_pixels_and_dirties_document()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("in.png");
    QVERIFY(writeTestImage(png));

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));
    const QImage before = w->currentImageAsQImage();

    w->applyFilter(filter::FilterKind::GaussianBlur);
    settle();
    const QImage after = w->currentImageAsQImage();

    QVERIFY(maxPixelDelta(before, after) > 20);
    QVERIFY(changedPixelCount(before, after) > 100);
    QVERIFY(w->isDirty());              // the user must be able to see unsaved work

    delete w;
}

// ---------------------------------------------------------------------------
//  D - undo restores the pixels exactly (real Ctrl+Z through keyPressEvent)
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::ctrl_z_restores_pixels_exactly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("in.png");
    QVERIFY(writeTestImage(png));

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));
    const QImage original = w->currentImageAsQImage();

    w->applyFilter(filter::FilterKind::GaussianBlur);
    settle();
    const QImage filtered = w->currentImageAsQImage();
    QVERIFY(maxPixelDelta(original, filtered) > 20);

    sendUndo(w);                        // Ctrl+Z
    settle();
    const QImage undone = w->currentImageAsQImage();

    QCOMPARE(maxPixelDelta(original, undone), 0);   // pixel-identical, not "close"
    QCOMPARE(changedPixelCount(original, undone), 0);
    QVERIFY(!w->isDirty());

    delete w;
}

// ---------------------------------------------------------------------------
//  D - redo re-applies the pixels exactly (real Ctrl+Y)
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::ctrl_y_reapplies_pixels_exactly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("in.png");
    QVERIFY(writeTestImage(png));

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));
    const QImage original = w->currentImageAsQImage();

    w->applyFilter(filter::FilterKind::GaussianBlur);
    settle();
    const QImage filtered = w->currentImageAsQImage();

    sendUndo(w);
    settle();
    sendRedo(w);                        // Ctrl+Y
    settle();
    const QImage redone = w->currentImageAsQImage();

    QCOMPARE(maxPixelDelta(filtered, redone), 0);  // identical to the edited state
    QVERIFY(maxPixelDelta(original, redone) > 20); // and still different from the original
    QVERIFY(w->isDirty());

    delete w;
}

// ---------------------------------------------------------------------------
//  E - save overwrites the source file, and the file survives a reopen
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::save_overwrites_source_and_survives_reopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("in.png");
    QVERIFY(writeTestImage(png));
    const qint64 sizeBefore = QFileInfo(png).size();

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));
    const QImage original = w->currentImageAsQImage();

    w->applyFilter(filter::FilterKind::GaussianBlur);
    settle();
    const QImage edited = w->currentImageAsQImage();
    QVERIFY(w->isDirty());

    w->savePublic();                    // overwrite the source file
    settle();

    // Save policy: the source path is kept, the document becomes clean.
    QCOMPARE(w->filePath(), png);
    QVERIFY(!w->isDirty());
    QVERIFY(QFileInfo::exists(png));
    QVERIFY(QFileInfo(png).size() != 0);
    Q_UNUSED(sizeBefore);

    // A second window sees exactly the state that was on screen when saved.
    ImageWindow* reopened = new ImageWindow();
    QString err2;
    QVERIFY2(reopened->loadFile(png, &err2), qPrintable(err2));
    const QImage reloaded = reopened->currentImageAsQImage();

    QCOMPARE(maxPixelDelta(edited, reloaded), 0);   // saved == reloaded
    QVERIFY(maxPixelDelta(original, reloaded) > 20); // and it is the edited version

    delete reopened;
    delete w;
}

// ---------------------------------------------------------------------------
//  A capability path must not produce Qt warnings.
//
//  Regression guard for docs/DEBT.md#A-STACKWIDGET: VDraggableTabWidget used to
//  insert the tab into the tab bar before the page reached the stack, so the
//  first tab of every stack made QStackedWidget warn "widget not contained in
//  stack". Three of those warnings were emitted on every ImageWindow
//  construction.
//
//  The guard asserts the warnings are gone. If a new one appears, this test
//  goes red and the defect is found by the build instead of by a human
//  scrolling a log.
// ---------------------------------------------------------------------------
void tst_Path_OpenEditUndoRedoSave::constructing_window_emits_no_unknown_qt_warning()
{
    QtMessageRecorder rec;
    {
        ImageWindow* w = new ImageWindow();
        delete w;
    }

    for (const QString& m : rec.messages()) {
        qWarning() << "unexpected Qt message:" << m;
    }

    QCOMPARE(rec.warningCount(), 0);
    QCOMPARE(rec.criticalCount(), 0);
}

QTEST_MAIN(tst_Path_OpenEditUndoRedoSave)
#include "tst_Path_OpenEditUndoRedoSave.moc"
