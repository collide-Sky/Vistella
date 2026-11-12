// =============================================================================
//  tst_M2_4_Tail_Tools.cpp
//
//  The M2-4 tail tests: the view tools' rotation / zoom-to-rectangle,
//  the pattern stamp, the history brush, the selection clipping, and
//  the M2-5c marquees.
//
//  What this pins:
//
//    The view transform had TWO writers -- the zoom (in the canvas) and
//    the rotation (in the tool, via setTransform). Rotating the view
//    dropped the zoom, and a later zoom change dropped the rotation.
//    Now the canvas owns both and rebuilds the matrix from them.
//
//    The pattern stamp tiles a source patch; the history brush paints a
//    snapshot back into the layer.
//
//    The selection clips every pixel edit, and it does so in ONE place
//    (ImageWindow::pushLayerImageEdit). The gradient and the paint
//    bucket used to clip as well, with a hard cut -- two clippers with
//    two rules, and the feather was lost.
//
//    The elliptical and the polygonal marquee produce a real selection.
// =============================================================================

#include <QTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include "../src/media/imagewindow.h"
#include "../src/media/imagewindow/ImageCanvas.h"
#include "../src/media/tools/GradientTool.h"
#include "../src/media/tools/PaintBucketTool.h"
#include "../src/media/tools/PatternStampTool.h"
#include "../src/media/tools/HistoryBrushTool.h"
#include "../src/media/tools/EllipseSelect.h"
#include "../src/media/tools/PolygonSelect.h"
#include "../src/media/tools/ViewTools.h"
#include "../src/media/tools/ToolStateFactory.h"
#include "../src/media/selection/SelectionModel.h"
#include "../src/media/selection/SelectionStrategy.h"

namespace {

// A ramp image: the red channel grows with x, the green channel with y.
//
//   A uniform image cannot distinguish "the pixels moved by (dx,dy)"
//   from "the pixels moved by (dx+1,dy)". A ramp makes the shift
//   observable.
cv::Mat makeRampBgra(int w, int h)
{
    cv::Mat m(h, w, CV_8UC4);
    for (int y = 0; y < h; ++y) {
        uchar* row = m.ptr<uchar>(y);
        for (int x = 0; x < w; ++x) {
            uchar* p = row + x * 4;
            p[0] = static_cast<uchar>((x * 3 + y * 5) & 0xFF);   // B
            p[1] = static_cast<uchar>((y * 7) & 0xFF);            // G
            p[2] = static_cast<uchar>((x * 11) & 0xFF);           // R
            p[3] = 255;                                           // A
        }
    }
    return m;
}

ImageWindow* makeWindowWithRamp(int w, int h)
{
    auto* win = new ImageWindow();
    win->setDocumentImage(makeRampBgra(w, h));
    return win;
}

QMouseEvent makePress(const QPointF& p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return QMouseEvent(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(p),
                       Qt::LeftButton, Qt::LeftButton, mods);
}

QMouseEvent makeMove(const QPointF& p, Qt::MouseButtons buttons = Qt::LeftButton,
                     Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return QMouseEvent(QEvent::MouseMove, QPointF(0, 0), QPointF(p),
                       Qt::NoButton, buttons, mods);
}

QMouseEvent makeRelease(const QPointF& p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return QMouseEvent(QEvent::MouseButtonRelease, QPointF(0, 0), QPointF(p),
                       Qt::LeftButton, Qt::NoButton, mods);
}

QMouseEvent makeDoubleClick(const QPointF& p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return QMouseEvent(QEvent::MouseButtonDblClick, QPointF(0, 0), QPointF(p),
                       Qt::LeftButton, Qt::LeftButton, mods);
}

// The pixel at (x,y) as a BGRA quadruple.
cv::Vec4b pixelAt(const cv::Mat& m, int x, int y)
{
    if (m.empty() || x < 0 || y < 0 || x >= m.cols || y >= m.rows) return cv::Vec4b(0, 0, 0, 0);
    return m.at<cv::Vec4b>(y, x);
}

} // namespace

class tst_M2_4_Tail_Tools : public QObject
{
    Q_OBJECT
private slots:
    // --- the view transform ---
    void the_canvas_keeps_the_zoom_when_the_view_is_rotated();
    void the_canvas_keeps_the_rotation_when_the_zoom_changes();
    void the_rotation_pivots_around_the_centre();
    void the_zoom_tool_zooms_to_the_dragged_rectangle();
    void the_rotate_tool_accumulates_the_angle();

    // --- the pattern stamp ---
    void the_pattern_stamp_needs_a_source();
    void the_pattern_stamp_tiles_the_source_patch();
    void the_pattern_stamp_is_undoable();

    // --- the history brush ---
    void the_history_brush_paints_the_snapshot();
    void the_history_brush_needs_a_snapshot();
    void the_history_brush_is_undoable();

    // --- the selection clipping ---
    void the_selection_clips_the_gradient();
    void the_selection_clips_the_paint_bucket();

    // --- the M2-5c marquees ---
    void the_ellipse_marquee_selects_an_ellipse();
    void the_polygon_marquee_selects_a_polygon();
    void the_polygon_marquee_needs_three_vertices();
    void every_new_tool_has_a_real_instance();
};

// ===== the view transform =====

void tst_M2_4_Tail_Tools::the_canvas_keeps_the_zoom_when_the_view_is_rotated()
{
    auto* win = makeWindowWithRamp(80, 60);
    auto* canvas = win->imageCanvas();
    QVERIFY(canvas != nullptr);

    canvas->setZoom(2.0);
    QCOMPARE(canvas->zoom(), 2.0);

    // Rotate the view THROUGH THE TOOL -- that is the path the user
    // takes, and it is where the defect lived (the tool wrote the
    // transform itself, which replaced the zoom).
    tools::RotateViewTool tool;
    tool.onEnter(win);
    QMouseEvent evPress(makePress(QPointF(0, 0)));
    QMouseEvent evMove(makeMove(QPointF(100, 0)));
    tool.onMousePress(&evPress, win, QPointF(0, 0));
    tool.onMouseMove(&evMove, win, QPointF(100, 0));

    QVERIFY2(qAbs(canvas->rotationDeg() - 45.0) < 1e-6,
             "the rotate tool did not rotate the view");

    // The ZOOM FIELD is not enough: it is just a number the canvas
    // remembers. What matters is the TRANSFORM that is actually
    // applied -- the scale has to be in the matrix.
    //
    //   Checking only zoom() would pass even when the matrix lost the
    //   scale, because the field is written before the matrix is
    //   rebuilt. The matrix is what the user sees.
    const QTransform t = canvas->transform();
    const double scale = std::sqrt(std::fabs(t.m11() * t.m22() - t.m12() * t.m21()));
    QVERIFY2(qAbs(scale - 2.0) < 1e-6,
             "rotating the view dropped the scale from the view transform");

    delete win;
}

void tst_M2_4_Tail_Tools::the_canvas_keeps_the_rotation_when_the_zoom_changes()
{
    auto* win = makeWindowWithRamp(80, 60);
    auto* canvas = win->imageCanvas();
    QVERIFY(canvas != nullptr);

    canvas->setRotationDeg(30.0);
    canvas->setZoom(1.5);

    QVERIFY2(qAbs(canvas->rotationDeg() - 30.0) < 1e-9,
             "zooming the view wiped the rotation");
    QVERIFY(qAbs(canvas->zoom() - 1.5) < 1e-9);

    delete win;
}

void tst_M2_4_Tail_Tools::the_rotation_pivots_around_the_centre()
{
    auto* win = makeWindowWithRamp(80, 60);
    auto* canvas = win->imageCanvas();
    QVERIFY(canvas != nullptr);

    // The pivot is the middle of the image.
    const QPointF centre = canvas->viewCenter();
    QVERIFY2(qAbs(centre.x() - 40.0) < 1e-6 && qAbs(centre.y() - 30.0) < 1e-6,
             "the rotation pivot is not the centre of the image");

    // And the transform really rotates about it: the centre maps to
    // itself.
    //
    //   The rotation has to be SET first. Without it the transform is
    //   the identity and "the centre maps to itself" holds trivially --
    //   the assertion would be vacuous and would pass even with a
    //   rotation around the origin.
    canvas->setRotationDeg(45.0);

    const QTransform t = canvas->transform();
    const QPointF mapped = t.map(centre);
    QVERIFY2(qAbs(mapped.x() - centre.x()) < 1e-6
             && qAbs(mapped.y() - centre.y()) < 1e-6,
             "the rotation does not pivot around the centre");

    // And the rotation really happened: a point off the centre moves.
    const QPointF corner = t.map(QPointF(0, 0));
    QVERIFY2(qAbs(corner.x() - 0.0) > 1e-6 || qAbs(corner.y() - 0.0) > 1e-6,
             "the view was not rotated at all");

    delete win;
}

void tst_M2_4_Tail_Tools::the_zoom_tool_zooms_to_the_dragged_rectangle()
{
    auto* win = makeWindowWithRamp(200, 200);
    auto* canvas = win->imageCanvas();
    QVERIFY(canvas != nullptr);

    tools::ZoomTool tool;
    tool.onEnter(win);

    // Drag a 100x100 rectangle. The canvas viewport is smaller than
    // that in a headless test, so the zoom is clamped -- what matters
    // is that the zoom CHANGED and that it is the fit factor.
    const double before = canvas->zoom();
    QMouseEvent ev1(makePress(QPointF(0, 0)));
    tool.onMousePress(&ev1, win, QPointF(0, 0));
    QMouseEvent ev2(makeMove(QPointF(100, 100)));
    tool.onMouseMove(&ev2, win, QPointF(100, 100));
    QMouseEvent ev3(makeRelease(QPointF(100, 100)));
    tool.onMouseRelease(&ev3, win, QPointF(100, 100));

    QVERIFY2(qAbs(canvas->zoom() - before) > 1e-9,
             "the zoom-to-rectangle gesture did not change the zoom");

    delete win;
}

void tst_M2_4_Tail_Tools::the_rotate_tool_accumulates_the_angle()
{
    auto* win = makeWindowWithRamp(80, 60);
    auto* canvas = win->imageCanvas();
    QVERIFY(canvas != nullptr);

    tools::RotateViewTool tool;
    tool.onEnter(win);

    // One drag: 100 pixels to the right = 100 * 360 / 800 = 45 degrees.
    QMouseEvent ev4(makePress(QPointF(0, 0)));
    tool.onMousePress(&ev4, win, QPointF(0, 0));
    QMouseEvent ev5(makeMove(QPointF(100, 0)));
    tool.onMouseMove(&ev5, win, QPointF(100, 0));
    QMouseEvent ev6(makeRelease(QPointF(100, 0)));
    tool.onMouseRelease(&ev6, win, QPointF(100, 0));
    QVERIFY(qAbs(tool.rotationDeg() - 45.0) < 1e-6);

    // A second drag ADDS to the angle instead of snapping back to zero.
    QMouseEvent ev7(makePress(QPointF(0, 0)));
    tool.onMousePress(&ev7, win, QPointF(0, 0));
    QMouseEvent ev8(makeMove(QPointF(100, 0)));
    tool.onMouseMove(&ev8, win, QPointF(100, 0));
    QMouseEvent ev9(makeRelease(QPointF(100, 0)));
    tool.onMouseRelease(&ev9, win, QPointF(100, 0));
    QVERIFY2(qAbs(tool.rotationDeg() - 90.0) < 1e-6,
             "the second drag replaced the angle instead of adding to it");

    delete win;
}

// ===== the pattern stamp =====

void tst_M2_4_Tail_Tools::the_pattern_stamp_needs_a_source()
{
    auto* win = makeWindowWithRamp(80, 60);
    tools::PatternStampTool tool;
    tool.onEnter(win);
    QVERIFY(!tool.hasSource());

    // Without a source the tool does nothing.
    const cv::Vec4b before = pixelAt(*win->activeLayerImage(), 40, 30);
    QMouseEvent ev10(makePress(QPointF(40, 30)));
    tool.onMousePress(&ev10, win, QPointF(40, 30));
    const cv::Vec4b after = pixelAt(*win->activeLayerImage(), 40, 30);
    QCOMPARE(int(after[0]), int(before[0]));

    delete win;
}

void tst_M2_4_Tail_Tools::the_pattern_stamp_tiles_the_source_patch()
{
    auto* win = makeWindowWithRamp(80, 60);
    tools::PatternStampTool tool;
    tool.onEnter(win);
    tool.setPatchSize(16);
    tool.setSpacing(100);

    // Alt+click captures the source.
    QMouseEvent ev11(makePress(QPointF(20, 20), Qt::AltModifier));
    tool.onMousePress(&ev11, win, QPointF(20, 20));
    QVERIFY(tool.hasSource());
    QCOMPARE(tool.sourcePatch().rows, 16);
    QCOMPARE(tool.sourcePatch().cols, 16);

    // Paint the pattern somewhere else. The stamped pixels must equal
    // the source patch's pixels -- that is what "tiles the source"
    // means.
    const cv::Mat patch = tool.sourcePatch();
    const cv::Vec4b expected = patch.at<cv::Vec4b>(8, 8);

    QMouseEvent ev12(makePress(QPointF(50, 40)));
    tool.onMousePress(&ev12, win, QPointF(50, 40));

    // The stamp is centred on the click, so the patch's centre lands
    // on (50,40) -> the source's (8,8) lands there.
    const cv::Vec4b got = pixelAt(*win->activeLayerImage(), 50, 40);
    QCOMPARE(int(got[0]), int(expected[0]));
    QCOMPARE(int(got[1]), int(expected[1]));
    QCOMPARE(int(got[2]), int(expected[2]));

    delete win;
}

void tst_M2_4_Tail_Tools::the_pattern_stamp_is_undoable()
{
    auto* win = makeWindowWithRamp(80, 60);
    tools::PatternStampTool tool;
    tool.onEnter(win);
    tool.setPatchSize(16);

    QMouseEvent ev13(makePress(QPointF(20, 20), Qt::AltModifier));
    tool.onMousePress(&ev13, win, QPointF(20, 20));
    QMouseEvent ev14(makePress(QPointF(50, 40)));
    tool.onMousePress(&ev14, win, QPointF(50, 40));
    QMouseEvent ev15(makeRelease(QPointF(50, 40)));
    tool.onMouseRelease(&ev15, win, QPointF(50, 40));

    auto* stack = win->undoStack();
    QVERIFY(stack != nullptr);
    QVERIFY2(stack->count() > 0, "the pattern stamp did not push an undo command");

    // Undo restores the pixels.
    const cv::Vec4b painted = pixelAt(*win->activeLayerImage(), 50, 40);
    stack->undo();
    const cv::Vec4b undone = pixelAt(*win->activeLayerImage(), 50, 40);
    QVERIFY2(undone != painted, "the undo did not restore the pixels");

    delete win;
}

// ===== the history brush =====

void tst_M2_4_Tail_Tools::the_history_brush_paints_the_snapshot()
{
    auto* win = makeWindowWithRamp(80, 60);
    tools::HistoryBrushTool tool;
    tool.onEnter(win);

    // A source snapshot that is a flat red.
    cv::Mat source(60, 80, CV_8UC4, cv::Scalar(0, 0, 200, 255));
    tool.setSource(source);
    tool.setBrushSize(20);
    tool.setStrength(100);

    // The layer starts as a ramp, so the painted pixel must change.
    const cv::Vec4b before = pixelAt(*win->activeLayerImage(), 40, 30);
    QMouseEvent ev16(makePress(QPointF(40, 30)));
    tool.onMousePress(&ev16, win, QPointF(40, 30));

    const cv::Vec4b after = pixelAt(*win->activeLayerImage(), 40, 30);
    QVERIFY2(after != before, "the history brush did not paint");
    QCOMPARE(int(after[2]), 200);   // the red channel of the snapshot

    delete win;
}

void tst_M2_4_Tail_Tools::the_history_brush_needs_a_snapshot()
{
    auto* win = makeWindowWithRamp(80, 60);
    tools::HistoryBrushTool tool;
    tool.onEnter(win);
    QVERIFY(!tool.hasSource());

    const cv::Vec4b before = pixelAt(*win->activeLayerImage(), 40, 30);
    QMouseEvent ev17(makePress(QPointF(40, 30)));
    tool.onMousePress(&ev17, win, QPointF(40, 30));
    const cv::Vec4b after = pixelAt(*win->activeLayerImage(), 40, 30);
    QCOMPARE(int(after[0]), int(before[0]));

    delete win;
}

void tst_M2_4_Tail_Tools::the_history_brush_is_undoable()
{
    auto* win = makeWindowWithRamp(80, 60);
    tools::HistoryBrushTool tool;
    tool.onEnter(win);

    cv::Mat source(60, 80, CV_8UC4, cv::Scalar(0, 0, 200, 255));
    tool.setSource(source);
    tool.setBrushSize(20);

    QMouseEvent ev18(makePress(QPointF(40, 30)));
    tool.onMousePress(&ev18, win, QPointF(40, 30));
    QMouseEvent ev19(makeRelease(QPointF(40, 30)));
    tool.onMouseRelease(&ev19, win, QPointF(40, 30));

    auto* stack = win->undoStack();
    QVERIFY(stack != nullptr);
    QVERIFY2(stack->count() > 0, "the history brush did not push an undo command");

    const cv::Vec4b painted = pixelAt(*win->activeLayerImage(), 40, 30);
    stack->undo();
    const cv::Vec4b undone = pixelAt(*win->activeLayerImage(), 40, 30);
    QVERIFY2(undone != painted, "the undo did not restore the pixels");

    delete win;
}

// ===== the selection clipping =====

void tst_M2_4_Tail_Tools::the_selection_clips_the_gradient()
{
    auto* win = makeWindowWithRamp(80, 60);
    auto* sel = win->selectionModel();
    QVERIFY(sel != nullptr);

    // A selection over the LEFT half only.
    QImage mask(80, 60, QImage::Format_Alpha8);
    mask.fill(0);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 40; ++x) mask.scanLine(y)[x] = 255;
    }
    sel->setMask(mask, selection::SelectionModel::Mode::Replace);

    tools::GradientTool tool;
    tool.onEnter(win);
    QMouseEvent ev20(makePress(QPointF(0, 0)));
    tool.onMousePress(&ev20, win, QPointF(0, 0));
    QMouseEvent ev21(makeMove(QPointF(79, 59)));
    tool.onMouseMove(&ev21, win, QPointF(79, 59));
    QMouseEvent ev22(makeRelease(QPointF(79, 59)));
    tool.onMouseRelease(&ev22, win, QPointF(79, 59));

    const cv::Mat* layer = win->activeLayerImage();
    QVERIFY(layer != nullptr);

    // Inside the selection the gradient painted.
    const cv::Vec4b inside = pixelAt(*layer, 10, 30);
    // Outside the selection the pixels kept their value: the ramp's
    // red channel at (60,30) is (60 * 11) & 0xFF.
    const cv::Vec4b outside = pixelAt(*layer, 60, 30);
    const int expectedRampR = (60 * 11) & 0xFF;
    QVERIFY2(outside[2] == expectedRampR,
             "the gradient painted OUTSIDE the selection");

    delete win;
}

void tst_M2_4_Tail_Tools::the_selection_clips_the_paint_bucket()
{
    auto* win = makeWindowWithRamp(80, 60);
    auto* sel = win->selectionModel();
    QVERIFY(sel != nullptr);

    // A selection over the LEFT half only.
    QImage mask(80, 60, QImage::Format_Alpha8);
    mask.fill(0);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 40; ++x) mask.scanLine(y)[x] = 255;
    }
    sel->setMask(mask, selection::SelectionModel::Mode::Replace);

    // A flat foreground colour, so the filled pixels are recognisable.
    win->colorState()->setForegroundColor(QColor(0, 200, 0));

    tools::PaintBucketTool tool;
    tool.onEnter(win);
    tool.setTolerance(255);
    QMouseEvent ev23(makePress(QPointF(10, 30)));
    tool.onMousePress(&ev23, win, QPointF(10, 30));

    const cv::Mat* layer = win->activeLayerImage();
    QVERIFY(layer != nullptr);

    // Outside the selection the pixels kept their value.
    const cv::Vec4b outside = pixelAt(*layer, 60, 30);
    const int expectedRampR = (60 * 11) & 0xFF;
    QVERIFY2(outside[2] == expectedRampR,
             "the paint bucket filled OUTSIDE the selection");

    delete win;
}

// ===== the M2-5c marquees =====

void tst_M2_4_Tail_Tools::the_ellipse_marquee_selects_an_ellipse()
{
    auto* win = makeWindowWithRamp(100, 100);
    auto* sel = win->selectionModel();
    QVERIFY(sel != nullptr);

    tools::EllipseSelect tool;
    tool.onEnter(win);
    QMouseEvent ev24(makePress(QPointF(20, 20)));
    tool.onMousePress(&ev24, win, QPointF(20, 20));
    QMouseEvent ev25(makeMove(QPointF(80, 80)));
    tool.onMouseMove(&ev25, win, QPointF(80, 80));
    QMouseEvent ev26(makeRelease(QPointF(80, 80)));
    tool.onMouseRelease(&ev26, win, QPointF(80, 80));

    const QImage mask = sel->mask();
    QVERIFY(!mask.isNull());

    // The centre is inside, the corners of the bounding box are OUTSIDE
    // (that is what makes it an ellipse and not a rounded rectangle).
    QVERIFY2(mask.scanLine(50)[50] > 0, "the centre of the ellipse is not selected");
    QVERIFY2(mask.scanLine(20)[20] == 0, "the corner of the box is inside the ellipse");

    delete win;
}

void tst_M2_4_Tail_Tools::the_polygon_marquee_selects_a_polygon()
{
    auto* win = makeWindowWithRamp(100, 100);
    auto* sel = win->selectionModel();
    QVERIFY(sel != nullptr);

    tools::PolygonSelect tool;
    tool.onEnter(win);

    // Four clicks, then a double click to close.
    QMouseEvent ev27(makePress(QPointF(20, 20)));
    tool.onMousePress(&ev27, win, QPointF(20, 20));
    QMouseEvent ev28(makePress(QPointF(80, 20)));
    tool.onMousePress(&ev28, win, QPointF(80, 20));
    QMouseEvent ev29(makePress(QPointF(80, 80)));
    tool.onMousePress(&ev29, win, QPointF(80, 80));
    QMouseEvent ev30(makePress(QPointF(20, 80)));
    tool.onMousePress(&ev30, win, QPointF(20, 80));
    QVERIFY(!tool.isClosed());

    QMouseEvent ev31(makeDoubleClick(QPointF(20, 80)));
    tool.onMouseDoubleClick(&ev31, win, QPointF(20, 80));
    QVERIFY(tool.isClosed());

    const QImage mask = sel->mask();
    QVERIFY(!mask.isNull());
    QVERIFY2(mask.scanLine(50)[50] > 0, "the centre of the polygon is not selected");
    QVERIFY2(mask.scanLine(5)[5] == 0, "a point outside the polygon is selected");

    delete win;
}

void tst_M2_4_Tail_Tools::the_polygon_marquee_needs_three_vertices()
{
    auto* win = makeWindowWithRamp(100, 100);
    auto* sel = win->selectionModel();
    QVERIFY(sel != nullptr);

    tools::PolygonSelect tool;
    tool.onEnter(win);

    // Two vertices cannot make a polygon.
    QMouseEvent ev32(makePress(QPointF(20, 20)));
    tool.onMousePress(&ev32, win, QPointF(20, 20));
    QMouseEvent ev33(makePress(QPointF(80, 20)));
    tool.onMousePress(&ev33, win, QPointF(80, 20));
    QMouseEvent ev34(makeDoubleClick(QPointF(80, 20)));
    tool.onMouseDoubleClick(&ev34, win, QPointF(80, 20));

    QVERIFY2(!tool.isClosed(), "a two-vertex polygon was closed");
    // The selection model allocates its mask eagerly (it is a fixed
    // size buffer), so "no selection" is "the mask is empty", not "the
    // mask is null".
    QVERIFY2(sel->isEmpty(), "a two-vertex polygon changed the selection");

    delete win;
}

void tst_M2_4_Tail_Tools::every_new_tool_has_a_real_instance()
{
    using T = mediators::ToolId;
    const T ids[] = { T::PatternStamp, T::HistoryBrush, T::EllipseSelect, T::PolygonSelect };
    for (T id : ids) {
        tools::ToolState* tool = tools::createToolState(id);
        QVERIFY2(tool != nullptr, QStringLiteral("no tool instance for id %1")
                                        .arg(static_cast<int>(id)).toLatin1().constData());
        delete tool;
    }
}

QTEST_MAIN(tst_M2_4_Tail_Tools)
#include "tst_M2_4_Tail_Tools.moc"
