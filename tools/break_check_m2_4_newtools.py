"""Break self-check for the M2-4 tools that were missing.

Injects a deliberate bug and verifies that the regression test goes RED.
Then restores the file byte for byte.

Every injection targets the CAUSE of an effect the test asserts on, and
every injection names the test that MUST go red. Checking the NAME (not
just "something went red") is what makes the gate trustworthy: a
different test failing means the injection did not reach the code path
under test, and the gate would silently accept a broken test.

  bug 1 (the eraser paints the background colour instead of clearing
         the alpha)
     On a 4-channel layer the eraser must set the alpha to 0. The
     injection paints the pixels opaque instead.
     -> eraser_makes_the_pixels_transparent

  bug 2 (the eraser stroke is not undoable)
     The before/after pair is not pushed into the undo stack.
     -> eraser_stroke_is_undoable

  bug 3 (the paint bucket ignores the tolerance)
     Every pixel is filled, no matter how different its colour is.
     -> paint_bucket_fills_the_contiguous_region

  bug 4 (the gradient ignores the drag direction)
     The gradient factor is always 0, so the whole layer is filled
     with the foreground colour.
     -> gradient_runs_from_the_foreground_to_the_background

  bug 5 (the burn lightens instead of darkening)
     The burn calls the dodge.
     -> dodge_lightens_and_burn_darkens

  bug 6 (the blur does not blur)
     The blurred result is the original.
     -> blur_softens_and_sharpen_crisps

  bug 7 (a new tool has no instance)
     The factory returns nullptr for the eraser.
     -> every_new_tool_has_a_real_instance

The files are restored byte for byte afterwards (hash comparison is
printed).
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

TOOLS_DIR = os.path.join(ROOT, "src", "media", "tools")
TOOLS_EXE = os.path.join(BUILD, "tst_M2_4_Tools.exe")

ERASER = os.path.join(TOOLS_DIR, "EraserTool.cpp")
BUCKET = os.path.join(TOOLS_DIR, "PaintBucketTool.cpp")
GRADIENT = os.path.join(TOOLS_DIR, "GradientTool.cpp")
DODGE = os.path.join(TOOLS_DIR, "DodgeBurnTool.cpp")
BLUR = os.path.join(TOOLS_DIR, "BlurSharpenSmudgeTool.cpp")
FACTORY = os.path.join(TOOLS_DIR, "ToolStateFactory.cpp")


# ---------------------------------------------------------------------------
# Bug 1: the eraser paints the background colour instead of clearing
#        the alpha.
# ---------------------------------------------------------------------------
BUG1_OLD = """    if (target->channels() == 4) {
        // The layer has an alpha channel: erase means "make it
        // transparent".
        cv::Mat roiView = (*target)(roi);
        for (int y = 0; y < roiView.rows; ++y) {
            uchar* px = roiView.ptr<uchar>(y);
            for (int x = 0; x < roiView.cols; ++x) {
                px[x * 4 + 3] = 0;   // alpha = 0
            }
        }
    } else {"""

BUG1_NEW = """    if (target->channels() == 4) {
        // BREAK SELF-CHECK: the eraser paints opaque black instead of
        // clearing the alpha.
        cv::Mat roiView = (*target)(roi);
        for (int y = 0; y < roiView.rows; ++y) {
            uchar* px = roiView.ptr<uchar>(y);
            for (int x = 0; x < roiView.cols; ++x) {
                px[x * 4 + 0] = 0;
                px[x * 4 + 1] = 0;
                px[x * 4 + 2] = 0;
                px[x * 4 + 3] = 255;
            }
        }
    } else {"""


# ---------------------------------------------------------------------------
# Bug 2: the eraser stroke is not undoable.
# ---------------------------------------------------------------------------
BUG2_OLD = """            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Erase"));"""

BUG2_NEW = """            // BREAK SELF-CHECK: the erase is not pushed into the undo
            // stack, so it cannot be undone.
            Q_UNUSED(cur);"""


# ---------------------------------------------------------------------------
# Bug 3: the paint bucket ignores the tolerance.
# ---------------------------------------------------------------------------
BUG3_OLD = """            if (db > tolerance || dg > tolerance || dr > tolerance) continue;"""

BUG3_NEW = """            // BREAK SELF-CHECK: the tolerance is ignored, so the whole
            // layer is filled.
            Q_UNUSED(db); Q_UNUSED(dg); Q_UNUSED(dr); Q_UNUSED(tolerance);"""


# ---------------------------------------------------------------------------
# Bug 4: the gradient ignores the drag direction.
# ---------------------------------------------------------------------------
BUG4_OLD = """    // Linear: the projection of (p - start) onto the drag direction.
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-6) return 0.0;
    const double t = ((p.x() - start.x()) * dx + (p.y() - start.y()) * dy) / len2;
    return std::clamp(t, 0.0, 1.0);"""

BUG4_NEW = """    // BREAK SELF-CHECK: the gradient factor is always 0, so the whole
    // layer is filled with the foreground colour.
    Q_UNUSED(p); Q_UNUSED(start); Q_UNUSED(end);
    return 0.0;"""


# ---------------------------------------------------------------------------
# Bug 5: the burn lightens instead of darkening.
# ---------------------------------------------------------------------------
BUG5_OLD = """                p[c] = (m_mode == Mode::Dodge) ? dodgeChannel(p[c], m_amount)
                                               : burnChannel(p[c], m_amount);"""

BUG5_NEW = """                // BREAK SELF-CHECK: the burn does the same as the
                // dodge, so the pixels get lighter instead of darker.
                p[c] = dodgeChannel(p[c], m_amount);"""


# ---------------------------------------------------------------------------
# Bug 6: the blur does not blur.
# ---------------------------------------------------------------------------
BUG6_OLD = """    cv::Mat blurred;
    cv::GaussianBlur(colour, blurred, cv::Size(ksize, ksize), 0);"""

BUG6_NEW = """    cv::Mat blurred;
    // BREAK SELF-CHECK: the blur does not blur -- the original comes
    // back unchanged.
    blurred = colour.clone();"""


# ---------------------------------------------------------------------------
# Bug 7: a new tool has no instance.
# ---------------------------------------------------------------------------
BUG7_OLD = """    case mediators::ToolId::Eraser:     return new EraserTool();"""

BUG7_NEW = """    // BREAK SELF-CHECK: the eraser has no instance, so the button does
    // nothing.
    case mediators::ToolId::Eraser:     return nullptr;"""


# (label, file, old, new, the test that MUST go red)
BUGS = [
    ("bug 1 (the eraser paints instead of clearing the alpha)",
     ERASER, BUG1_OLD, BUG1_NEW, "eraser_makes_the_pixels_transparent"),
    ("bug 2 (the eraser stroke is not undoable)",
     ERASER, BUG2_OLD, BUG2_NEW, "eraser_stroke_is_undoable"),
    ("bug 3 (the paint bucket ignores the tolerance)",
     BUCKET, BUG3_OLD, BUG3_NEW, "paint_bucket_fills_the_contiguous_region"),
    ("bug 4 (the gradient ignores the drag direction)",
     GRADIENT, BUG4_OLD, BUG4_NEW, "gradient_runs_from_the_foreground_to_the_background"),
    ("bug 5 (the burn lightens instead of darkening)",
     DODGE, BUG5_OLD, BUG5_NEW, "dodge_lightens_and_burn_darkens"),
    ("bug 6 (the blur does not blur)",
     BLUR, BUG6_OLD, BUG6_NEW, "blur_softens_and_sharpen_crisps"),
    ("bug 7 (the eraser has no instance)",
     FACTORY, BUG7_OLD, BUG7_NEW, "every_new_tool_has_a_real_instance"),
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
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    if "BUILD OK" not in out:
        print("[break] BUILD FAILED -- the injection does not compile")
        print(out[-4000:])
        return False
    print("[break] build ok")
    return True


def run_test():
    env = dict(os.environ)
    env["PATH"] = QT_BIN + os.pathsep + env["PATH"]
    r = subprocess.run([TOOLS_EXE], cwd=BUILD, capture_output=True, text=True,
                       env=env, encoding="utf-8", errors="replace")
    return (r.stdout or "") + (r.stderr or "")


def failing_tests(out):
    """The names of the tests that failed."""
    names = []
    for line in out.splitlines():
        if "FAIL!" in line:
            try:
                names.append(line.split("tst_M2_4_Tools::")[1].split("(")[0])
            except IndexError:
                names.append(line.strip())
    return names


def expect_red(label, expected_test, out):
    failed = failing_tests(out)
    print("[break] %s -- the tests that failed: %s" % (label, failed or "NONE"))
    for line in out.splitlines():
        if ("FAIL!" in line or "Totals:" in line or "Actual" in line
                or "Expected" in line or "ASSERT" in line):
            print("   " + line.strip())

    if not failed:
        print("[break] FAIL: no test went red -- the tests do not detect it")
        return False
    if expected_test not in failed:
        print("[break] FAIL: the WRONG test went red. Expected %s, got %s"
              % (expected_test, failed))
        return False
    print("[break] OK: the expected test (%s) went red" % expected_test)
    return True


def main():
    if not os.path.exists(TOOLS_EXE):
        print("FAIL: the test binary does not exist: " + TOOLS_EXE)
        return 2

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
                out = run_test()
                if not expect_red(label, expected_test, out):
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
    out = run_test()
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + os.path.basename(TOOLS_EXE) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
