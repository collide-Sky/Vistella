#!/usr/bin/env python3
"""break_test_contracts.py -- prove the contract gate can still go red.

A gate that never fails is worse than no gate: it gives false confidence.
This script injects one deliberate violation per rule into a real file, runs
`tools/check_contracts.py`, and requires it to report ERROR for that rule.
The original file is restored byte for byte afterwards.

Rules covered:
  R1  a new public method with no caller
  R2  a comment carrying a date stamp
  R3  a TODO marker pointing at an unregistered DEBT id
  R4  one more duplicate refresh call in an already duplicated file

Usage:  python tools/break_test_contracts.py
Exit:   0 = every rule still goes red, 1 = a rule no longer fires.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# (file, expected rule, anchor text, injected text)
CASES = [
    ("src/ui/dock/VDragManager.h",
     "R1",
     "public:",
     "public:\n    void brandNewUnusedApi(int x);"),
    ("src/media/brushes/BrushKind.h",
     "R2",
     "class BrushKind",
     "// P9.9 (2026-10-27): deliberate date stamp\nclass BrushKind"),
    ("src/media/imageworker/ImageProcessorAsync.h",
     "R3",
     "#include",
     "// TODO(P8.9): deliberate marker -- see docs/DEBT.md#A-DOES-NOT-EXIST\n#include"),
    ("src/media/tools/Brush.cpp",
     "R4",
     "    paintSingleStamp(host, scenePos, 1.0);",
     "    paintSingleStamp(host, scenePos, 1.0);\n    host->renderToViewPublic();"),
]


def run_gate() -> tuple[int, str]:
    r = subprocess.run([sys.executable, "tools/check_contracts.py"], cwd=REPO,
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=300)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def main() -> int:
    failures: list[str] = []
    for rel, rule, anchor, inject in CASES:
        p = REPO / rel
        if not p.is_file():
            failures.append(f"{rel}: file not found")
            continue
        backup = p.read_bytes()
        try:
            text = backup.decode("utf-8", errors="replace")
            if anchor not in text:
                failures.append(f"{rel}: anchor not found -> {anchor[:60]}")
                continue
            p.write_text(text.replace(anchor, inject, 1), encoding="utf-8", newline="")
            code, out = run_gate()
            if code == 0 or f"[ERROR]" not in out or rule not in out:
                failures.append(
                    f"{rel}: expected {rule} ERROR, got exit={code}; tail:\n{out[-600:]}")
            else:
                print(f"OK  {rule}: {rel} -> gate went red (exit={code})")
        finally:
            p.write_bytes(backup)

    code, out = run_gate()
    if code != 0:
        failures.append(f"after restore the gate still fails:\n{out[-600:]}")
    else:
        print("OK  tree restored, gate green again")

    for f in failures:
        print("  !", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
