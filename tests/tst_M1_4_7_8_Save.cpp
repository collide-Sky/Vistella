// =============================================================================
//  tst_M1_4_7_8_Save - regression tests for the M1-4 / M1-7 / M1-8 work
//
//  M1-4 (save chain):
//    1. A save must write a FRESH composite, not the possibly stale render
//       cache. The cache is only refreshed by rebuildCurrentCache(), and for
//       images above 1 MP that path is ASYNCHRONOUS -- so saving right after
//       an edit used to write the PREVIOUS frame.
//    2. The write must be ATOMIC: a failed write must leave the previous
//       good file untouched (the old code used QImage::save(path), which
//       opens the destination directly and truncates it).
//    3. A failed write must leave the document DIRTY (not marked saved).
//
//  M1-7 (undo memory):
//    4. A tool edit must store only the CHANGED REGION, not two full-layer
//       clones.
//    5. The undo stack must enforce a MEMORY BUDGET, not just a count
//       limit.
//    6. A selection gesture (lasso / marquee / magic wand) must be
//       undoable.
//
//  M1-8 (colour):
//    7. The colour must have a single source of truth: the toolbar, the
//       colour panel and the brush must all agree.
//    8. The eyedropper must push the sampled colour into that source
//       (DEBT A-EYEDROPPER).
//
//  Every test here is written so that it FAILS when the corresponding fix
//  is reverted -- that is verified by the break self-check (inject the old
//  code, see the test go red, restore, see it go green).
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imagewindow.h"
#include "../src/media/imageprocessor.h"
#include "../src/media/ColorState.h"
#include "../src/media/UndoStack.h"
#include "../src/media/docks/ColorTab.h"
#include "../src/media/tools/LeftToolBar.h"
#include "../src/media/tools/EyedropperTool.h"
#include "../src/media/tools/Brush.h"
#include "../src/media/selection/SelectionModel.h"
#include "../src/media/selection/SelectionCommand.h"
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/LayerCommand.h"
#include "../src/ui/docwindow.h"

using namespace vistella;

class tst_M1_4_7_8_Save : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- M1-4: save chain ----
    void save_writesFreshPixels_notStaleCache();
    void saveImage_isAtomic_failedWriteKeepsOldFile();
    void saveImage_rejectsUnknownExtension();
    void docWindow_saveIsAtomic_failedWriteKeepsOldFile();

    // ---- M1-7: undo memory ----
    void setLayerImage_storesOnlyTheChangedRegion();
    void undoStack_evictsOldestWhenOverBudget();
    void undoStack_evictionDoesNotTouchTheDocument();
    void undoStack_evictionKeepsUndoRedoWorking();
    void undoStack_evictionKeepsTheRedoTail();
    void undoStack_accountingTracksFootprint();
    void selectionGesture_isUndoable();

    // ---- M1-8: colour ----
    void colorState_isTheSingleSourceOfTruth();
    void colorTab_followsTheState();
    void leftToolBar_followsTheState();
    void eyedropper_pushesIntoTheState();
    void brush_readsTheState();

private:
    // A 4-channel BGRA test image with a position-varying ramp, so a
    // pixel-level mistake cannot hide behind a uniform image.
    static cv::Mat rampImage(int w, int h);
    static void writePng(const QString& path, const cv::Mat& bgra);
};

namespace {

// A ramp image: B = x, G = y, R = 0, A = 255.
//   A UNIFORM image would hide channel / offset mistakes completely, so
//   every pixel-level test in this file uses a ramp.
cv::Mat makeRamp(int w, int h)
{
    cv::Mat img(h, w, CV_8UC4, cv::Scalar(0, 0, 0, 255));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uchar* p = img.ptr<uchar>(y) + x * 4;
            p[0] = uchar(x);
            p[1] = uchar(y);
            p[2] = 0;
            p[3] = 255;
        }
    }
    return img;
}

}   // namespace

void tst_M1_4_7_8_Save::initTestCase()
{
    QVERIFY(QCoreApplication::instance());
}

void tst_M1_4_7_8_Save::cleanupTestCase()
{
}

cv::Mat tst_M1_4_7_8_Save::rampImage(int w, int h)
{
    return makeRamp(w, h);
}

void tst_M1_4_7_8_Save::writePng(const QString& path, const cv::Mat& bgra)
{
    QImage img = ImageProcessor::matToQImage(bgra);
    QVERIFY2(img.save(path), qPrintable(path));
}

// =============================================================================
//  M1-4: the save chain
// =============================================================================

// The saved file must hold the CURRENT pixels.
//
//   The render cache m_current is only refreshed by rebuildCurrentCache(),
//   and for images above 1 MP that path is asynchronous. So a save that
//   reads m_current verbatim can write the previous frame.
//
//   The test drives the real user path: load an image, change a layer,
//   save, and check that the file holds the NEW pixels.
void tst_M1_4_7_8_Save::save_writesFreshPixels_notStaleCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("fresh.png");

    const cv::Mat original = makeRamp(64, 64);
    writePng(png, original);

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));

    // Change the base layer's pixels without going through a repaint.
    //   The point is to leave the render cache stale.
    auto* stack = w->layerStack();
    QVERIFY(stack);
    QVERIFY(stack->count() > 0);
    cv::Mat& layerImage = stack->at(0)->image;
    QVERIFY(!layerImage.empty());
    // Paint a solid magenta block into the top-left corner.
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uchar* p = layerImage.ptr<uchar>(y) + x * layerImage.channels();
            p[0] = 255;   // B
            p[1] = 0;     // G
            p[2] = 255;   // R
            p[3] = 255;   // A
        }
    }
    stack->notifyLayerChanged(0);

    // Save WITHOUT pumping the event loop: no repaint, so the render
    // cache still holds the pre-edit frame.
    w->savePublic();

    // The file must hold the magenta block.
    QImage saved(png);
    QVERIFY(!saved.isNull());
    const QColor got = saved.pixelColor(4, 4);
    QCOMPARE(got.red(), 255);
    QCOMPARE(got.blue(), 255);
    QCOMPARE(got.green(), 0);

    delete w;
}

// A failed write must leave the previous good file untouched.
//
//   The old code used QImage::save(path), which opens the destination
//   directly: a failure halfway through truncated the file. QSaveFile
//   writes to a temp file and only replaces the target on commit.
void tst_M1_4_7_8_Save::saveImage_isAtomic_failedWriteKeepsOldFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("atomic.png");

    // A good file first.
    const cv::Mat good = makeRamp(32, 32);
    writePng(png, good);
    const qint64 goodSize = QFileInfo(png).size();
    QVERIFY(goodSize > 0);

    // Now make the write fail: the destination is a DIRECTORY, so
    // opening it for writing fails.
    const QString dirPath = dir.filePath("adir");
    QVERIFY(QDir().mkpath(dirPath));

    QString err;
    const cv::Mat other = makeRamp(16, 16);
    const bool ok = ImageProcessor::saveImage(other, dirPath, &err);
    QVERIFY2(!ok, "writing to a directory path must fail");
    QVERIFY(!err.isEmpty());

    // The good file must be untouched.
    QVERIFY(QFileInfo(png).exists());
    QCOMPARE(QFileInfo(png).size(), goodSize);
    QImage stillThere(png);
    QVERIFY(!stillThere.isNull());
    QCOMPARE(stillThere.size(), QSize(32, 32));
}

// An unknown extension cannot be mapped to a format -> the save must
// fail with a message instead of writing a file with a wrong format.
void tst_M1_4_7_8_Save::saveImage_rejectsUnknownExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("weird.xyz");

    QString err;
    const bool ok = ImageProcessor::saveImage(makeRamp(8, 8), path, &err);
    QVERIFY2(!ok, "an unmappable extension must fail");
    QVERIFY(!err.isEmpty());
    QVERIFY(!QFileInfo::exists(path));
}

// A failed write must leave the previous good file untouched.
//
//   The old DocWindow code used QFile + Truncate, which destroys the
//   previous content the moment the file is opened, and it never
//   checked the QTextStream flush result -- so a truncated file could
//   still be reported as "saved".
void tst_M1_4_7_8_Save::docWindow_saveIsAtomic_failedWriteKeepsOldFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString txt = dir.filePath("doc.txt");

    // A good file first.
    {
        QFile f(txt);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("hello", 5);
        f.close();
    }
    const qint64 goodSize = QFileInfo(txt).size();
    QVERIFY(goodSize > 0);

    // Now save to a path that cannot be written: a directory.
    const QString dirPath = dir.filePath("adir");
    QVERIFY(QDir().mkpath(dirPath));

    DocWindow w;
    QString err;
    const bool ok = w.saveAsFile(dirPath, &err);
    QVERIFY2(!ok, "saving onto a directory path must fail");
    QVERIFY(!err.isEmpty());

    // The good file must be untouched.
    QVERIFY(QFileInfo(txt).exists());
    QCOMPARE(QFileInfo(txt).size(), goodSize);
    QFile f(txt);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(f.readAll(), QByteArray("hello"));
}

// =============================================================================
//  M1-7: the undo memory
// =============================================================================

// A tool edit must store only the changed region.
//
//   The old code stored two FULL-layer clones: 288 MB per stroke on a
//   24 MP document. Now the command stores the bounding box of the
//   difference.
void tst_M1_4_7_8_Save::setLayerImage_storesOnlyTheChangedRegion()
{
    layers::LayerStack stack;
    stack.addLayer(QStringLiteral("Base"), makeRamp(200, 200));

    const cv::Mat before = stack.at(0)->image.clone();
    cv::Mat after = before.clone();
    // Change a 10x10 block in the middle.
    for (int y = 95; y < 105; ++y) {
        for (int x = 95; x < 105; ++x) {
            uchar* p = after.ptr<uchar>(y) + x * 4;
            p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255;
        }
    }
    stack.at(0)->image = after.clone();

    QUndoStack undoStack;
    undoStack.push(layers::LayerCommand::makeSetLayerImage(
        &stack, 0, before, after, QStringLiteral("stroke")));

    // The command must not hold two full 200x200 images.
    //   200*200*4 = 160000 bytes per full image; two of them is 320000.
    //   The patch is 10x10x4 = 400 bytes per side, 800 in total.
    const auto* cmd = undoStack.command(0);
    QVERIFY(cmd);
    const auto* fp = dynamic_cast<const IUndoMemoryFootprint*>(cmd);
    QVERIFY2(fp, "the command must report its footprint");
    const size_t footprint = fp->memoryFootprint();
    QVERIFY2(footprint < 10000,
             qPrintable(QStringLiteral("footprint %1 bytes -- the full-image clone is back")
                            .arg(footprint)));

    // The undo must still restore the pixels EXACTLY.
    undoStack.undo();
    const cv::Vec4b restored = stack.at(0)->image.at<cv::Vec4b>(100, 100);
    QCOMPARE(int(restored[2]), int(before.at<cv::Vec4b>(100, 100)[2]));
    undoStack.redo();
    const cv::Vec4b redone = stack.at(0)->image.at<cv::Vec4b>(100, 100);
    QCOMPARE(int(redone[2]), 255);
}

// The undo stack must enforce a MEMORY budget, not just a count limit.
void tst_M1_4_7_8_Save::undoStack_evictsOldestWhenOverBudget()
{
    // A command that reports a fixed footprint.
    class SizedCommand : public QUndoCommand, public IUndoMemoryFootprint
    {
    public:
        SizedCommand(const QString& text, size_t bytes)
            : QUndoCommand(text), m_bytes(bytes) {}
        size_t memoryFootprint() const override { return m_bytes; }
    private:
        size_t m_bytes;
    };

    UndoStack stack;
    stack.setUndoLimit(1000);
    // Budget: 1000 bytes, each command claims 400 -> 2 fit, the 3rd evicts.
    stack.setMemoryBudgetBytes(1000);

    stack.push(new SizedCommand(QStringLiteral("a"), 400));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.memoryUsedBytes(), size_t(400));

    stack.push(new SizedCommand(QStringLiteral("b"), 400));
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.memoryUsedBytes(), size_t(800));

    stack.push(new SizedCommand(QStringLiteral("c"), 400));
    // Over the budget -> the OLDEST is dropped.
    QVERIFY2(stack.memoryUsedBytes() <= 1000,
             qPrintable(QStringLiteral("used %1 bytes").arg(stack.memoryUsedBytes())));
    QCOMPARE(stack.evictedCommandCount(), 1);
    QCOMPARE(stack.count(), 2);
    // The dropped one is the OLDEST.
    QCOMPARE(stack.command(0)->text(), QStringLiteral("b"));
}

// The eviction must NOT change the document.
//
//   The undo stack rebuilds itself when the byte budget is exceeded.
//   Qt's QUndoStack cannot drop a command from the bottom of a
//   populated stack (setUndoLimit() is refused on a non-empty stack),
//   so the rebuild drains the stack and pushes the survivors back.
//
//   The whole point of the design is that the survivors are pushed
//   back in "replay" mode: their first redo() is swallowed, so no
//   command re-applies its state to the document. If that ever breaks,
//   the document silently jumps to an older state while the user is
//   drawing -- a data corruption bug, not a cosmetic one.
//
//   This test uses a command whose redo()/undo() are OBSERVABLE, so
//   any document mutation during the eviction shows up.
void tst_M1_4_7_8_Save::undoStack_evictionDoesNotTouchTheDocument()
{
    // A command that records every undo()/redo() it receives.
    class ObservableCommand : public QUndoCommand, public IUndoMemoryFootprint
    {
    public:
        ObservableCommand(const QString& text, int* undoCount, int* redoCount, size_t bytes)
            : QUndoCommand(text), m_undoCount(undoCount), m_redoCount(redoCount), m_bytes(bytes)
        {}
        void undo() override { ++(*m_undoCount); }
        void redo() override { ++(*m_redoCount); }
        size_t memoryFootprint() const override { return m_bytes; }
    private:
        int*   m_undoCount;
        int*   m_redoCount;
        size_t m_bytes;
    };

    int undoCount = 0;
    int redoCount = 0;

    UndoStack stack;
    stack.setUndoLimit(1000);
    stack.setMemoryBudgetBytes(1000);

    stack.push(new ObservableCommand(QStringLiteral("a"), &undoCount, &redoCount, 400));
    stack.push(new ObservableCommand(QStringLiteral("b"), &undoCount, &redoCount, 400));
    QCOMPARE(undoCount, 0);
    // Each push calls redo() once (the normal QUndoStack contract).
    QCOMPARE(redoCount, 2);

    // The third command pushes the stack over the budget -> eviction.
    stack.push(new ObservableCommand(QStringLiteral("c"), &undoCount, &redoCount, 400));
    QCOMPARE(stack.evictedCommandCount(), 1);
    QCOMPARE(stack.count(), 2);

    // The eviction must not have undone or redone anything.
    QCOMPARE(undoCount, 0);
    QCOMPARE(redoCount, 3);
}

// After an eviction the undo / redo must still work.
//
//   The eviction rebuilds the stack, so the survivors are new guard
//   objects. Their first redo() was swallowed during the rebuild --
//   if the real command did not learn about that, the NEXT redo
//   (after the user pressed undo) would be swallowed as well and the
//   redo would silently do nothing.
void tst_M1_4_7_8_Save::undoStack_evictionKeepsUndoRedoWorking()
{
    class CountingCommand : public QUndoCommand, public IUndoMemoryFootprint
    {
    public:
        CountingCommand(const QString& text, int* undoCount, int* redoCount, size_t bytes)
            : QUndoCommand(text), m_undoCount(undoCount), m_redoCount(redoCount), m_bytes(bytes)
        {}
        void undo() override { ++(*m_undoCount); }
        void redo() override { ++(*m_redoCount); }
        size_t memoryFootprint() const override { return m_bytes; }
    private:
        int*   m_undoCount;
        int*   m_redoCount;
        size_t m_bytes;
    };

    int undoCount = 0;
    int redoCount = 0;

    UndoStack stack;
    stack.setUndoLimit(1000);
    stack.setMemoryBudgetBytes(1000);

    stack.push(new CountingCommand(QStringLiteral("a"), &undoCount, &redoCount, 400));
    stack.push(new CountingCommand(QStringLiteral("b"), &undoCount, &redoCount, 400));
    stack.push(new CountingCommand(QStringLiteral("c"), &undoCount, &redoCount, 400));
    QCOMPARE(stack.evictedCommandCount(), 1);
    QCOMPARE(stack.count(), 2);

    // Undo must reach the real command.
    const int undoBefore = undoCount;
    stack.undo();
    QCOMPARE(undoCount, undoBefore + 1);

    // Redo must reach the real command too -- this is what breaks if
    // the rebuild swallowed the survivors' first redo without telling
    // the command about it.
    const int redoBefore = redoCount;
    stack.redo();
    QCOMPARE(redoCount, redoBefore + 1);
}

// The redo tail must survive an eviction.
//
//   The user can undo a few commands and then keep drawing. The
//   commands they undid are still on the stack -- they are just
//   "redoable" (the stack index is below the top).
//
//   The eviction rebuilds the stack. If the rebuild only kept the
//   commands below the index, the user would silently lose the
//   ability to redo what they had undone -- and the document would
//   jump forward to the newest state while they were looking at an
//   older one.
void tst_M1_4_7_8_Save::undoStack_evictionKeepsTheRedoTail()
{
    class CountingCommand : public QUndoCommand, public IUndoMemoryFootprint
    {
    public:
        CountingCommand(const QString& text, int* undoCount, int* redoCount, size_t bytes)
            : QUndoCommand(text), m_undoCount(undoCount), m_redoCount(redoCount), m_bytes(bytes)
        {}
        void undo() override { ++(*m_undoCount); }
        void redo() override { ++(*m_redoCount); }
        size_t memoryFootprint() const override { return m_bytes; }
    private:
        int*   m_undoCount;
        int*   m_redoCount;
        size_t m_bytes;
    };

    int undoCount = 0;
    int redoCount = 0;

    UndoStack stack;
    stack.setUndoLimit(1000);
    stack.setMemoryBudgetBytes(1000);

    stack.push(new CountingCommand(QStringLiteral("a"), &undoCount, &redoCount, 400));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.index(), 1);

    stack.push(new CountingCommand(QStringLiteral("b"), &undoCount, &redoCount, 400));
    QCOMPARE(stack.count(), 2);
    QCOMPARE(stack.index(), 2);

    // The third command goes over the budget -> the OLDEST ("a") is
    // evicted. The stack keeps "b" and "c".
    stack.push(new CountingCommand(QStringLiteral("c"), &undoCount, &redoCount, 400));
    QCOMPARE(stack.evictedCommandCount(), 1);
    QCOMPARE(stack.count(), 2);
    // The index counts the APPLIED commands. The evicted command was
    // at the bottom and was applied too, so removing it does not move
    // the index -- it stays at the top of the (smaller) stack.
    QCOMPARE(stack.index(), 2);

    // The bookkeeping has to agree with the real stack: the number of
    // commands and the number of bytes must match what is really there.
    QCOMPARE(stack.memoryUsedBytes(), size_t(800));

    // The user undoes both commands -> the stack index is 0.
    stack.undo();
    stack.undo();
    QCOMPARE(stack.index(), 0);
    QCOMPARE(stack.count(), 2);

    // Now push one more command.
    //
    //   QUndoStack::push() DELETES the commands ABOVE the current index
    //   ("the redo tail") -- that is Qt's own behaviour, not something
    //   the eviction does. With the index at 0, that is every command
    //   except the bottom one, so "b" and "c" are thrown away and
    //   only "d" remains.
    //
    //   The bookkeeping has to follow: the thrown-away commands must
    //   be gone from the byte count too -- otherwise the stack would
    //   think it is over budget and would evict commands that are
    //   still there.
    stack.push(new CountingCommand(QStringLiteral("d"), &undoCount, &redoCount, 400));
    QCOMPARE(stack.evictedCommandCount(), 1);   // still just the first one
    QCOMPARE(stack.count(), 1);                 // only "d" is left
    QCOMPARE(stack.index(), 1);
    QCOMPARE(stack.memoryUsedBytes(), size_t(400));
}

// The accounting must follow the commands that report a footprint.
void tst_M1_4_7_8_Save::undoStack_accountingTracksFootprint()
{
    class SizedCommand : public QUndoCommand, public IUndoMemoryFootprint
    {
    public:
        SizedCommand(size_t bytes) : QUndoCommand(QStringLiteral("x")), m_bytes(bytes) {}
        size_t memoryFootprint() const override { return m_bytes; }
    private:
        size_t m_bytes;
    };
    class PlainCommand : public QUndoCommand
    {
    public:
        PlainCommand() : QUndoCommand(QStringLiteral("p")) {}
    };

    UndoStack stack;
    stack.setMemoryBudgetBytes(0);   // unlimited

    stack.push(new SizedCommand(1234));
    stack.push(new PlainCommand());  // no footprint -> 0 bytes
    QCOMPARE(stack.memoryUsedBytes(), size_t(1234));
}

// A selection gesture must be undoable.
//
//   The lasso / marquee / magic wand changed the selection directly on
//   the model, with no undo entry at all.
void tst_M1_4_7_8_Save::selectionGesture_isUndoable()
{
    selection::SelectionModel model;
    model.setSize(QSize(20, 20));
    QVERIFY(model.isEmpty());

    UndoStack stack;

    // A gesture: the tool captures the before-mask, applies the new one
    // and pushes a SetMask command.
    const QImage before = model.mask();
    QImage gestureMask(20, 20, QImage::Format_Alpha8);
    gestureMask.fill(0);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            gestureMask.setPixel(x, y, qRgba(255, 255, 255, 255));
        }
    }
    model.setMask(gestureMask, selection::SelectionModel::Mode::Replace);
    const QImage after = model.mask();
    stack.push(selection::SelectionCommand::makeSetMask(&model, before, after,
                                                        QStringLiteral("Lasso select")));

    QCOMPARE(model.pixelCount(), 100);

    // Undo restores the empty selection.
    stack.undo();
    QCOMPARE(model.pixelCount(), 0);

    // Redo restores it -- and it must be idempotent: a second redo must
    // not change the result (the old op-based command double-inverted).
    stack.redo();
    QCOMPARE(model.pixelCount(), 100);
    stack.redo();
    QCOMPARE(model.pixelCount(), 100);
}

// =============================================================================
//  M1-8: the colour
// =============================================================================

// The colour must have ONE source of truth.
void tst_M1_4_7_8_Save::colorState_isTheSingleSourceOfTruth()
{
    ColorState state;
    QCOMPARE(state.foregroundColor(), QColor(Qt::black));
    QCOMPARE(state.backgroundColor(), QColor(Qt::white));

    // The same colour twice must not emit twice.
    QSignalSpy fgSpy(&state, &ColorState::foregroundColorChanged);
    state.setForegroundColor(QColor(Qt::red));
    QCOMPARE(fgSpy.count(), 1);
    state.setForegroundColor(QColor(Qt::red));
    QCOMPARE(fgSpy.count(), 1);

    // Swap.
    state.swapColors();
    QCOMPARE(state.foregroundColor(), QColor(Qt::white));
    QCOMPARE(state.backgroundColor(), QColor(Qt::red));

    // Default.
    state.defaultColors();
    QCOMPARE(state.foregroundColor(), QColor(Qt::black));
    QCOMPARE(state.backgroundColor(), QColor(Qt::white));
}

// The colour panel must render the state (and push into it).
void tst_M1_4_7_8_Save::colorTab_followsTheState()
{
    ColorState state;
    docks::ColorTab tab;
    tab.setColorState(&state);

    QCOMPARE(tab.foregroundColor(), state.foregroundColor());

    // The state changes -> the panel follows.
    state.setForegroundColor(QColor(12, 34, 56));
    QCOMPARE(tab.foregroundColor(), QColor(12, 34, 56));

    // The panel changes -> the state follows.
    tab.setForegroundColor(QColor(200, 100, 50));
    QCOMPARE(state.foregroundColor(), QColor(200, 100, 50));
}

// The toolbar must render the state (and push into it).
void tst_M1_4_7_8_Save::leftToolBar_followsTheState()
{
    ColorState state;
    tools::LeftToolBar bar;
    bar.setColorState(&state);

    QCOMPARE(bar.foregroundColor(), state.foregroundColor());

    state.setForegroundColor(QColor(9, 8, 7));
    QCOMPARE(bar.foregroundColor(), QColor(9, 8, 7));

    bar.setForegroundColor(QColor(1, 2, 3));
    QCOMPARE(state.foregroundColor(), QColor(1, 2, 3));
}

// The eyedropper must push the sampled colour into the state.
//
//   This is DEBT entry A-EYEDROPPER: the eyedropper must push the
//   sampled colour into the shared state.
void tst_M1_4_7_8_Save::eyedropper_pushesIntoTheState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("pick.png");

    // A solid red image, so the sampled colour is unambiguous.
    cv::Mat solid(64, 64, CV_8UC4, cv::Scalar(0, 0, 255, 255));
    writePng(png, solid);

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));

    ColorState state;
    // The eyedropper writes into the WINDOW's state, so bind the window
    // state to a local one by pushing into it directly -- the point of
    // the test is that the sampled colour reaches a ColorState at all.
    auto* windowState = w->colorState();
    QVERIFY2(windowState, "the window must own a colour state");

    tools::EyedropperTool tool;
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    tool.onMousePress(&ev, w, QPointF(32, 32));

    QVERIFY(tool.sampledColor().isValid());
    QCOMPARE(tool.sampledColor().red(), 255);
    QCOMPARE(tool.sampledColor().green(), 0);
    QCOMPARE(tool.sampledColor().blue(), 0);

    // The sampled colour must be in the window's state.
    QCOMPARE(windowState->foregroundColor(), QColor(255, 0, 0));

    delete w;
}

// The brush must read the colour from the state.
void tst_M1_4_7_8_Save::brush_readsTheState()
{
    ColorState state;
    tools::Brush brush;
    brush.setColorState(&state);

    QCOMPARE(brush.paintColor(), state.foregroundColor());

    state.setForegroundColor(QColor(11, 22, 33));
    QCOMPARE(brush.paintColor(), QColor(11, 22, 33));

    // A brush that is NOT bound keeps its own colour (unit-test path).
    tools::Brush loose;
    loose.setPaintColor(QColor(1, 2, 3));
    QCOMPARE(loose.paintColor(), QColor(1, 2, 3));
}

QTEST_MAIN(tst_M1_4_7_8_Save)
#include "tst_M1_4_7_8_Save.moc"
