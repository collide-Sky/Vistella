"""Break self-check for the M2-6a adjustment preview / cancel rollback.

Injects a deliberate bug into the preview implementation and verifies that
the regression tests go RED.  Then restores the file byte for byte.

Two bugs are injected, one after the other, because they cover the two
independent halves of the feature:

  1. "preview pushes an undo command"
     -- the preview path stops being preview-only and starts writing to the
        undo stack.  A slider drag would then flood the undo stack, and
        Cancel would leave entries behind.  Caught by
        repeatedPreview_neverTouchesTheUndoStack().

  2. "cancel does not roll back"
     -- AdjustDialogBase::reject() stops ending the preview session, so the
        canvas keeps the previewed adjustment after Cancel.  Caught by
        cancel_restoresExactPixelsAndLeavesNoResidue() and
        dialog_emitsSessionLifecycleAndCancelsRollBack().

The file is restored byte for byte afterwards (hash comparison is printed).
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

PANEL = os.path.join(ROOT, "src", "media", "imagewindow", "AdjustmentPanel.cpp")
BASE = os.path.join(ROOT, "src", "media", "dialogs", "AdjustDialogBase.cpp")
EXE = os.path.join(BUILD, "tst_M2_6_AdjustPreview.exe")


# ---------------------------------------------------------------------------
# Bug 1: the preview pushes an undo command.
#
# The preview writes the adjustment through applyTabLutInPreviewMode, which
# deliberately does NOT start the debounce timer and does NOT mark the tab
# as "pending" -- the preview pushes no undo command.
#
# The injection makes the preview mark the tab as pending, which is what
# the debounce commit looks at: 200ms later commitUndoDebounced() finds a
# pending tab and pushes a LayerCommand.  A slider drag would then fill
# the undo stack with one entry per drag -- exactly the bug the whole
# preview feature exists to avoid.
# ---------------------------------------------------------------------------
BUG1_OLD = """    // The canvas changes at once (that is what a preview is for)
    m_host->refreshCanvasNow();

    // Note: the commit timer is deliberately NOT started here -- the preview pushes no undo command.
}"""

BUG1_NEW = """    // The canvas changes at once (that is what a preview is for)
    m_host->refreshCanvasNow();

    // BREAK SELF-CHECK: the preview marks the tab as pending, so the
    // debounce commit pushes an undo command for every slider drag.
    m_pendingValid[tab] = true;
    if (m_commitTimer) m_commitTimer->start();
}"""


# ---------------------------------------------------------------------------
# Bug 2: Cancel does not roll back.
#
# The host listens to AdjustDialogBase::previewSessionEnded and calls
# AdjustmentPanel::cancelPreviewSession() when the dialog is rejected.
# Dropping that call means the canvas keeps the previewed adjustment
# after Cancel.
# ---------------------------------------------------------------------------
BUG2_OLD = """    // Cancel: first let the host restore the canvas to the state before the dialog opened (without leaving an undo entry),
    // and only then close the dialog.
    endPreviewSession(/*committed=*/false);
    QDialog::reject();"""

BUG2_NEW = """    // BREAK SELF-CHECK: Cancel does not end the preview session, so the
    // canvas keeps the previewed adjustment instead of rolling back.
    QDialog::reject();"""


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
        raise AssertionError("anchor not found in " + path + ": " + old.splitlines()[0])
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


def run_test():
    env = dict(os.environ)
    env["PATH"] = QT_BIN + os.pathsep + env["PATH"]
    r = subprocess.run([EXE], cwd=BUILD, capture_output=True, text=True, env=env)
    return (r.stdout or "") + (r.stderr or ""), r.returncode


def expect_red(label, out):
    red = ("FAIL!" in out) or ("Totals:" in out and " 0 failed" not in out)
    print("[break] %s -- test output:" % label)
    for line in out.splitlines():
        if "FAIL!" in line or "Totals:" in line or "Actual" in line or "Expected" in line:
            print("   " + line.strip())
    if not red:
        print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
              "they do not detect it")
        return False
    print("[break] OK: the regression tests went RED with the injected bug")
    return True


def main():
    if not os.path.exists(EXE):
        print("FAIL: the test binary does not exist: " + EXE)
        return 2

    panel_hash_before = sha(PANEL)
    base_hash_before = sha(BASE)

    backups = {}
    for path in (PANEL, BASE):
        backup = tempfile.NamedTemporaryFile(delete=False, suffix=".cpp")
        with io.open(path, "rb") as f:
            backup.write(f.read())
        backup.close()
        backups[path] = backup.name

    ok = True
    try:
        # ---- bug 1: the preview pushes an undo command ----
        inject(PANEL, BUG1_OLD, BUG1_NEW)
        print("[break] injected: the preview arms the debounce undo commit")
        if not build():
            ok = False
        else:
            out, _ = run_test()
            if not expect_red("bug 1 (preview pushes an undo command)", out):
                ok = False

        # restore the panel file before the second injection
        shutil.copyfile(backups[PANEL], PANEL)

        # ---- bug 2: Cancel does not roll back ----
        inject(BASE, BUG2_OLD, BUG2_NEW)
        print("[break] injected: Cancel does not roll back")
        if not build():
            ok = False
        else:
            out, _ = run_test()
            if not expect_red("bug 2 (cancel does not roll back)", out):
                ok = False

    finally:
        # ---- restore byte for byte ----
        for path, backup in backups.items():
            shutil.copyfile(backup, path)
            os.unlink(backup)

        panel_same = sha(PANEL) == panel_hash_before
        base_same = sha(BASE) == base_hash_before
        print("[restore] AdjustmentPanel.cpp restored, byte identical:", panel_same)
        print("[restore] AdjustDialogBase.cpp restored, byte identical:", base_same)
        if not (panel_same and base_same):
            return 2

    if not ok:
        return 1

    # The tree is back to its original state, so rebuild once more and make
    # sure the tests are green again.
    print("[break] rebuilding the restored tree")
    if not build():
        return 2
    out, _ = run_test()
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
