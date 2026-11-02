#!/usr/bin/env python3
"""Build + test helper for the MultiDoc project.

    python tools/build.py              -> build only
    python tools/build.py --test       -> build + full ctest
    python tools/build.py --test --only 'tst_ToolState|tst_ImageProcessor'

Every source file is touched before the build, because ninja reports a
false green when an object file's timestamp did not change even though
the source was edited (the Edit tool and the patch scripts both write
the file with a preserved mtime).

Files that are locked (a running test binary holds them) are skipped
instead of aborting the run.
"""
import os
import re
import subprocess
import sys

ROOT = r"D:\Collide\MyCode\QT6Projects\MultiDoc"
BIN = os.path.join(ROOT, "build", "Desktop_Qt_6_11_1_MSVC2022_64bit_Debug")
VSWHERE = r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"

ERROR_RE = re.compile(r"(error C\d+|error LNK\d+|FAILED|ninja: build stopped)")


def touch_sources():
    now = None
    import time
    now = time.time()
    count = 0
    for sub in ("src", "tests"):
        base = os.path.join(ROOT, sub)
        for dirpath, _dirs, files in os.walk(base):
            for fn in files:
                if fn.endswith((".cpp", ".h", ".hpp")):
                    p = os.path.join(dirpath, fn)
                    try:
                        os.utime(p, (now, now))
                        count += 1
                    except OSError:
                        pass  # locked by a running binary -- skip it
    print("[touch] %d source files" % count)


def run(cmd_line, label):
    print("=== %s ===" % label, flush=True)
    p = subprocess.run(["cmd", "/c", cmd_line], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    out = (p.stdout or "") + (p.stderr or "")
    if label == "BUILD":
        lines = out.splitlines()
        hits = [l for l in lines if ERROR_RE.search(l)]
        for l in hits[:30]:
            print(l)
        if not hits:
            # The error text can be anywhere; show the tail so the failure
            # is never silent.
            print("--- no error line matched, tail of the output ---")
            for l in lines[-40:]:
                print(l)
        else:
            # Show the context of the first error too.
            first = next(i for i, l in enumerate(lines) if ERROR_RE.search(l))
            for l in lines[max(0, first - 2): first + 6]:
                if ERROR_RE.search(l) or ".cpp(" in l or ".h(" in l:
                    print(l[:400])
    else:
        print("\n".join(out.splitlines()[-25:]))
    print("=== %s EXIT: %d ===" % (label, p.returncode), flush=True)
    return p.returncode


def main():
    args = sys.argv[1:]
    do_test = "--test" in args
    only = ""
    if "--only" in args:
        i = args.index("--only")
        only = args[i + 1] if i + 1 < len(args) else ""

    if "--no-touch" not in args:
        touch_sources()

    # Re-run the CMake configure step first: adding a file to a
    # CMakeLists.txt needs a regeneration, and without it the build
    # fails with an empty output (ninja cannot make progress).
    if "--no-configure" not in args:
        cfg_cmd = "cd /d {b} && call {v} >nul 2>&1 && cmake . -DCMAKE_BUILD_TYPE=Debug".format(
            b=BIN, v=VSWHERE)
        p = subprocess.run(["cmd", "/c", cfg_cmd], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if p.returncode != 0:
            print((p.stdout or "") + (p.stderr or ""))
            print("=== CONFIGURE EXIT: %d ===" % p.returncode)
            return p.returncode
        print("[configure] ok", flush=True)

    build_cmd = "cd /d {b} && call {v} >nul 2>&1 && cmake --build . -- -k 0".format(b=BIN, v=VSWHERE)
    rc = run(build_cmd, "BUILD")
    if rc != 0:
        return rc
    if not do_test:
        return 0

    if only:
        # The pipe has to be escaped for cmd.
        pattern = only.replace("|", "^|")
        test_cmd = "cd /d {b} && call {v} >nul 2>&1 && ctest -R {p} --output-on-failure".format(b=BIN, v=VSWHERE, p=pattern)
    else:
        test_cmd = "cd /d {b} && call {v} >nul 2>&1 && ctest -j 4".format(b=BIN, v=VSWHERE)
    return run(test_cmd, "CTEST")


if __name__ == "__main__":
    sys.exit(main())
