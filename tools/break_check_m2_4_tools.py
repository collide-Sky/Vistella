"""Break self-check for the M2-4 tools (move tool, keyboard chain, geometry).

Injects a deliberate bug and verifies that the regression test goes RED.
Then restores the file byte for byte.

Every injection targets the CAUSE of an effect the test asserts on -- not
something that merely looks like it:

  bug 1 (the layer pixels are shifted the wrong way)
     The source/destination rects of the shift are swapped, so the pixels
     land at the mirrored position. The test probes "the pixel that moved
     from (20,20) arrived at (30,25)" -- with the swap it arrives at the
     wrong place, so the probe goes red.

     This is the probe-pair rule: the test asserts BOTH that the dragged
     pixel arrived AND that the vacated slot does NOT still hold it. A
     no-op implementation fails the second half.

  bug 2 (the drag is not undoable)
     The before/after pair is no longer pushed into the undo stack, so
     the undo/redo assertions fail.

  bug 3 (the move tool no longer claims the mouse)
     wantsExclusiveMouse() returns false. Without it the canvas pans
     instead of moving the content, which is exactly the defect the
     exclusivity hook exists for. The claim assertions fail.

  bug 4 (the scene position is not clamped into the canvas)
     ToolGeometry::clampToCanvas returns the position unchanged. The
     clamp assertions fail.

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

MOVE = os.path.join(ROOT, "src", "media", "tools", "MoveTool.cpp")
MOVE_H = os.path.join(ROOT, "src", "media", "tools", "MoveTool.h")
GEOM = os.path.join(ROOT, "src", "media", "tools", "ToolGeometry.cpp")
TOOLS_EXE = os.path.join(BUILD, "tst_M2_4_Tools.exe")


# ---------------------------------------------------------------------------
# Bug 1: the layer pixels are shifted the wrong way.
#
# The source rect and the destination rect are swapped: the pixels land
# at the mirrored position instead of the dragged one.
# ---------------------------------------------------------------------------
BUG1_OLD = """    const int srcX0 = destX0 - dx;
    const int srcY0 = destY0 - dy;
    const int rw = destX1 - destX0;
    const int rh = destY1 - destY0;"""

BUG1_NEW = """    // BREAK SELF-CHECK: the source and the destination are swapped, so
    // the pixels land at the mirrored position.
    const int srcX0 = destX0;
    const int srcY0 = destY0;
    const int rw = destX1 - destX0;
    const int rh = destY1 - destY0;"""


# ---------------------------------------------------------------------------
# Bug 2: the drag is not undoable.
#
# The before/after pair is no longer pushed into the undo stack.
# ---------------------------------------------------------------------------
BUG2_OLD = """            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Move layer"));"""

BUG2_NEW = """            // BREAK SELF-CHECK: the edit is not pushed into the undo
            // stack, so the move cannot be undone.
            Q_UNUSED(cur);"""


# ---------------------------------------------------------------------------
# Bug 3: the move tool no longer claims the mouse.
#
# The canvas would scroll instead of moving the content.
# ---------------------------------------------------------------------------
BUG3_OLD = """    bool wantsExclusiveMouse() const override { return true; }"""

BUG3_NEW = """    // BREAK SELF-CHECK: the tool does not claim the mouse, so the
    // canvas pans instead of moving the content.
    bool wantsExclusiveMouse() const override { return false; }"""


# ---------------------------------------------------------------------------
# Bug 4: the scene position is not clamped into the canvas.
# ---------------------------------------------------------------------------
BUG4_OLD = """    const double maxX = std::max(0.0, double(sz.width() - 1));
    const double maxY = std::max(0.0, double(sz.height() - 1));
    return QPointF(std::clamp(scenePos.x(), 0.0, maxX),
                   std::clamp(scenePos.y(), 0.0, maxY));"""

BUG4_NEW = """    // BREAK SELF-CHECK: the position is not clamped into the canvas.
    return scenePos;"""


BUGS = [
    ("bug 1 (the layer pixels are shifted the wrong way)",
     MOVE, BUG1_OLD, BUG1_NEW),
    ("bug 2 (the drag is not undoable)",
     MOVE, BUG2_OLD, BUG2_NEW),
    ("bug 3 (the move tool does not claim the mouse)",
     MOVE_H, BUG3_OLD, BUG3_NEW),
    ("bug 4 (the scene position is not clamped into the canvas)",
     GEOM, BUG4_OLD, BUG4_NEW),
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
    return (r.stdout or "") + (r.stderr or "")


def expect_red(label, out):
    red = ("FAIL!" in out) or ("Totals:" in out and " 0 failed" not in out)
    print("[break] %s -- test output:" % label)
    for line in out.splitlines():
        if ("FAIL!" in line or "Totals:" in line or "Actual" in line
                or "Expected" in line or "ASSERT" in line):
            print("   " + line.strip())
    if not red:
        print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
              "they do not detect it")
        return False
    print("[break] OK: the regression tests went RED with the injected bug")
    return True


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
        for label, path, old, new in BUGS:
            inject(path, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                out = run_test(TOOLS_EXE)
                if not expect_red(label, out):
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
    out = run_test(TOOLS_EXE)
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + os.path.basename(TOOLS_EXE) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
