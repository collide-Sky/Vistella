"""Break self-check for the M2-6b adjustment transforms and their wiring.

Injects a deliberate bug and verifies that the regression tests go RED.
Then restores the file byte for byte.

Four bugs are injected, one after the other, because they cover the four
independent halves of the feature:

  1. "the hue shift is a no-op"
     -- the hue table stops shifting the hue.  Caught by
        hslHueShift_movesRedTowardsGreen() and
        hslHueShift_changesTheImage().

  2. "the channel mixer uses the wrong matrix order"
     -- the rows are read as blue output instead of red output.  Caught
        by channelMixer_pureRedOutputRow_zeroesGreenAndBlue().

  3. "the black & white mixers cannot change the brightness"
     -- the grey value is divided by the sum of the weights again, which
        makes a uniform change of all six mixers a no-op.  Caught by
        blackWhite_mixersChangeTheGreyValue().

  4. "the panel does not write the per-pixel payload"
     -- AdjustmentPanel stops writing the AdjustmentParams into the
        adjustment layer, so the per-pixel transforms become dead code
        in the product again.  Caught by the wiring test
        (hslDialog_writesThePerPixelPayload() and friends).

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

TRANSFORM = os.path.join(ROOT, "src", "media", "imageworker", "layers",
                         "AdjustmentTransform.cpp")
PANEL = os.path.join(ROOT, "src", "media", "imagewindow", "AdjustmentPanel.cpp")

TRANSFORM_EXE = os.path.join(BUILD, "tst_M2_6_AdjustTransform.exe")
WIRING_EXE = os.path.join(BUILD, "tst_M2_6_AdjustWiring.exe")


# ---------------------------------------------------------------------------
# Bug 1: the hue shift becomes a no-op.
# ---------------------------------------------------------------------------
BUG1_OLD = """        const int shifted = ((hue + units) % 180 + 180) % 180;
        table.at<uchar>(h) = static_cast<uchar>(shifted);"""

BUG1_NEW = """        // BREAK SELF-CHECK: the hue shift is dropped, so the table is
        // the identity and a hue shift changes nothing.
        const int shifted = hue;
        table.at<uchar>(h) = static_cast<uchar>(shifted);"""


# ---------------------------------------------------------------------------
# Bug 2: the channel mixer reads the matrix rows in the wrong order.
# ---------------------------------------------------------------------------
BUG2_OLD = """            const double or_ = m[0] * r + m[1] * g + m[2] * b;
            const double og  = m[3] * r + m[4] * g + m[5] * b;
            const double ob  = m[6] * r + m[7] * g + m[8] * b;"""

BUG2_NEW = """            // BREAK SELF-CHECK: the rows are read as blue / green / red
            // output instead of red / green / blue, so the matrix is
            // applied transposed.
            const double ob  = m[0] * r + m[1] * g + m[2] * b;
            const double og  = m[3] * r + m[4] * g + m[5] * b;
            const double or_ = m[6] * r + m[7] * g + m[8] * b;"""


# ---------------------------------------------------------------------------
# Bug 3: the black & white mixers cannot change the brightness.
# ---------------------------------------------------------------------------
BUG3_OLD = """    const double grey = (r * wR + g * wG + b * wB) / 300.0;"""

BUG3_NEW = """    // BREAK SELF-CHECK: the grey value is divided by the sum of the
    // actual weights again, which makes a uniform change of all six
    // mixers a no-op.
    const double grey = (r * wR + g * wG + b * wB) / (wR + wG + wB);"""


# ---------------------------------------------------------------------------
# Bug 4: the panel stops writing the per-pixel payload.
# ---------------------------------------------------------------------------
BUG4_OLD = """    stack->setAdjustmentParams(layerIdx, params);
    stack->setAdjustmentChannelLuts(layerIdx, channelLuts);"""

BUG4_NEW = """    // BREAK SELF-CHECK: the per-pixel payload is not written into the
    // adjustment layer, so HSL / black & white / channel mixer become
    // dead code in the product again.
    stack->setAdjustmentChannelLuts(layerIdx, channelLuts);"""


BUGS = [
    ("bug 1 (the hue shift is a no-op)", TRANSFORM, BUG1_OLD, BUG1_NEW,
     [TRANSFORM_EXE]),
    ("bug 2 (the channel mixer matrix is transposed)", TRANSFORM,
     BUG2_OLD, BUG2_NEW, [TRANSFORM_EXE]),
    ("bug 3 (the black & white mixers cannot change the brightness)",
     TRANSFORM, BUG3_OLD, BUG3_NEW, [TRANSFORM_EXE]),
    ("bug 4 (the panel does not write the per-pixel payload)", PANEL,
     BUG4_OLD, BUG4_NEW, [WIRING_EXE]),
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


def nl_of(text):
    return "\r\n" if "\r\n" in text else "\n"


def inject(path, old, new):
    """Replace `old` with `new` in `path`, keeping the file's newline style."""
    src = read(path)
    nl = nl_of(src)
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


def expect_red(label, out):
    red = ("FAIL!" in out) or ("Totals:" in out and " 0 failed" not in out)
    print("[break] %s -- test output:" % label)
    for line in out.splitlines():
        if ("FAIL!" in line or "Totals:" in line or "Actual" in line
                or "Expected" in line):
            print("   " + line.strip())
    if not red:
        print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
              "they do not detect it")
        return False
    print("[break] OK: the regression tests went RED with the injected bug")
    return True


def main():
    for exe in (TRANSFORM_EXE, WIRING_EXE):
        if not os.path.exists(exe):
            print("FAIL: the test binary does not exist: " + exe)
            return 2

    hashes_before = {p: sha(p) for p in (TRANSFORM, PANEL)}

    backups = {}
    for path in (TRANSFORM, PANEL):
        backup = tempfile.NamedTemporaryFile(delete=False, suffix=".cpp")
        with io.open(path, "rb") as f:
            backup.write(f.read())
        backup.close()
        backups[path] = backup.name

    ok = True
    try:
        for label, path, old, new, exes in BUGS:
            inject(path, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                red = False
                for exe in exes:
                    out, _ = run_test(exe)
                    if not expect_red(label + " [" + os.path.basename(exe) + "]",
                                      out):
                        red = False
                        break
                    red = True
                if not red:
                    ok = False
            # restore before the next injection
            shutil.copyfile(backups[path], path)

    finally:
        for path, backup in backups.items():
            shutil.copyfile(backup, path)
            os.unlink(backup)

        for path in (TRANSFORM, PANEL):
            same = sha(path) == hashes_before[path]
            print("[restore] %s restored, byte identical: %s"
                  % (os.path.basename(path), same))
            if not same:
                return 2

    if not ok:
        return 1

    # The tree is back to its original state, so rebuild once more and make
    # sure the tests are green again.
    print("[break] rebuilding the restored tree")
    if not build():
        return 2
    green = True
    for exe in (TRANSFORM_EXE, WIRING_EXE):
        out, _ = run_test(exe)
        g = "Totals:" in out and " 0 failed" in out
        for line in out.splitlines():
            if "Totals:" in line:
                print("   " + os.path.basename(exe) + ": " + line.strip())
        if not g:
            green = False
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
