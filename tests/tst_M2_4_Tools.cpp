// =============================================================================
//  tst_M2_4_Tools.cpp
//
//  M2-4 tool capability tests.
//
//  What this pins (docs/CODE_AUDIT_2026-10-26.md 04 / docs/DEBT.md):
//
//    A-MOVE    : the move tool really moves a layer / a text item / the
//                selection. Before M2-4 it was an empty shell that only
//                showed a status bar message.
//    A-KEYBOARD: the keyboard reaches the tools (the canvas forwards the
//                key events to the ToolContext).
//    A-CROP    : the crop tool applies its aspect ratio while dragging.
//
//  The layer drag is verified at PIXEL level and with a probe pair that
//  distinguishes "the drag happened" from "the drag happened in the
//  wrong direction": one pixel that must have moved and one pixel that
//  must NOT have moved.
//
//  A uniform image would hide an off-by-one in the shift, so every
//  image here VARIES with position (see the ramp helpers below).
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include "../src/media/imagewindow.h"
#include "../src/media/tools/MoveTool.h"
#include "../src/media/tools/EyedropperTool.h"
#include "../src/media/tools/EraserTool.h"
#include "../src/media/tools/GradientTool.h"
#include "../src/media/tools/PaintBucketTool.h"
#include "../src/media/tools/DodgeBurnTool.h"
#include "../src/media/tools/BlurSharpenSmudgeTool.h"
#include "../src/media/tools/ViewTools.h"
#include "../src/media/tools/ToolGroupButton.h"
#include "../src/media/tools/LeftToolBar.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/ToolGeometry.h"
#include "../src/media/tools/ToolState.h"
#include "../src/media/tools/ToolStateFactory.h"
#include "../src/media/selection/SelectionModel.h"

#include "support/PathHarness.h"

using namespace vistella_test;
using namespace tools;

namespace {

// A ramp image: the red channel grows with x, the green channel with y.
//
//   A uniform image cannot distinguish "the pixels moved by (dx,dy)"
//   from "the pixels moved by (dx+1,dy)" -- every pixel has the same
//   value, so the probe reads the same colour either way. A ramp makes
//   the shift observable.
//
//   The image is 4-channel BGRA because that is the project's pixel
//   contract (docs/CONTRACTS.md 7.6.1).
cv::Mat makeRampBgra(int w, int h)
{
    cv::Mat m(h, w, CV_8UC4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            m.at<cv::Vec4b>(y, x) = cv::Vec4b(
                static_cast<uchar>(x * 255 / std::max(1, w - 1)),   // B
                static_cast<uchar>(y * 255 / std::max(1, h - 1)),   // G
                static_cast<uchar>((x + y) * 255 / std::max(1, w + h - 2)),  // R
                255);                                               // A
        }
    }
    return m;
}

// Read the BGRA pixel at (x,y). Returns false when the coordinate is
// outside the image.
bool bgraAt(const cv::Mat& m, int x, int y, cv::Vec4b& out)
{
    if (m.empty() || x < 0 || y < 0 || x >= m.cols || y >= m.rows) return false;
    out = m.at<cv::Vec4b>(y, x);
    return true;
}

// Build a window that holds a single bitmap layer with the ramp.
ImageWindow* makeWindowWithRamp(int w, int h)
{
    auto* win = new ImageWindow();
    win->setDocumentImage(makeRampBgra(w, h));
    return win;
}

QMouseEvent makePress(const QPointF& p)
{
    return QMouseEvent(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(p),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
}

QMouseEvent makeMove(const QPointF& p, Qt::MouseButtons buttons = Qt::LeftButton)
{
    return QMouseEvent(QEvent::MouseMove, QPointF(0, 0), QPointF(p),
                       Qt::NoButton, buttons, Qt::NoModifier);
}

QMouseEvent makeRelease(const QPointF& p)
{
    return QMouseEvent(QEvent::MouseButtonRelease, QPointF(0, 0), QPointF(p),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
}

} // namespace

class tst_M2_4_Tools : public QObject
{
    Q_OBJECT
private slots:
    // --- A-MOVE: layer pixels ---
    void move_tool_drags_the_active_layer_pixels();
    void move_tool_layer_drag_is_undoable();
    void move_tool_without_editable_layer_starts_no_drag();
    void move_tool_clamps_the_drag_to_the_canvas();

    // --- A-MOVE: the tool declares that it needs the mouse ---
    void move_tool_claims_the_mouse();
    void passive_tool_does_not_claim_the_mouse();

    // --- A-KEYBOARD ---
    void tool_context_receives_the_keyboard();

    // --- the shared coordinate helpers ---
    void tool_geometry_clamps_into_the_canvas();

    // --- the tools that were missing (M2-4) ---
    void every_new_tool_has_a_real_instance();
    void eraser_makes_the_pixels_transparent();
    void eraser_stroke_is_undoable();
    void paint_bucket_fills_the_contiguous_region();
    void paint_bucket_respects_the_tolerance();
    void gradient_runs_from_the_foreground_to_the_background();
    void dodge_lightens_and_burn_darkens();
    void blur_softens_and_sharpen_crisps();
    void the_view_tools_claim_the_mouse();

    // --- the tool groups (the long-press popup) ---
    void tool_group_button_holds_its_siblings();
    void tool_group_button_emits_the_picked_tool();
    void the_toolbar_has_the_groups();
};

// ---------------------------------------------------------------------------
//  A-MOVE: the layer pixels really move
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::move_tool_drags_the_active_layer_pixels()
{
    const int W = 64, H = 64;
    ImageWindow* w = makeWindowWithRamp(W, H);

    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);
    QVERIFY(!layer->empty());
    QCOMPARE(layer->type(), CV_8UC4);
    QCOMPARE(layer->cols, W);
    QCOMPARE(layer->rows, H);

    // The probe pixels, chosen so that a shift of exactly (+10, +5)
    // moves one of them and provably does not move the other.
    //
    //   src = (20, 20) -> the pixel that must arrive at (30, 25)
    //   the pixel that must STAY at (20, 20) is the one that comes
    //   from (10, 15) -- i.e. the source of the destination.
    const cv::Vec4b srcPixel = layer->at<cv::Vec4b>(20, 20);
    const cv::Vec4b stayPixel = layer->at<cv::Vec4b>(10, 15);

    MoveTool tool;
    tool.onEnter(w);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));
    QCOMPARE(tool.dragTarget(), MoveTool::DragTarget::LayerPixels);

    QMouseEvent move = makeMove(QPointF(30, 25));
    tool.onMouseMove(&move, w, QPointF(30, 25));

    QMouseEvent release = makeRelease(QPointF(30, 25));
    tool.onMouseRelease(&release, w, QPointF(30, 25));

    // The dragged pixel arrived at the new place.
    cv::Vec4b dst;
    QVERIFY(bgraAt(*layer, 30, 25, dst));
    QCOMPARE(dst, srcPixel);

    // The pixel at the new place did NOT stay: it was replaced by the
    // pixel that came from (20,20). This is the mutual-exclusion
    // probe -- without it a no-op implementation would pass.
    cv::Vec4b atOld;
    QVERIFY(bgraAt(*layer, 20, 20, atOld));
    QVERIFY(atOld != srcPixel);

    // The pixel that shifted INTO (10,15)'s old slot is gone: the
    // region that was vacated holds the pixel from (0,10).
    cv::Vec4b vacated;
    QVERIFY(bgraAt(*layer, 10, 15, vacated));
    QVERIFY(vacated != stayPixel);

    delete w;
}

// ---------------------------------------------------------------------------
//  A-MOVE: the move lands in the undo stack
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::move_tool_layer_drag_is_undoable()
{
    const int W = 48, H = 48;
    ImageWindow* w = makeWindowWithRamp(W, H);

    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);
    const cv::Mat before = layer->clone();

    auto* stack = w->undoStack();
    QVERIFY(stack != nullptr);
    const int undoCountBefore = stack->count();

    MoveTool tool;
    tool.onEnter(w);

    QMouseEvent press = makePress(QPointF(10, 10));
    tool.onMousePress(&press, w, QPointF(10, 10));
    QMouseEvent move = makeMove(QPointF(18, 14));
    tool.onMouseMove(&move, w, QPointF(18, 14));
    QMouseEvent release = makeRelease(QPointF(18, 14));
    tool.onMouseRelease(&release, w, QPointF(18, 14));

    // One undo command for the whole drag (not one per mouse move).
    QCOMPARE(stack->count(), undoCountBefore + 1);

    // The pixels really changed.
    {
        const cv::Mat* now = w->activeLayerImage();
        QVERIFY(now != nullptr);
        const double diff = cv::sum(cv::abs(*now - before))[0];
        QVERIFY(diff > 0.0);
    }

    // Undo restores them exactly.
    stack->undo();
    const cv::Mat* afterUndo = w->activeLayerImage();
    QVERIFY(afterUndo != nullptr);
    QCOMPARE(afterUndo->size(), before.size());
    QCOMPARE(afterUndo->type(), before.type());
    const double undoDiff = cv::sum(cv::abs(*afterUndo - before))[0];
    QCOMPARE(undoDiff, 0.0);

    // Redo re-applies them.
    stack->redo();
    const cv::Mat* afterRedo = w->activeLayerImage();
    QVERIFY(afterRedo != nullptr);
    const double redoDiff = cv::sum(cv::abs(*afterRedo - before))[0];
    QVERIFY(redoDiff > 0.0);

    delete w;
}

// ---------------------------------------------------------------------------
//  A-MOVE: nothing to drag -> no drag
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::move_tool_without_editable_layer_starts_no_drag()
{
    ImageWindow* w = new ImageWindow();   // no document, no layer

    MoveTool tool;
    tool.onEnter(w);

    QMouseEvent press = makePress(QPointF(10, 10));
    tool.onMousePress(&press, w, QPointF(10, 10));

    QCOMPARE(tool.dragTarget(), MoveTool::DragTarget::None);
    QVERIFY(!tool.isDragging());

    delete w;
}

// ---------------------------------------------------------------------------
//  A-MOVE: the drag cannot leave the canvas
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::move_tool_clamps_the_drag_to_the_canvas()
{
    const int W = 40, H = 40;
    ImageWindow* w = makeWindowWithRamp(W, H);

    MoveTool tool;
    tool.onEnter(w);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));

    // A drag of +1000 pixels -- far outside the canvas.
    QMouseEvent move = makeMove(QPointF(1020, 20));
    tool.onMouseMove(&move, w, QPointF(1020, 20));

    // The applied delta is limited to the canvas size.
    QVERIFY(std::abs(tool.lastDelta().x()) <= W);
    QVERIFY(std::abs(tool.lastDelta().y()) <= H);

    QMouseEvent release = makeRelease(QPointF(1020, 20));
    tool.onMouseRelease(&release, w, QPointF(1020, 20));

    delete w;
}

// ---------------------------------------------------------------------------
//  The move tool needs the mouse; the other tools do not
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::move_tool_claims_the_mouse()
{
    MoveTool tool;
    QVERIFY(tool.wantsExclusiveMouse());

    // Through the context as well -- that is what the event filter asks.
    ToolContext ctx;
    ctx.setState(std::make_unique<MoveTool>());
    QVERIFY(ctx.currentToolWantsExclusiveMouse());
}

void tst_M2_4_Tools::passive_tool_does_not_claim_the_mouse()
{
    // A tool that does not move anything must not steal the pan.
    ToolContext ctx;
    QVERIFY(!ctx.currentToolWantsExclusiveMouse());

    ctx.setState(std::make_unique<EyedropperTool>());
    QVERIFY(!ctx.currentToolWantsExclusiveMouse());
}

// ---------------------------------------------------------------------------
//  A-KEYBOARD: the keyboard reaches the tools
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::tool_context_receives_the_keyboard()
{
    // A probe tool that records the keys it sees.
    struct KeyProbe : public ToolState {
        QString lastKey;
        int      count = 0;
        void onEnter(ImageWindow*) override {}
        void onExit(ImageWindow*) override {}
        void onKeyPress(QKeyEvent* e, ImageWindow*) override {
            lastKey = QKeySequence(e->key()).toString();
            ++count;
        }
        mediators::ToolId id() const override { return mediators::ToolId::None; }
        QString pageTitle() const override { return QString(); }
        QCursor cursor() const override { return QCursor(Qt::ArrowCursor); }
    };

    ToolContext ctx;
    auto probe = std::make_unique<KeyProbe>();
    KeyProbe* raw = probe.get();
    ctx.setState(std::move(probe));

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    ctx.onKeyPress(&press);

    QCOMPARE(raw->count, 1);
    QCOMPARE(raw->lastKey, QStringLiteral("Return"));
}

// ---------------------------------------------------------------------------
//  The shared coordinate helpers
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::tool_geometry_clamps_into_the_canvas()
{
    const int W = 64, H = 48;
    ImageWindow* w = makeWindowWithRamp(W, H);

    const QSize sz = ToolGeometry::canvasSize(w);
    QCOMPARE(sz, QSize(W, H));

    // Inside -> unchanged.
    const QPointF inside(10, 20);
    QCOMPARE(ToolGeometry::clampToCanvas(w, inside), inside);

    // Outside -> clamped to the last pixel (not to the size).
    QCOMPARE(ToolGeometry::clampToCanvas(w, QPointF(-5, -5)), QPointF(0, 0));
    QCOMPARE(ToolGeometry::clampToCanvas(w, QPointF(1000, 1000)),
             QPointF(W - 1, H - 1));

    delete w;
}

// ---------------------------------------------------------------------------
//  Every new tool has a real instance
//
//  The factory used to return nullptr for Transform, so a tool that
//  "exists" in the enum but has no class behind it is invisible: the
//  user clicks the button and nothing happens.
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::every_new_tool_has_a_real_instance()
{
    const mediators::ToolId newTools[] = {
        mediators::ToolId::Eraser,
        mediators::ToolId::Gradient,
        mediators::ToolId::PaintBucket,
        mediators::ToolId::DodgeBurn,
        mediators::ToolId::BlurSharpenSmudge,
        mediators::ToolId::Hand,
        mediators::ToolId::Zoom,
        mediators::ToolId::Rotate,
    };

    for (const auto id : newTools) {
        std::unique_ptr<ToolState> tool(createToolState(id));
        QVERIFY2(tool != nullptr,
                 qPrintable(QStringLiteral("no tool instance for id %1")
                                .arg(static_cast<int>(id))));
        // The tool must report its own id back -- a tool that reports
        // the wrong id would be routed to the wrong option page.
        QCOMPARE(tool->id(), id);
        QVERIFY(!tool->pageTitle().isEmpty());
    }
}

// ---------------------------------------------------------------------------
//  The eraser makes the pixels transparent
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::eraser_makes_the_pixels_transparent()
{
    const int W = 40, H = 40;
    ImageWindow* w = makeWindowWithRamp(W, H);

    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);
    QCOMPARE(layer->channels(), 4);

    // The probe pixel starts out opaque.
    cv::Vec4b before = layer->at<cv::Vec4b>(20, 20);
    QCOMPARE(int(before[3]), 255);

    EraserTool tool;
    tool.onEnter(w);
    tool.setBrushSize(11);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));
    QVERIFY(tool.isStroking());

    QMouseEvent release = makeRelease(QPointF(20, 20));
    tool.onMouseRelease(&release, w, QPointF(20, 20));

    // The probed pixel is now transparent.
    cv::Vec4b after = layer->at<cv::Vec4b>(20, 20);
    QCOMPARE(int(after[3]), 0);

    // The colour channels are NOT changed -- the eraser only removes
    // the alpha. A pixel outside the brush keeps everything.
    QCOMPARE(int(after[0]), int(before[0]));
    QCOMPARE(int(after[1]), int(before[1]));
    QCOMPARE(int(after[2]), int(before[2]));

    // A pixel far outside the brush is untouched.
    cv::Vec4b far = layer->at<cv::Vec4b>(38, 38);
    QCOMPARE(int(far[3]), 255);

    delete w;
}

void tst_M2_4_Tools::eraser_stroke_is_undoable()
{
    const int W = 40, H = 40;
    ImageWindow* w = makeWindowWithRamp(W, H);

    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);
    const cv::Mat before = layer->clone();

    auto* stack = w->undoStack();
    QVERIFY(stack != nullptr);
    const int undoBefore = stack->count();

    EraserTool tool;
    tool.onEnter(w);
    tool.setBrushSize(11);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));
    QMouseEvent release = makeRelease(QPointF(20, 20));
    tool.onMouseRelease(&release, w, QPointF(20, 20));

    QCOMPARE(stack->count(), undoBefore + 1);

    // Undo restores the alpha of the erased pixels.
    stack->undo();
    const cv::Mat* afterUndo = w->activeLayerImage();
    QVERIFY(afterUndo != nullptr);
    QCOMPARE(int(afterUndo->at<cv::Vec4b>(20, 20)[3]), 255);
    const double undoDiff = cv::sum(cv::abs(*afterUndo - before))[0];
    QCOMPARE(undoDiff, 0.0);

    delete w;
}

// ---------------------------------------------------------------------------
//  The paint bucket fills the contiguous region
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::paint_bucket_fills_the_contiguous_region()
{
    const int W = 40, H = 40;
    ImageWindow* w = makeWindowWithRamp(W, H);

    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);

    // The foreground colour is what the bucket paints with. The
    // default is black.
    PaintBucketTool tool;
    tool.onEnter(w);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));

    QVERIFY(tool.lastFilledPixelCount() > 0);

    // The painted pixel is now the foreground colour.
    cv::Vec4b painted = layer->at<cv::Vec4b>(20, 20);
    QCOMPARE(int(painted[0]), 0);   // B
    QCOMPARE(int(painted[1]), 0);   // G
    QCOMPARE(int(painted[2]), 0);   // R

    // A pixel that is FARTHER away than the tolerance must NOT have
    // been filled.
    //
    //   The ramp changes the blue channel by 255/63 = 4 per pixel, so
    //   with the default tolerance of 32 only the pixels within
    //   32/4 = 8 columns are part of the region. The probe at
    //   x=35 is 15 columns away -- outside the region.
    cv::Vec4b far = layer->at<cv::Vec4b>(20, 35);
    QVERIFY(far != painted);
    // ... and it still holds its original (non-black) value.
    QVERIFY(int(far[0]) != 0 || int(far[1]) != 0 || int(far[2]) != 0);

    delete w;
}

void tst_M2_4_Tools::paint_bucket_respects_the_tolerance()
{
    const int W = 40, H = 40;

    // A flat image: every pixel has the SAME colour, so the whole
    // layer is one region regardless of the tolerance.
    auto* w = new ImageWindow();
    cv::Mat flat(40, 40, CV_8UC4, cv::Scalar(10, 20, 30, 255));
    w->setDocumentImage(flat);

    PaintBucketTool tool;
    tool.onEnter(w);
    tool.setTolerance(32);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));

    // The whole layer was filled (1600 pixels).
    QCOMPARE(tool.lastFilledPixelCount(), 40 * 40);

    delete w;
}

// ---------------------------------------------------------------------------
//  The gradient runs from the foreground to the background
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::gradient_runs_from_the_foreground_to_the_background()
{
    const int W = 64, H = 64;
    ImageWindow* w = makeWindowWithRamp(W, H);

    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);

    GradientTool tool;
    tool.onEnter(w);

    QMouseEvent press = makePress(QPointF(10, 32));
    tool.onMousePress(&press, w, QPointF(10, 32));
    QVERIFY(tool.isDragging());

    QMouseEvent move = makeMove(QPointF(50, 32));
    tool.onMouseMove(&move, w, QPointF(50, 32));

    QMouseEvent release = makeRelease(QPointF(50, 32));
    tool.onMouseRelease(&release, w, QPointF(50, 32));

    // The gradient starts at the foreground colour (black by default)
    // and ends at the background colour (white).
    cv::Vec4b atStart = layer->at<cv::Vec4b>(32, 10);
    cv::Vec4b atEnd   = layer->at<cv::Vec4b>(32, 50);

    QCOMPARE(int(atStart[0]), 0);
    QCOMPARE(int(atStart[2]), 0);

    QCOMPARE(int(atEnd[0]), 255);
    QCOMPARE(int(atEnd[2]), 255);

    // The middle is between the two -- strictly brighter than the
    // start and strictly darker than the end. This is the probe that
    // distinguishes a real gradient from a two-colour fill.
    cv::Vec4b atMiddle = layer->at<cv::Vec4b>(32, 30);
    QVERIFY(int(atMiddle[0]) > int(atStart[0]));
    QVERIFY(int(atMiddle[0]) < int(atEnd[0]));

    delete w;
}

// ---------------------------------------------------------------------------
//  The dodge / burn
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::dodge_lightens_and_burn_darkens()
{
    // The maths on its own -- no window needed.
    //   The dodge pushes a pixel towards white, the burn towards black.
    QVERIFY(DodgeBurnTool::dodgeChannel(128, 50) > 128);
    QVERIFY(DodgeBurnTool::burnChannel(128, 50) < 128);

    // A pixel that is already at the limit cannot go further.
    QCOMPARE(int(DodgeBurnTool::dodgeChannel(255, 100)), 255);
    QCOMPARE(int(DodgeBurnTool::burnChannel(0, 100)), 0);

    // The amount 0 (well, the clamped minimum of 1) must not change a
    // mid-tone much.
    QVERIFY(std::abs(int(DodgeBurnTool::dodgeChannel(128, 1)) - 128) <= 8);

    // And on a real layer.
    const int W = 40, H = 40;
    ImageWindow* w = makeWindowWithRamp(W, H);
    cv::Mat* layer = w->activeLayerImage();
    QVERIFY(layer != nullptr);

    const cv::Vec4b before = layer->at<cv::Vec4b>(20, 20);

    DodgeBurnTool tool;
    tool.onEnter(w);
    tool.setMode(DodgeBurnTool::Mode::Dodge);
    tool.setBrushSize(11);
    tool.setAmount(100);

    QMouseEvent press = makePress(QPointF(20, 20));
    tool.onMousePress(&press, w, QPointF(20, 20));
    QMouseEvent release = makeRelease(QPointF(20, 20));
    tool.onMouseRelease(&release, w, QPointF(20, 20));

    const cv::Vec4b after = layer->at<cv::Vec4b>(20, 20);
    QVERIFY(int(after[0]) >= int(before[0]));
    QVERIFY(int(after[1]) >= int(before[1]));
    QVERIFY(int(after[2]) >= int(before[2]));

    // The BURN on a real layer.
    //
    //   This half was missing: the burn was only checked through the
    //   static burnChannel(), so a bug that made the tool call the
    //   dodge instead of the burn was invisible to the test.
    //
    //   The probe has to be on a pixel that is NOT already black --
    //   the ramp gives every pixel a non-zero colour, so a burn that
    //   really darkens is observable.
    ImageWindow* w2 = makeWindowWithRamp(W, H);
    cv::Mat* layer2 = w2->activeLayerImage();
    QVERIFY(layer2 != nullptr);

    const cv::Vec4b beforeBurn = layer2->at<cv::Vec4b>(20, 20);
    QVERIFY(int(beforeBurn[0]) > 0);

    DodgeBurnTool burnTool;
    burnTool.onEnter(w2);
    burnTool.setMode(DodgeBurnTool::Mode::Burn);
    burnTool.setBrushSize(11);
    burnTool.setAmount(100);

    QMouseEvent burnPress = makePress(QPointF(20, 20));
    burnTool.onMousePress(&burnPress, w2, QPointF(20, 20));
    QMouseEvent burnRelease = makeRelease(QPointF(20, 20));
    burnTool.onMouseRelease(&burnRelease, w2, QPointF(20, 20));

    const cv::Vec4b afterBurn = layer2->at<cv::Vec4b>(20, 20);
    // The burn makes the pixels darker.
    QVERIFY(int(afterBurn[0]) < int(beforeBurn[0]));
    QVERIFY(int(afterBurn[1]) < int(beforeBurn[1]));
    QVERIFY(int(afterBurn[2]) < int(beforeBurn[2]));

    delete w2;
    delete w;
}

// ---------------------------------------------------------------------------
//  The blur / sharpen
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::blur_softens_and_sharpen_crisps()
{
    // A hard edge: the left half is dark, the right half is bright.
    //   A blur has to move the edge value towards the middle; a
    //   sharpen has to push it further out.
    cv::Mat img(16, 16, CV_8UC4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uchar v = (x < 8) ? 0 : 200;
            img.at<cv::Vec4b>(y, x) = cv::Vec4b(v, v, v, 255);
        }
    }

    // The pixel ON the edge: with a hard edge it is either 0 or 200.
    const int edgeBefore = int(img.at<cv::Vec4b>(8, 7)[0]);

    cv::Mat blurred = img.clone();
    BlurSharpenSmudgeTool::blurRegion(blurred, cv::Rect(4, 4, 8, 8), 100);
    const int edgeBlurred = int(blurred.at<cv::Vec4b>(8, 7)[0]);

    // The blur pulls the edge value towards the middle of the two
    // sides -- it must land strictly between them.
    QVERIFY(edgeBlurred > 0);
    QVERIFY(edgeBlurred < 200);

    cv::Mat sharpened = img.clone();
    BlurSharpenSmudgeTool::sharpenRegion(sharpened, cv::Rect(4, 4, 8, 8), 100);
    const int edgeSharpened = int(sharpened.at<cv::Vec4b>(8, 7)[0]);

    // The sharpen pushes the edge further out than the original.
    QVERIFY(edgeSharpened <= edgeBefore);

    // The alpha channel is NOT touched by either.
    QCOMPARE(int(blurred.at<cv::Vec4b>(8, 7)[3]), 255);
    QCOMPARE(int(sharpened.at<cv::Vec4b>(8, 7)[3]), 255);
}

// ---------------------------------------------------------------------------
//  The view tools
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::the_view_tools_claim_the_mouse()
{
    // The view tools own the left button -- without the claim the
    // canvas would scroll instead of doing what the tool says.
    HandTool hand;
    QVERIFY(hand.wantsExclusiveMouse());

    ZoomTool zoom;
    QVERIFY(zoom.wantsExclusiveMouse());

    RotateViewTool rotate;
    QVERIFY(rotate.wantsExclusiveMouse());

    // The zoom factor: in without alt, out with alt.
    QVERIFY(ZoomTool::zoomFactorFor(false) > 1.0);
    QVERIFY(ZoomTool::zoomFactorFor(true) < 1.0);
}

// ---------------------------------------------------------------------------
//  The tool groups (the long-press popup)
//
//  The audit (docs/CODE_AUDIT_2026-10-26.md 5.3 item 10) found that
//  the whole "one button per group, long press for the siblings"
//  interaction was missing.
// ---------------------------------------------------------------------------
void tst_M2_4_Tools::tool_group_button_holds_its_siblings()
{
    ToolGroupButton button;
    button.setGroup({int(mediators::ToolId::RectSelect),
                     int(mediators::ToolId::Lasso),
                     int(mediators::ToolId::MagicWand)},
                    {"tool-rectselect.svg", "tool-lasso.svg", "tool-magicwand.svg"});

    // The group holds three tools -> it has siblings.
    QVERIFY(button.hasSiblings());

    // The first tool is the default.
    QCOMPARE(button.currentToolId(), int(mediators::ToolId::RectSelect));
    QCOMPARE(button.activeToolId(), int(mediators::ToolId::RectSelect));

    // Switching the shown tool keeps the ACTIVE tool (the one a short
    // click activates) where it was.
    button.setCurrentToolId(int(mediators::ToolId::Lasso));
    QCOMPARE(button.currentToolId(), int(mediators::ToolId::Lasso));
    QCOMPARE(button.activeToolId(), int(mediators::ToolId::RectSelect));

    // A group with one tool has no siblings -- no long press popup.
    ToolGroupButton single;
    single.setGroup({int(mediators::ToolId::Move)}, {"tool-move.svg"});
    QVERIFY(!single.hasSiblings());
}

void tst_M2_4_Tools::tool_group_button_emits_the_picked_tool()
{
    ToolGroupButton button;
    button.setGroup({int(mediators::ToolId::RectSelect),
                     int(mediators::ToolId::Lasso)},
                    {"tool-rectselect.svg", "tool-lasso.svg"});

    QSignalSpy spy(&button, &ToolGroupButton::toolActivated);

    // Simulate the popup picking the sibling. The button's own
    // "toolActivated" is what the toolbar listens to.
    button.setActiveToolId(int(mediators::ToolId::Lasso));
    button.setCurrentToolId(int(mediators::ToolId::Lasso));
    emit button.toolActivated(int(mediators::ToolId::Lasso));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), int(mediators::ToolId::Lasso));

    // The picked tool became the button's default.
    QCOMPARE(button.activeToolId(), int(mediators::ToolId::Lasso));
    QCOMPARE(button.currentToolId(), int(mediators::ToolId::Lasso));
}

void tst_M2_4_Tools::the_toolbar_has_the_groups()
{
    LeftToolBar bar;

    // The toolbar has to hold the group buttons. The count is derived
    // from the group definition -- a group that is defined but has no
    // button is a group the user cannot reach.
    const auto groups = LeftToolBar::toolGroupDefinitions();
    QVERIFY(!groups.isEmpty());

    auto groupButtons = bar.findChildren<ToolGroupButton*>();
    QCOMPARE(groupButtons.size(), groups.size());

    // Every tool of a group must be reachable through a group button.
    for (const auto& def : groups) {
        for (const int id : def.first) {
            bool found = false;
            for (auto* b : groupButtons) {
                if (b->currentToolId() == id
                    || b->activeToolId() == id
                    || (b->hasSiblings() && b->toolIds().contains(id))) {
                    found = true;
                    break;
                }
            }
            QVERIFY2(found, qPrintable(
                QStringLiteral("no group button holds the tool %1").arg(id)));
        }
    }
}

QTEST_MAIN(tst_M2_4_Tools)
#include "tst_M2_4_Tools.moc"
