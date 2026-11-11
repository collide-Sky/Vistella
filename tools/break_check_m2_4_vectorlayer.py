"""Break self-check for the M2-4 vector layer rendering (pixel coordinates).

Injects a deliberate bug and verifies that the regression test goes RED.
Then restores the file byte for byte.

The bug: rasterize() scales the vector paths by canvasSize/100 on the
assumption that they live in a normalised 0..100 space, while the tools
(ShapeTool / PenTool) write IMAGE PIXEL coordinates. A rectangle from
(100,100) to (200,200) on an 800x600 canvas was therefore rendered at
(800,600)-(1600,1200) -- far off the canvas, so the user saw nothing.

The bug is injected in two halves, because they are two independent
defects that happened to live in the same three lines:

  1. "the paths are scaled out of a normalised space"
     -- the p.scale(canvasSize/100) comes back.

  2. "the alpha is dropped"
     -- the result is converted to 3-channel BGR, so the transparent
        area of the shape becomes opaque black (LYR-01).

Why the probe points in the test have to be chosen carefully:
with the old scale factor (64/100 = 0.64 on a 64px canvas) the rect
(20,20)-(50,50) lands at (12.8,12.8)-(32,32) -- which still covers the
middle of the rectangle. A probe in the middle therefore passes with the
bug in place. The test probes (45,45) -- red only under the correct
rendering -- and (15,15) -- red only under the buggy one.

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

STACK = os.path.join(ROOT, "src", "media", "imageworker", "layers", "LayerStack.cpp")
KIND_EXE = os.path.join(BUILD, "tst_LayerKindRender.exe")

# ---------------------------------------------------------------------------
# Bug 1: the paths are scaled out of a normalised space again.
# ---------------------------------------------------------------------------
BUG1_OLD = """            p.drawPath(l->vectorPaths[i]);
        }
        p.end();
        // QImage ARGB32 -> cv::Mat BGRA.
        //   It must NOT be converted to BGR -- dropping the alpha turns
        //   the transparent area into opaque black, so the shape covers
        //   the whole canvas with a black rectangle (LYR-01).
        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        return mat.clone();"""

BUG1_NEW = """            // BREAK SELF-CHECK: the old normalised-space scaling.
            p.save();
            p.scale(sz.width / 100.0, sz.height / 100.0);
            p.drawPath(l->vectorPaths[i]);
            p.restore();
        }
        p.end();
        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        return mat.clone();"""


# ---------------------------------------------------------------------------
# Bug 2: the alpha is dropped again.
# ---------------------------------------------------------------------------
BUG2_OLD = """        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        return mat.clone();
    }
    case Layer::SmartObject: {"""

BUG2_NEW = """        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        // BREAK SELF-CHECK: the alpha is dropped, so the transparent
        // area of the shape turns into opaque black (LYR-01).
        cv::Mat bgra = mat.clone();
        cv::Mat bgr;
        cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case Layer::SmartObject: {"""


BUGS = [
    ("bug 1 (the vector paths are scaled out of a normalised space)",
     BUG1_OLD, BUG1_NEW),
    ("bug 2 (the vector layer drops its alpha)", BUG2_OLD, BUG2_NEW),
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
    if not os.path.exists(KIND_EXE):
        print("FAIL: the test binary does not exist: " + KIND_EXE)
        return 2

    hash_before = sha(STACK)
    backup = tempfile.NamedTemporaryFile(delete=False, suffix=".cpp")
    with io.open(STACK, "rb") as f:
        backup.write(f.read())
    backup.close()

    ok = True
    try:
        for label, old, new in BUGS:
            inject(STACK, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                out = run_test(KIND_EXE)
                if not expect_red(label, out):
                    ok = False
            # restore before the next injection
            shutil.copyfile(backup.name, STACK)

    finally:
        shutil.copyfile(backup.name, STACK)
        os.unlink(backup.name)
        same = sha(STACK) == hash_before
        print("[restore] %s restored, byte identical: %s"
              % (os.path.basename(STACK), same))
        if not same:
            return 2

    if not ok:
        return 1

    print("[break] rebuilding the restored tree")
    if not build():
        return 2
    out = run_test(KIND_EXE)
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + os.path.basename(KIND_EXE) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
