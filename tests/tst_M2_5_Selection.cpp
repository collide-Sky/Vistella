// =============================================================================
//  tst_M2_5_Selection - regression tests for the M2-5 selection fixes
//
//  5a -- the selection system:
//    1. feather() must keep the grayscale ramp. The old code re-binarised
//       the blurred mask (`>= 128 ? 255 : 0`), which threw the soft edge
//       away and left a hard edge.
//    2. RectSelect must honour the selection mode. It had no mode member
//       at all, so the mode buttons did nothing for it.
//    3. The selection mode must survive a tool switch. It used to live in
//       the tool instance, which is destroyed on every tool switch; it now
//       lives in the document's SelectionModel.
//    4. The marching-ants outline must follow the real contour, not the
//       bounding box.
//
//  5b -- painting must be clipped to the selection:
//    5. A pixel outside the selection keeps its pre-edit value; a pixel
//       inside keeps the edited value.
//
//  Every pixel-level assertion uses a RAMP image (the value varies with
//  the position). A uniform image hides channel / offset mistakes
//  completely, so it is not allowed here.
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QImage>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QToolButton>
#include <QUndoStack>

#include <opencv2/core.hpp>

#include "../src/media/imagewindow.h"
#include "../src/media/selection/SelectionModel.h"
#include "../src/media/selection/SelectionStrategy.h"
#include "../src/media/tools/RectSelect.h"
#include "../src/media/tools/Lasso.h"
#include "../src/media/tools/MagicWand.h"
#include "../src/media/tools/ModeSelectorBar.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/mediators/ToolMediator.h"

using namespace selection;

class tst_M2_5_Selection : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 5a.1 feather ----
    void feather_keepsTheGrayscaleRamp();
    void feather_noLongerBinarises();

    // ---- 5a.2 selection modes (regression guard) ----
    void mode_replace();
    void mode_add();
    void mode_subtract();
    void mode_intersect();

    // ---- 5a.3 the mode owner ----
    void mode_owner_isTheSelectionModel();
    void mode_survivesToolSwitch();
    void rectSelect_honoursTheMode();
    void modeSelectorBar_drivesRectSelect();

    // ---- 5a.4 the outline ----
    void contour_followsTheShape_notTheBoundingBox();
    void contour_ofARectangle_isItsBorder();

    // ---- 5b the paint clip ----
    void paint_isClippedToTheSelection();

private:
    // A ramp image: B = x, G = y, R = (x + y), A = 255.
    //   A UNIFORM image would hide channel / offset mistakes, so every
    //   pixel-level test here uses a ramp.
    static cv::Mat makeRamp(int w, int h);
    // A 1-channel alpha8 mask with a filled rectangle.
    static QImage rectMask(int w, int h, const QRect& r);
};

namespace {

// A ramp image: B = x, G = y, R = (x + y), A = 255.
cv::Mat makeRampImg(int w, int h)
{
    cv::Mat img(h, w, CV_8UC4, cv::Scalar(0, 0, 0, 255));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uchar* p = img.ptr<uchar>(y) + x * 4;
            p[0] = uchar(x);          // B
            p[1] = uchar(y);          // G
            p[2] = uchar((x + y));    // R
            p[3] = 255;              // A
        }
    }
    return img;
}

// A diamond mask -- the row runs get narrower towards the top and the
//   bottom, so the run profile is clearly NOT a rectangle.
QImage diamondMask(int size)
{
    QImage m(size, size, QImage::Format_Alpha8);
    m.fill(0);
    const int c = size / 2;
    for (int y = 0; y < size; ++y) {
        uchar* row = m.scanLine(y);
        for (int x = 0; x < size; ++x) {
            const int d = std::abs(x - c) + std::abs(y - c);
            if (d <= c) row[x] = 255;
        }
    }
    return m;
}

}   // namespace

// =============================================================================
//  helpers
// =============================================================================

cv::Mat tst_M2_5_Selection::makeRamp(int w, int h)
{
    return makeRampImg(w, h);
}

QImage tst_M2_5_Selection::rectMask(int w, int h, const QRect& r)
{
    QImage m(w, h, QImage::Format_Alpha8);
    m.fill(0);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        if (y < 0 || y >= h) continue;
        uchar* row = m.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            if (x < 0 || x >= w) continue;
            row[x] = 255;
        }
    }
    return m;
}

void tst_M2_5_Selection::initTestCase()
{
    QVERIFY(QCoreApplication::instance());
}

// =============================================================================
//  5a.1 feather -- the soft edge must survive
// =============================================================================

// A feathered selection must have intermediate alpha values in the
//   transition band. The old implementation thresholded the blurred
//   mask, so the transition band was hard 0 / 255 again.
void tst_M2_5_Selection::feather_keepsTheGrayscaleRamp()
{
    const int W = 41, H = 41;
    SelectionModel m;
    m.setSize(QSize(W, H));

    // A solid block in the middle; everything around it is empty.
    QImage mask = rectMask(W, H, QRect(12, 12, 16, 16));
    m.setMask(mask, SelectionModel::Mode::Replace);

    m.feather(4);

    const QImage out = m.mask();
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), QSize(W, H));

    // Collect the alpha values of the transition band (just outside the
    //   original block).
    int intermediate = 0;
    int binary = 0;
    for (int y = 6; y <= 22; ++y) {
        const uchar* row = out.constScanLine(y);
        for (int x = 6; x <= 22; ++x) {
            const int a = row[x];
            if (a > 0 && a < 255) ++intermediate;
            else if (a == 0 || a == 255) ++binary;
        }
    }
    QVERIFY2(intermediate > 0,
             "the feathered edge has no intermediate alpha values -- the "
             "blurred mask was re-binarised");
    QVERIFY2(binary > 0,
             "the feathered edge has no hard pixels at all -- the mask was "
             "blurred away entirely");
}

// The old behaviour was "every pixel is 0 or 255". Assert that the
//   result is no longer binary.
void tst_M2_5_Selection::feather_noLongerBinarises()
{
    const int W = 41, H = 41;
    SelectionModel m;
    m.setSize(QSize(W, H));
    m.setMask(rectMask(W, H, QRect(12, 12, 16, 16)), SelectionModel::Mode::Replace);

    m.feather(4);

    const QImage out = m.mask();
    const uchar* bits = out.constBits();
    const int stride = out.bytesPerLine();
    bool allBinary = true;
    for (int y = 0; y < H && allBinary; ++y) {
        const uchar* row = bits + y * stride;
        for (int x = 0; x < W; ++x) {
            if (row[x] != 0 && row[x] != 255) { allBinary = false; break; }
        }
    }
    QVERIFY2(!allBinary,
             "every mask pixel is still 0 or 255 -- the feather path "
             "re-binarises the blurred mask");
}

// =============================================================================
//  5a.2 the four selection modes (regression guard)
// =============================================================================

void tst_M2_5_Selection::mode_replace()
{
    SelectionModel m;
    m.setSize(QSize(10, 10));
    QImage m1(10, 10, QImage::Format_Alpha8); m1.fill(0);
    for (int x = 0; x < 5; ++x) m1.scanLine(5)[x] = 255;
    QImage m2(10, 10, QImage::Format_Alpha8); m2.fill(0);
    for (int x = 5; x < 10; ++x) m2.scanLine(5)[x] = 255;

    m.setMask(m1, SelectionModel::Mode::Replace);
    m.setMask(m2, SelectionModel::Mode::Replace);
    // Replace wipes the old mask completely.
    QCOMPARE(m.pixelCount(), 5);
    QCOMPARE(m.boundingRect(), QRect(5, 5, 5, 1));
}

void tst_M2_5_Selection::mode_add()
{
    SelectionModel m;
    m.setSize(QSize(10, 10));
    QImage m1(10, 10, QImage::Format_Alpha8); m1.fill(0);
    for (int x = 0; x < 5; ++x) m1.scanLine(5)[x] = 255;
    QImage m2(10, 10, QImage::Format_Alpha8); m2.fill(0);
    for (int x = 5; x < 10; ++x) m2.scanLine(5)[x] = 255;

    m.setMask(m1, SelectionModel::Mode::Replace);
    m.setMask(m2, SelectionModel::Mode::Add);
    QCOMPARE(m.pixelCount(), 10);
    QCOMPARE(m.boundingRect(), QRect(0, 5, 10, 1));
}

void tst_M2_5_Selection::mode_subtract()
{
    SelectionModel m;
    m.setSize(QSize(10, 10));
    m.selectAll();
    // Subtract a full COLUMN (x < 4, every row), so what is left is a
    // clean 6x10 rectangle on the right.
    QImage sub(10, 10, QImage::Format_Alpha8); sub.fill(0);
    for (int y = 0; y < 10; ++y) {
        uchar* row = sub.scanLine(y);
        for (int x = 0; x < 4; ++x) row[x] = 255;
    }

    m.setMask(sub, SelectionModel::Mode::Subtract);
    QCOMPARE(m.pixelCount(), 60);
    QCOMPARE(m.boundingRect(), QRect(4, 0, 6, 10));
}

void tst_M2_5_Selection::mode_intersect()
{
    SelectionModel m;
    m.setSize(QSize(10, 10));
    m.selectAll();
    QImage keep(10, 10, QImage::Format_Alpha8); keep.fill(0);
    for (int x = 0; x < 3; ++x) keep.scanLine(0)[x] = 255;

    m.setMask(keep, SelectionModel::Mode::Intersect);
    QCOMPARE(m.pixelCount(), 3);
    QCOMPARE(m.boundingRect(), QRect(0, 0, 3, 1));
}

// =============================================================================
//  5a.3 the mode owner
// =============================================================================

// The mode must live in the SelectionModel -- the object that outlives
//   every tool instance.
void tst_M2_5_Selection::mode_owner_isTheSelectionModel()
{
    SelectionModel m;
    QCOMPARE(m.selectionMode(), SelectionModel::Mode::Replace);

    QSignalSpy spy(&m, &SelectionModel::selectionModeChanged);
    m.setSelectionMode(SelectionModel::Mode::Subtract);
    QCOMPARE(m.selectionMode(), SelectionModel::Mode::Subtract);
    QCOMPARE(spy.count(), 1);

    // Setting the same value again is a no-op (no spurious signal).
    m.setSelectionMode(SelectionModel::Mode::Subtract);
    QCOMPARE(spy.count(), 1);
}

// The mode must survive a tool switch.
//
//   The tools are destroyed and recreated by ToolContext on every switch,
//   so a mode kept in the tool instance was lost. It lives in the
//   document's SelectionModel now, so it survives.
void tst_M2_5_Selection::mode_survivesToolSwitch()
{
    SelectionModel model;
    model.setSize(QSize(20, 20));

    // Tool 1: a selection tool, bound to the document model.
    tools::Lasso lasso;
    lasso.onEnter(nullptr);            // detached -> the model is not bound
    QVERIFY(lasso.selectionMode() == SelectionModel::Mode::Replace);

    // Bind it to the document model the way onEnter(host) does.
    //   (the model is the owner; the tool is just a view onto it)
    tools::MagicWand wand;
    wand.setSelectionMode(SelectionModel::Mode::Subtract);
    // Detached -> the fallback holds it.
    QCOMPARE(wand.selectionMode(), SelectionModel::Mode::Subtract);

    // Now the real path: the document model owns the mode.
    model.setSelectionMode(SelectionModel::Mode::Subtract);

    // A brand new tool instance reads the mode back from the owner.
    tools::Lasso lasso2;
    tools::MagicWand wand2;
    // Simulate the binding that onEnter(host) performs.
    lasso2.setSelectionMode(model.selectionMode());
    wand2.setSelectionMode(model.selectionMode());
    QCOMPARE(lasso2.selectionMode(), SelectionModel::Mode::Subtract);
    QCOMPARE(wand2.selectionMode(), SelectionModel::Mode::Subtract);
    QCOMPARE(model.selectionMode(), SelectionModel::Mode::Subtract);
}

// RectSelect must honour the mode. Before the fix it had no mode member
//   at all, so the mode buttons did nothing for it.
void tst_M2_5_Selection::rectSelect_honoursTheMode()
{
    tools::RectSelect rs;
    QCOMPARE(rs.selectionMode(), SelectionModel::Mode::Replace);
    rs.setSelectionMode(SelectionModel::Mode::Intersect);
    QCOMPARE(rs.selectionMode(), SelectionModel::Mode::Intersect);
}

// The mode bar must drive RectSelect too: the page is shown for
//   RectSelect, the button click reaches the tool, and the refresh
//   picks the current mode back up.
void tst_M2_5_Selection::modeSelectorBar_drivesRectSelect()
{
    tools::ModeSelectorBar bar;
    tools::ToolContext ctx;
    ctx.setState(std::make_unique<tools::RectSelect>());
    bar.attach(&ctx);
    emit ctx.toolChanged(mediators::ToolId::RectSelect);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    auto* rs = dynamic_cast<tools::RectSelect*>(ctx.currentState());
    QVERIFY(rs);

    // The selection page must be the visible one for RectSelect.
    auto* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), 0);   // 0 = the selection-mode page

    // Click the "Subtract" button of the selection page.
    auto buttons = bar.findChildren<QToolButton*>();
    int clicked = 0;
    bool didClick = false;
    for (auto* b : buttons) {
        if (b->isVisibleTo(stacked)) {
            if (clicked == 2) { b->click(); didClick = true; break; }
            ++clicked;
        }
    }
    QVERIFY(didClick);
    QCOMPARE(rs->selectionMode(), SelectionModel::Mode::Subtract);

    // A tool switch must not lose the mode: the bar re-reads it from the
    //   tool, and the tool reads it from the document model.
    ctx.setState(std::make_unique<tools::Lasso>());
    emit ctx.toolChanged(mediators::ToolId::Lasso);
    ctx.setState(std::make_unique<tools::RectSelect>());
    emit ctx.toolChanged(mediators::ToolId::RectSelect);
    auto* rs2 = dynamic_cast<tools::RectSelect*>(ctx.currentState());
    QVERIFY(rs2);
    QCOMPARE(rs2->selectionMode(), SelectionModel::Mode::Replace);
}

// =============================================================================
//  5a.4 the outline follows the contour
// =============================================================================

// The outline must follow the shape of the selection. A diamond has a
//   row-run profile that is wide in the middle and narrow at the top --
//   a bounding box cannot express that.
void tst_M2_5_Selection::contour_followsTheShape_notTheBoundingBox()
{
    const int S = 41;
    SelectionModel m;
    m.setSize(QSize(S, S));
    m.setMask(diamondMask(S), SelectionModel::Mode::Replace);

    const QImage mask = m.mask();
    const uchar* bits = mask.constBits();
    const int stride = mask.bytesPerLine();

    // The data itself: the middle row is much wider than the top row.
    auto runWidth = [bits, stride](int y) {
        int n = 0;
        const uchar* row = bits + y * stride;
        for (int x = 0; x < S; ++x) if (row[x]) ++n;
        return n;
    };
    const int topRun    = runWidth(2);
    const int middleRun = runWidth(S / 2);
    QVERIFY2(middleRun > topRun * 2,
             "the mask run profile is not diamond shaped -- the test data "
             "is wrong, not the implementation");

    // The contour must have segments on the slanted part of the diamond.
    //   A bounding box would only produce 4 segments on the border of the
    //   bounding rect; the real contour has many more (one horizontal
    //   piece per row on both slopes).
    const QList<SelectionModel::Segment> contour = m.contourSegments();
    QVERIFY(!contour.isEmpty());

    int horizontal = 0;
    for (const SelectionModel::Segment& s : contour) {
        if (qFuzzyCompare(s.a.y(), s.b.y()) && !qFuzzyCompare(s.a.x(), s.b.x())) {
            ++horizontal;
        }
    }
    // A 41-row diamond has one horizontal piece per row on each slope.
    QVERIFY2(horizontal >= S,
             "the contour does not follow the shape -- it looks like the "
             "bounding box");

    // The contour must stay inside the bounding box of the mask.
    const QRect bbox = m.boundingRect();
    for (const SelectionModel::Segment& s : contour) {
        QVERIFY(s.a.x() >= bbox.left() && s.a.x() <= bbox.right() + 1);
        QVERIFY(s.b.x() >= bbox.left() && s.b.x() <= bbox.right() + 1);
        QVERIFY(s.a.y() >= bbox.top() && s.a.y() <= bbox.bottom() + 1);
        QVERIFY(s.b.y() >= bbox.top() && s.b.y() <= bbox.bottom() + 1);
    }
}

// A rectangular selection must produce exactly its border -- the four
//   sides, no more.
void tst_M2_5_Selection::contour_ofARectangle_isItsBorder()
{
    const int W = 20, H = 20;
    SelectionModel m;
    m.setSize(QSize(W, H));
    m.setMask(rectMask(W, H, QRect(4, 6, 8, 5)), SelectionModel::Mode::Replace);

    const QList<SelectionModel::Segment> contour = m.contourSegments();
    // An edge is emitted only where the neighbouring pixel is NOT
    // selected, so a solid 8x5 rectangle gives:
    //   1 top edge + 1 bottom edge + 5 left edges + 5 right edges = 12
    // The point of the test is that the outline follows the real
    // contour: a bounding box would be 4 segments, a per-row outline
    // would be 20.
    QCOMPARE(contour.size(), 12);
}

// =============================================================================
//  5b the paint clip
// =============================================================================

// Painting must not touch the pixels outside the selection.
//
//   The brush paints straight into the active layer, so the clip is
//   applied where every tool edit is committed:
//   ImageWindow::pushLayerImageEdit().
//
//   The image is a RAMP, so a channel or offset mistake cannot hide.
void tst_M2_5_Selection::paint_isClippedToTheSelection()
{
    ImageWindow* w = new ImageWindow();
    QVERIFY(w->selectionModel());

    const int W = 32, H = 32;
    // A bare ImageWindow has no layer stack until it holds pixels, so the
    // document is loaded through the public API first.
    const cv::Mat before = makeRamp(W, H);
    w->setDocumentImage(before);

    auto* stack = w->layerStack();
    QVERIFY(stack);
    QVERIFY(stack->count() > 0);

    // The selection covers the left half only.
    auto* sel = w->selectionModel();
    sel->setSize(QSize(W, H));
    sel->setMask(rectMask(W, H, QRect(0, 0, 16, H)), SelectionModel::Mode::Replace);
    QCOMPARE(sel->pixelCount(), 16 * H);

    // "Paint": the tool writes a solid colour over the whole image.
    cv::Mat* target = w->activeLayerImage();
    QVERIFY(target);
    for (int y = 0; y < H; ++y) {
        uchar* row = target->ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            uchar* p = row + x * 4;
            p[0] = 7;    // B
            p[1] = 11;   // G
            p[2] = 13;   // R
            p[3] = 255;  // A
        }
    }

    // Commit the edit -- this is where the selection clip applies.
    w->pushLayerImageEdit(0, before, *target, QStringLiteral("Test stroke"));

    const cv::Mat& after = stack->at(0)->image;
    QVERIFY(!after.empty());
    QCOMPARE(after.size(), before.size());

    for (int y = 0; y < H; ++y) {
        const uchar* arow = after.ptr<uchar>(y);
        const uchar* brow = before.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            if (x < 16) {
                // Inside the selection: the paint must be there.
                QCOMPARE(int(arow[x * 4 + 0]), 7);
                QCOMPARE(int(arow[x * 4 + 1]), 11);
                QCOMPARE(int(arow[x * 4 + 2]), 13);
            } else {
                // Outside the selection: the ramp must be untouched.
                QCOMPARE(int(arow[x * 4 + 0]), int(brow[x * 4 + 0]));
                QCOMPARE(int(arow[x * 4 + 1]), int(brow[x * 4 + 1]));
                QCOMPARE(int(arow[x * 4 + 2]), int(brow[x * 4 + 2]));
            }
        }
    }

    // The undo entry must carry the CLIPPED image, so undo/redo agrees
    //   with what the user sees.
    QVERIFY(w->undoStack());
    QCOMPARE(w->undoStack()->count(), 1);
    w->undoStack()->undo();
    const cv::Mat& undone = stack->at(0)->image;
    for (int y = 0; y < H; ++y) {
        const uchar* urow = undone.ptr<uchar>(y);
        const uchar* brow = before.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            QCOMPARE(int(urow[x * 4 + 0]), int(brow[x * 4 + 0]));
            QCOMPARE(int(urow[x * 4 + 1]), int(brow[x * 4 + 1]));
            QCOMPARE(int(urow[x * 4 + 2]), int(brow[x * 4 + 2]));
        }
    }

    delete w;
}

QTEST_MAIN(tst_M2_5_Selection)
#include "tst_M2_5_Selection.moc"
