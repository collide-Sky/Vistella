"""Run every break self-check in sequence and report a summary.

The break checks must NOT run in parallel: each of them injects a bug
into a source file and rebuilds, so two of them at once would fight
over the same build directory (and, for two checks that touch the same
file, over the same source file -- that is a PermissionError, not a
test failure).

Usage:
    python tools/run_all_break_checks.py
"""
import os
import subprocess
import sys

ROOT = r"D:\Collide\MyCode\QT6Projects\MultiDoc"
PY = sys.executable

CHECKS = [
    "break_test_contracts.py",
    "break_check_m1_7_evict.py",
    "break_check_m2_6_preview.py",
    "break_check_m2_6_transform.py",
    "break_check_m2_7_filetree.py",
    "break_check_m2_2_docklayout.py",
    "break_check_m2_4_vectorlayer.py",
    "break_check_m2_4_tools.py",
    "break_check_m2_4_newtools.py",
    "break_check_m2_4_tail.py",
    "break_check_m2_1_project.py",
]


def main():
    results = []
    log_dir = os.path.join(ROOT, "build")
    for name in CHECKS:
        path = os.path.join(ROOT, "tools", name)
        if not os.path.exists(path):
            results.append((name, None, "MISSING"))
            print("=" * 70)
            print("[all] %s -- MISSING" % name)
            continue

        print("=" * 70)
        print("[all] running %s ..." % name)
        r = subprocess.run([PY, path], cwd=ROOT, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        out = (r.stdout or "") + (r.stderr or "")

        # Save the full output -- when a check fails only in the
        # sequence, the summary line is not enough to tell why.
        log_path = os.path.join(log_dir, "break_%s.log" % name.replace(".py", ""))
        try:
            with open(log_path, "w", encoding="utf-8") as f:
                f.write(out)
        except OSError:
            pass

        # Show the interesting lines only.
        for line in out.splitlines():
            if ("OK:" in line or "FAIL" in line or "restored" in line
                    or "green" in line or "RED" in line
                    or "byte identical" in line):
                print("   " + line.strip())

        results.append((name, r.returncode, out))
        print("[all] %s -> exit=%d (log: %s)"
              % (name, r.returncode, log_path))

    print("=" * 70)
    print("[all] ===== SUMMARY =====")
    failed = 0
    for name, code, out in results:
        if code is None:
            print("   %-38s MISSING" % name)
            failed += 1
            continue
        status = "OK" if code == 0 else "FAILED"
        if code != 0:
            failed += 1
        print("   %-38s %s (exit=%d)" % (name, status, code))

    print("[all] %d/%d break checks passed" % (len(CHECKS) - failed, len(CHECKS)))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
