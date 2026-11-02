"""Break self-check for the M1-7 undo-stack eviction.

Injects the OLD (broken) eviction logic -- the one that used
QUndoStack::setUndoLimit() on a non-empty stack, which Qt 6 refuses
to do -- and verifies that the regression tests go RED.

Then restores the file byte for byte.
"""
import io
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = r"D:\Collide\MyCode\QT6Projects\MultiDoc"
BUILD = os.path.join(ROOT, "build", "Desktop_Qt_6_11_1_MSVC2022_64bit_Debug")
TARGET = os.path.join(ROOT, "src", "media", "UndoStack.cpp")
QT_BIN = r"D:\QT6\6.11.1\msvc2022_64\bin"

# The broken version: the eviction lowers the undo limit, which Qt
# refuses to do on a non-empty stack -> the command is never removed.
BROKEN = r"""int UndoStack::evictUntilUnderBudget()
{
    if (m_budgetBytes == 0) return 0;

    int dropped = 0;

    // BREAK SELF-CHECK: the old implementation. Qt 6 refuses
    // setUndoLimit() on a non-empty stack, so nothing is ever removed.
    while (m_usedBytes > m_budgetBytes && count() > 0) {
        const size_t oldest = footprintOf(command(0));
        const int limit = undoLimit();
        setUndoLimit(limit > 0 ? limit - 1 : 0);
        m_usedBytes = (m_usedBytes >= oldest) ? (m_usedBytes - oldest) : 0;
        ++dropped;
    }

    if (dropped > 0) {
        m_evictedCount += dropped;
        emit indexShifted(dropped);
    }
    return dropped;
}
"""

ANCHOR_START = "int UndoStack::evictUntilUnderBudget()\n{"
ANCHOR_END = "\nint UndoStack::evictOldest(int keepCount)\n"


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def main():
    original = io.open(TARGET, encoding="utf-8").read()

    start = original.find(ANCHOR_START)
    end = original.find(ANCHOR_END)
    if start < 0 or end < 0:
        print("FAIL: could not locate evictUntilUnderBudget in UndoStack.cpp")
        return 2

    broken = original[:start] + BROKEN + original[end:]

    # Back up the original so the restore is byte exact.
    backup = tempfile.NamedTemporaryFile(delete=False, suffix=".cpp")
    backup.write(original.encode("utf-8"))
    backup.close()

    try:
        io.open(TARGET, "w", encoding="utf-8", newline="").write(broken)
        print("[break] injected the old eviction logic")

        r = run(["cmd", "/c", "tools\\build.bat"], cwd=ROOT)
        if "BUILD OK" not in (r.stdout or "") + (r.stderr or ""):
            print("[break] BUILD FAILED -- the injection does not compile")
            print((r.stdout or "")[-2000:])
            print((r.stderr or "")[-2000:])
            return 2
        print("[break] build ok")

        env = dict(os.environ)
        env["PATH"] = QT_BIN + os.pathsep + env["PATH"]
        exe = os.path.join(BUILD, "tst_M1_4_7_8_Save.exe")
        r = run([exe], cwd=BUILD, )
        out = (r.stdout or "") + (r.stderr or "")
        # The env has to be passed through run(); redo it explicitly.
        r2 = subprocess.run([exe], cwd=BUILD, capture_output=True, text=True, env=env)
        out = (r2.stdout or "") + (r2.stderr or "")

        red = "FAIL!" in out
        print("[break] test output:")
        for line in out.splitlines():
            if "FAIL!" in line or "Totals:" in line or "Actual" in line or "Expected" in line:
                print("   " + line.strip())

        if not red:
            print("[break] FAIL: the tests stayed GREEN with the old code -- "
                  "they do not detect the bug")
            return 1

        print("[break] OK: the regression tests went RED with the old code")
        return 0
    finally:
        # Restore byte for byte.
        shutil.copyfile(backup.name, TARGET)
        os.unlink(backup.name)
        with io.open(TARGET, "rb") as f:
            restored = f.read()
        original_bytes = original.encode("utf-8")
        same = restored == original_bytes
        print("[restore] UndoStack.cpp restored, byte identical:", same)
        if not same:
            return 2


if __name__ == "__main__":
    sys.exit(main())
