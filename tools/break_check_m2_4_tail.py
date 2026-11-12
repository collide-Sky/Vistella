"""Break self-check for the M2-4 tail and the M2-5c marquees.

Injects a deliberate bug and verifies that the EXPECTED regression test
goes RED. Then restores the file byte for byte.

Every injection targets the CAUSE of an effect the test asserts on -- not
something that merely looks like it. Every injection names the test that
must go red, and the check verifies the NAME, not just "something went
red":

  bug 1 (the rotation replaces the zoom)
     The rotate tool writes the transform itself (setTransform) instead
     of going through the canvas. setTransform REPLACES the whole
     matrix, so the zoom is gone -> the_canvas_keeps_the_zoom_when_the_view_is_rotated.

     The first version of this injection removed the rotation from
     applyTransform() instead -- and the PIVOT test caught that one.
     The two defects are different (one drops the scale, the other
     moves the pivot), so the injection has to hit the right one.

  bug 2 (the rotation pivots around the origin)
     The rotation is applied without the centre translate. The picture
     orbits the top-left corner instead of turning in place ->
     the_rotation_pivots_around_the_centre.

  bug 3 (the pattern stamp paints nothing)
     The stamp does not copy the patch. The pixel probe stays at the
     original value -> the_pattern_stamp_tiles_the_source_patch.

  bug 4 (the history brush paints nothing)
     The source is not written into the layer ->
     the_history_brush_paints_the_snapshot.

  bug 5 (the selection does not clip the edit)
     The central clip (ImageWindow::clipEditToSelection) stops
     clipping. That is the CAUSE of "the gradient stays inside the
     selection" -> the_selection_clips_the_gradient.

     The first version of this injection put a COMMENT into
     GradientTool -- it changed nothing, so the test stayed green. A
     comment is not a defect; the injection has to break the thing that
     actually does the clipping.

  bug 6 (the ellipse marquee fills its bounding box)
     The ellipse is drawn as a rectangle. The corner probe comes back
     inside -> the_ellipse_marquee_selects_an_ellipse.

  bug 7 (the polygon marquee closes with two vertices)
     The vertex count check is dropped. The selection changes ->
     the_polygon_marquee_needs_three_vertices.
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = r"D:\Collide\MyCode\QT6Projects\MultiDoc"
BUILD = os.path.join(ROOT, "build", "Desktop_Qt_6_11_1_MSVC2022_64bit_Debug")
QT_BIN = r"D:\QT6\6.11.1\msvc2022_64\bin"

CANVAS = os.path.join(ROOT, "src", "media", "imagewindow", "ImageCanvas.cpp")
VIEWTOOLS = os.path.join(ROOT, "src", "media", "tools", "ViewTools.cpp")
IMAGEWINDOW = os.path.join(ROOT, "src", "media", "imagewindow.cpp")
PATTERN = os.path.join(ROOT, "src", "media", "tools", "PatternStampTool.cpp")
HISTORY = os.path.join(ROOT, "src", "media", "tools", "HistoryBrushTool.cpp")
ELLIPSE = os.path.join(ROOT, "src", "media", "selection", "SelectionStrategy.cpp")
POLYGON = os.path.join(ROOT, "src", "media", "selection", "SelectionStrategy.cpp")

TOOLS_EXE = os.path.join(BUILD, "tst_M2_4_Tail_Tools.exe")


# ---------------------------------------------------------------------------
# Bug 1: the rotation replaces the zoom.
#
# The rotate tool writes the transform ITSELF (setTransform) instead of
# going through the canvas. setTransform REPLACES the whole matrix, so
# the zoom is gone -- that is exactly the defect the canvas-owned
# rotation was introduced to fix.
#
#   The earlier version of this injection removed the rotation from
#   applyTransform(), and the PIVOT test caught that one instead. The
#   zoom was still in the matrix (applyTransform still did scale()), so
#   the zoom assertion was right not to fire.
#
#   The two defects are different: one drops the scale, the other
#   moves the pivot. The injections have to hit the right one.
# ---------------------------------------------------------------------------
BUG1_OLD = """    // The VIEW is rotated, the pixels are not.
    //
    //   The canvas owns the rotation and rebuilds the transform from it
    //   together with the zoom, so rotating no longer throws the zoom
    //   away and a later zoom change no longer throws the rotation away.
    canvas->setRotationDeg(m_rotationDeg);"""

BUG1_NEW = """    // The VIEW is rotated, the pixels are not.
    //
    // BREAK SELF-CHECK: the tool writes the transform itself, so the
    // zoom is replaced by the rotation.
    QTransform t;
    t.rotate(m_rotationDeg);
    canvas->setTransform(t);"""


# ---------------------------------------------------------------------------
# Bug 2: the rotation pivots around the origin.
#
# The centre translate is gone, so the picture orbits the top-left
# corner instead of turning in place.
# ---------------------------------------------------------------------------
BUG2_OLD = """    if (!qFuzzyIsNull(m_rotationDeg)) {
        translate(center.x(), center.y());
        rotate(m_rotationDeg);
        translate(-center.x(), -center.y());
    }"""

BUG2_NEW = """    // BREAK SELF-CHECK: the rotation pivots around the origin.
    if (!qFuzzyIsNull(m_rotationDeg)) {
        rotate(m_rotationDeg);
    }"""


# ---------------------------------------------------------------------------
# Bug 3: the pattern stamp paints nothing.
#
# The copy into the layer is skipped.
# ---------------------------------------------------------------------------
BUG3_OLD = """    m_sourcePatch(srcRect).copyTo((*target)(clipped));
    ++m_lastStampCount;"""

BUG3_NEW = """    // BREAK SELF-CHECK: the patch is never written into the layer.
    Q_UNUSED(srcRect);
    ++m_lastStampCount;"""


# ---------------------------------------------------------------------------
# Bug 4: the history brush paints nothing.
#
# The blend loop is skipped.
# ---------------------------------------------------------------------------
BUG4_OLD = """            const double alpha = factor * coverage;
            for (int c = 0; c < colourCh; ++c) {"""

BUG4_NEW = """            const double alpha = factor * coverage;
            // BREAK SELF-CHECK: the source is never written.
            for (int c = 0; c < 0; ++c) {"""


# ---------------------------------------------------------------------------
# Bug 5: the selection does not clip the edit.
#
# The clipping happens in ONE place: ImageWindow::clipEditToSelection(),
# which ImageWindow::pushLayerImageEdit() calls. That is the CAUSE of
# "the gradient stays inside the selection".
#
# The earlier version of this injection put a comment into GradientTool
# -- it changed nothing, so the test stayed green. A comment is not a
# defect. The injection has to break the thing that actually does the
# clipping.
# ---------------------------------------------------------------------------
BUG5_OLD = """    const int W = after.cols;
    const int H = after.rows;
    const int ch = after.channels();
    const int maskStride = mask.bytesPerLine();"""

BUG5_NEW = """    // BREAK SELF-CHECK: the edit is not clipped to the selection, so
    // the gradient paints over the whole layer.
    out = after.clone();
    return true;

    const int W = after.cols;
    const int H = after.rows;
    const int ch = after.channels();
    const int maskStride = mask.bytesPerLine();"""


# ---------------------------------------------------------------------------
# Bug 6: the ellipse marquee fills its bounding box.
#
# The ellipse is drawn as a rectangle -- so the corner of the box comes
# back INSIDE the selection.
# ---------------------------------------------------------------------------
BUG6_OLD = """    QPainterPath path;
    path.addEllipse(r);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, m_antiAlias);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 255));
        painter.drawPath(path);
        painter.end();
    }"""

BUG6_NEW = """    // BREAK SELF-CHECK: the ellipse is drawn as a rectangle.
    QPainterPath path;
    path.addRect(r);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, m_antiAlias);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 255));
        painter.drawPath(path);
        painter.end();
    }"""


# ---------------------------------------------------------------------------
# Bug 7: the polygon marquee closes with two vertices.
#
# The vertex count check is dropped from close().
# ---------------------------------------------------------------------------
BUG7_OLD = """bool PolygonSelectionStrategy::close()
{
    if (m_path.size() < 3) return false;"""

BUG7_NEW = """bool PolygonSelectionStrategy::close()
{
    // BREAK SELF-CHECK: a two-vertex "polygon" is accepted.
    if (m_path.size() < 1) return false;"""


BUGS = [
    ("bug 1 (the rotation replaces the zoom)",
     VIEWTOOLS, BUG1_OLD, BUG1_NEW,
     "the_canvas_keeps_the_zoom_when_the_view_is_rotated"),
    ("bug 2 (the rotation pivots around the origin)",
     CANVAS, BUG2_OLD, BUG2_NEW,
     "the_rotation_pivots_around_the_centre"),
    ("bug 3 (the pattern stamp paints nothing)",
     PATTERN, BUG3_OLD, BUG3_NEW,
     "the_pattern_stamp_tiles_the_source_patch"),
    ("bug 4 (the history brush paints nothing)",
     HISTORY, BUG4_OLD, BUG4_NEW,
     "the_history_brush_paints_the_snapshot"),
    ("bug 5 (the selection does not clip the edit)",
     IMAGEWINDOW, BUG5_OLD, BUG5_NEW,
     "the_selection_clips_the_gradient"),
    ("bug 6 (the ellipse marquee fills its bounding box)",
     ELLIPSE, BUG6_OLD, BUG6_NEW,
     "the_ellipse_marquee_selects_an_ellipse"),
    ("bug 7 (the polygon marquee closes with two vertices)",
     POLYGON, BUG7_OLD, BUG7_NEW,
     "the_polygon_marquee_needs_three_vertices"),
]


def read(path):
    with io.open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write(path, text):
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def sha(path):
    with io.open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def inject(path, old, new):
    """Replace `old` with `new` in `path`, keeping the file's newline style."""
    src = read(path)
    nl = "\r\n" if "\r\n" in src else "\n"
    o = old.replace("\n", nl)
    n = new.replace("\n", nl)
    if o not in src:
        raise AssertionError("anchor not found in " + path + ": "
                             + old.splitlines()[0])
    write(path, src.replace(o, n, 1))


def build():
    r = subprocess.run(["cmd", "/c", "tools\\build.bat"], cwd=ROOT,
                       capture_output=True, text=True)
    out = (r.stdout or "") + (r.stderr or "")
    if "BUILD OK" not in out:
        print("[break] BUILD FAILED -- the injection does not compile")
        print(out[-3000:])
        return False
    print("[break] build ok")
    return True


def run_test(exe):
    env = dict(os.environ)
    env["PATH"] = QT_BIN + os.pathsep + env["PATH"]
    r = subprocess.run([exe], cwd=BUILD, capture_output=True, text=True, env=env)
    return (r.stdout or "") + (r.stderr or ""), r.returncode


def expect_red(label, expected_test, out, returncode):
    """The EXPECTED test has to go red.

    A crash counts as red: a Qt ASSERT aborts without printing a
    "FAIL!" line, so the exit code is the only signal. Both are
    checked, and the test NAME is verified -- "something went red" is
    not enough, the right thing has to go red.
    """
    print("[break] %s -- test output:" % label)
    for line in out.splitlines():
        if ("FAIL!" in line or "Totals:" in line or "Actual" in line
                or "Expected" in line or "ASSERT" in line):
            print("   " + line.strip())

    crashed = (returncode != 0) and ("Totals:" not in out)

    # The expected test has to appear on a FAIL line.
    #
    #   It is NOT enough that the name appears anywhere in the output:
    #   Qt Test prints the name on the PASS line too, so a plain
    #   "expected_test in out" matches a GREEN run. That is a false
    #   green in the gate itself -- exactly the kind of thing this
    #   project keeps running into.
    #
    #   So the name is looked for on the "FAIL!" lines only.
    failed_names = set()
    for line in out.splitlines():
        if "FAIL!" in line:
            failed_names.add(line)

    for line in failed_names:
        if expected_test in line:
            print("[break] OK: the expected test (%s) went red" % expected_test)
            return True

    if crashed:
        print("[break] OK: the test binary crashed (exit=%d) with the injected bug"
              % returncode)
        return True

    if failed_names:
        print("[break] FAIL: the tests went red, but NOT the expected one (%s)"
              % expected_test)
        for line in failed_names:
            print("   " + line.strip())
        return False

    print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
          "they do not detect it")
    return False


def main():
    if not os.path.exists(TOOLS_EXE):
        print("FAIL: the test binary does not exist: " + TOOLS_EXE)
        return 2

    # Back up every file we touch, so the restore is byte identical.
    touched = sorted(set(b[1] for b in BUGS))
    hash_before = {p: sha(p) for p in touched}
    backups = {}
    for p in touched:
        tf = tempfile.NamedTemporaryFile(delete=False, suffix=".bak")
        with io.open(p, "rb") as f:
            tf.write(f.read())
        tf.close()
        backups[p] = tf.name

    ok = True
    try:
        for label, path, old, new, expected_test in BUGS:
            inject(path, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                out, rc = run_test(TOOLS_EXE)
                if not expect_red(label, expected_test, out, rc):
                    ok = False
            # restore before the next injection
            shutil.copyfile(backups[path], path)

    finally:
        for p, b in backups.items():
            shutil.copyfile(b, p)
            os.unlink(b)
        for p in touched:
            same = sha(p) == hash_before[p]
            print("[restore] %s restored, byte identical: %s"
                  % (os.path.basename(p), same))
            if not same:
                return 2

    if not ok:
        return 1

    print("[break] rebuilding the restored tree")
    if not build():
        return 2
    out, _ = run_test(TOOLS_EXE)
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + os.path.basename(TOOLS_EXE) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
