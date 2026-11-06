"""Break self-check for the M2-2 panel system (native QDockWidget + saveState).

Injects a deliberate bug and verifies that the regression test goes RED.
Then restores the file byte for byte.

Five bugs are injected, one after the other, because they cover the
independent halves of the change:

  1. "the panel group grows to the tallest page again"
     -- the tab widget's sizeHint() is no longer overridden, so it
        returns the max over ALL pages instead of the current one. The
        property group then claims most of the right panel even while
        the short PropertiesDock page is showing.
        Caught by contentSizedSplit_layerGroupGetsTheRest().

  2. "the layer group can no longer grow"
     -- its size policy goes back to Maximum, which caps it at its
        sizeHint(). The stretch factor alone cannot hand it more space.
        Caught by contentSizedSplit_layerGroupGetsTheRest().

  3. "the layout is never saved"
     -- save() returns early without writing the file, so the
        arrangement is lost on every exit.
        Caught by save_writesAFile() and savedFile_isNotEmpty().

  4. "reset goes back to the live layout"
     -- reset() applies the live layout file instead of the default
        one. This is the distinction that makes the two files
        necessary in the first place.
        Caught by reset_doesNotGoBackToTheLiveLayout() and
        saveAsDefault_thenReset_goesBackToTheDefault().

  5. "the default layout is the live layout"
     -- saveAsDefault() writes to the live layout file, so declaring a
        default silently overwrites the session layout.
        Caught by reset_doesNotGoBackToTheLiveLayout().

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

DOCK_CPP = os.path.join(ROOT, "src", "media", "docks", "RightPanelDock.cpp")
LAYOUT_CPP = os.path.join(ROOT, "src", "app", "DockLayoutManager.cpp")

DOCK_EXE = os.path.join(BUILD, "tst_RightPanelDock.exe")
LAYOUT_EXE = os.path.join(BUILD, "tst_M2_2_DockLayout.exe")


# ---------------------------------------------------------------------------
# Bug 1: the group size hint follows the tallest page again.
# ---------------------------------------------------------------------------
BUG1_OLD = """    QSize sizeHint() const override
    {
        QSize s = tabBar()->sizeHint();
        if (QWidget* page = currentWidget())
            s = s.expandedTo(page->sizeHint());
        return s;
    }"""

BUG1_NEW = """    // BREAK SELF-CHECK: the size hint is the max over ALL pages again,
    // so the property group claims most of the panel even while the
    // short PropertiesDock page is showing.
    QSize sizeHint() const override
    {
        return QTabWidget::sizeHint();
    }"""


# ---------------------------------------------------------------------------
# Bug 2: the layer group can no longer grow.
# ---------------------------------------------------------------------------
BUG2_OLD = """    tabs->setSizePolicy(QSizePolicy::Preferred,
                        expanding ? QSizePolicy::Expanding : QSizePolicy::Maximum);"""

BUG2_NEW = """    // BREAK SELF-CHECK: every group is capped at its size hint, so the
    // stretch factor cannot hand the layer group the leftover height.
    tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    Q_UNUSED(expanding);"""


# ---------------------------------------------------------------------------
# Bug 3: the layout is never written.
# ---------------------------------------------------------------------------
BUG3_OLD = """    if (!writeFile(currentLayoutPath(m_dir), payload)) {
        m_lastError = QStringLiteral("cannot write ") + currentLayoutPath(m_dir);
        return false;
    }
    return true;
}"""

BUG3_NEW = """    // BREAK SELF-CHECK: the layout is never written, so the arrangement
    // is lost on every exit.
    m_lastError = QStringLiteral("BREAK SELF-CHECK: nothing was written");
    return false;
}"""


# ---------------------------------------------------------------------------
# Bug 4: reset applies the LIVE layout instead of the default.
# ---------------------------------------------------------------------------
BUG4_OLD = """    bool ok = false;
    const QByteArray payload = readFile(defaultLayoutPath(m_dir), &ok);
    if (!ok) {
        // No default saved yet: the built-in arrangement is the default.
        // Nothing to restore, and the window already IS in its built-in
        // arrangement, so this is a success.
        m_lastError.clear();
        return true;
    }"""

BUG4_NEW = """    // BREAK SELF-CHECK: reset applies the LIVE layout, so it can never
    // go back to the arrangement the user declared as the default.
    bool ok = false;
    const QByteArray payload = readFile(currentLayoutPath(m_dir), &ok);
    if (!ok) {
        m_lastError.clear();
        return true;
    }"""


# ---------------------------------------------------------------------------
# Bug 5: saveAsDefault overwrites the live layout.
# ---------------------------------------------------------------------------
BUG5_OLD = """    if (!writeFile(defaultLayoutPath(m_dir), payload)) {
        m_lastError = QStringLiteral("cannot write ") + defaultLayoutPath(m_dir);
        return false;
    }
    return true;
}"""

BUG5_NEW = """    // BREAK SELF-CHECK: declaring a default silently overwrites the
    // live session layout.
    if (!writeFile(currentLayoutPath(m_dir), payload)) {
        m_lastError = QStringLiteral("cannot write ") + currentLayoutPath(m_dir);
        return false;
    }
    return true;
}"""


BUGS = [
    ("bug 1 (the panel group grows to the tallest page again)",
     DOCK_CPP, BUG1_OLD, BUG1_NEW, DOCK_EXE),
    ("bug 2 (the layer group can no longer grow)",
     DOCK_CPP, BUG2_OLD, BUG2_NEW, DOCK_EXE),
    ("bug 3 (the layout is never saved)",
     LAYOUT_CPP, BUG3_OLD, BUG3_NEW, LAYOUT_EXE),
    ("bug 4 (reset goes back to the live layout)",
     LAYOUT_CPP, BUG4_OLD, BUG4_NEW, LAYOUT_EXE),
    ("bug 5 (the default layout is the live layout)",
     LAYOUT_CPP, BUG5_OLD, BUG5_NEW, LAYOUT_EXE),
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
                or "Expected" in line or "ASSERT" in line):
            print("   " + line.strip())
    if not red:
        print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
              "they do not detect it")
        return False
    print("[break] OK: the regression tests went RED with the injected bug")
    return True


def main():
    for exe in (DOCK_EXE, LAYOUT_EXE):
        if not os.path.exists(exe):
            print("FAIL: the test binary does not exist: " + exe)
            return 2

    touched = (DOCK_CPP, LAYOUT_CPP)
    hashes_before = {p: sha(p) for p in touched}

    backups = {}
    for path in touched:
        backup = tempfile.NamedTemporaryFile(delete=False, suffix=".cpp")
        with io.open(path, "rb") as f:
            backup.write(f.read())
        backup.close()
        backups[path] = backup.name

    ok = True
    try:
        for label, path, old, new, exe in BUGS:
            inject(path, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                out, _ = run_test(exe)
                if not expect_red(label, out):
                    ok = False
            # restore before the next injection
            shutil.copyfile(backups[path], path)

    finally:
        for path, backup in backups.items():
            shutil.copyfile(backup, path)
            os.unlink(backup)

        for path in touched:
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
    for exe in (DOCK_EXE, LAYOUT_EXE):
        out, _ = run_test(exe)
        ok_here = "Totals:" in out and " 0 failed" in out
        green = green and ok_here
        for line in out.splitlines():
            if "Totals:" in line:
                print("   " + os.path.basename(exe) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
