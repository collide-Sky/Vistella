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
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# 统计逻辑必须跟门禁**同一套**, 两套规则会悄悄漂移。
#   曾经把 R4 目标写死成 src/media/tools/Brush.cpp, M1-1 把该文件收敛成
#   refreshCanvasNow() 单一入口 (DEBT A-REFRESHDUP2 -> done) 之后, 注入的
#   1 次够不到 R4 的 "同一文件 >= 2 次" 阈值 —— R4 覆盖静默失效, 而脚本
#   只说 "expected R4 ERROR", 看不出真正原因是目标已不存在重复。
sys.path.insert(0, str(REPO / "tools"))
from check_contracts import (  # noqa: E402
    REFRESH_CALLS,
    SOURCE_SUFFIXES,
    strip_code_and_comments,
)

# R4 只在"同一文件内同一个刷新调用出现 >= 2 次"时才报告
R4_MIN_OCCURRENCES = 2

# (file, expected rule, anchor text, injected text)
CASES = [
    ("src/media/docks/ColorTab.h",
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
]


def read_raw(path: pathlib.Path) -> str:
    """按原始字节解码, **不做**通用换行转换。

    Path.read_text() 会把 \\r\\n 统一成 \\n, 跟按字节还原出来的原文对不上 ——
    锚点因此会"明明找到却匹配不上"。
    """
    return path.read_bytes().decode("utf-8", errors="replace")


def write_raw(path: pathlib.Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8"))


def run_gate() -> tuple[int, str]:
    r = subprocess.run([sys.executable, "tools/check_contracts.py"], cwd=REPO,
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=300)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def refresh_counts(text: str) -> dict[str, int]:
    """与门禁 R4 同一套统计: 去掉注释后数刷新调用次数。"""
    code, _ = strip_code_and_comments(text)
    counts: dict[str, int] = {}
    for raw in code.splitlines():
        for name in REFRESH_CALLS:
            if re.search(r"\b" + name + r"\s*\(", raw):
                counts[name] = counts.get(name, 0) + 1
    return counts


def base_count_for(path: pathlib.Path, call: str) -> int:
    """该文件在基点版本 (HEAD) 里的刷新调用次数。

    R4 是**增量**门禁: 只有当文件里的调用次数**超过基点**时才报 ERROR。
    注入一次 (+1) 未必能超过基点 —— 收敛过的文件可能比基点少很多。
    所以注入的份数必须按基点算, 而不是固定 +1。

    读不出来 (文件是新增的 / git 不可用) 时返回 0: 那时 +1 就够了。
    """
    rel = path.relative_to(REPO).as_posix()
    try:
        r = subprocess.run(["git", "-C", str(REPO), "show", "HEAD:" + rel],
                           capture_output=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return 0
    if r.returncode != 0:
        return 0
    return refresh_counts(r.stdout.decode("utf-8", errors="replace")).get(call, 0)


def pick_r4_target() -> tuple[pathlib.Path, str, int]:
    """挑刷新调用重复最多的文件 + 调用名。

    目标必须**当前**真有重复, 所以动态挑而不是写死 ——
    写死的目标在该文件被收敛后会静默失效 (见文件头的说明)。
    """
    best: tuple[pathlib.Path, str, int] | None = None
    for p in sorted((REPO / "src").rglob("*")):
        if not p.is_file() or p.suffix not in SOURCE_SUFFIXES:
            continue
        try:
            text = read_raw(p)
        except OSError:
            continue
        for name, n in refresh_counts(text).items():
            if best is None or n > best[2]:
                best = (p, name, n)
    return best if best is not None else (REPO / "src", "", 0)


def find_r4_anchor(path: pathlib.Path, call: str,
                   copies: int = 1) -> tuple[str, str, int]:
    """找一行可安全复制的刷新调用语句, 以及它所在的唯一上下文块。

    返回 (anchor_block, injected_block, 该调用当前次数)。
    injected_block 把那行**原样复制 copies 份**, 所以注入后仍是合法 C++,
    而且计数必然 +copies。
    """
    text = read_raw(path)
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    pattern = re.compile(r"\b" + re.escape(call) + r"\s*\(")
    before = refresh_counts(text).get(call, 0)
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.endswith(";") or not pattern.search(line):
            continue
        # 用该行 + 后面 1~2 行做上下文, 要求全文唯一, 否则注入会命中多处
        for span in (3, 2):
            if i + span > len(lines):
                continue
            block = nl.join(lines[i:i + span])
            if text.count(block) != 1:
                continue
            injected = nl.join([line] * copies + lines[i + 1:i + span])
            return block, injected, before
    return "", "", before


def check_r4(failures: list[str]) -> None:
    path, call, n = pick_r4_target()
    if n < R4_MIN_OCCURRENCES:
        failures.append(
            "R4: 全项目找不到刷新调用重复 >= %d 次的文件 (最多的是 %s 的 %d 次) -- "
            "R4 已无法被破坏自检覆盖" % (R4_MIN_OCCURRENCES, path, n))
        return
    rel = path.relative_to(REPO)
    # R4 是增量门禁: 只有超过**基点**的计数才算新增违规。所以注入的份数
    # 必须把计数推到基点之上, 固定 +1 在收敛过的文件上不够。
    base_n = base_count_for(path, call)
    # 锚点块原本有 1 行调用, 注入后变成 copies 行调用, 所以计数涨的是
    # (copies - 1) 而不是 copies: n + (copies - 1) = base_n + 1。
    copies = max(2, base_n - n + 2)
    anchor, inject, before = find_r4_anchor(path, call, copies)
    if not anchor:
        failures.append("R4: 在 %s 里找不到可安全复制的 '%s' 调用行" % (rel, call))
        return
    backup = path.read_bytes()
    try:
        text = backup.decode("utf-8", errors="replace")
        if text.count(anchor) != 1:
            failures.append("R4: 锚点在 %s 里出现 %d 次" % (rel, text.count(anchor)))
            return
        write_raw(path, text.replace(anchor, inject, 1))
        after = refresh_counts(read_raw(path)).get(call, 0)
        if after != before + copies - 1:
            failures.append("R4: 注入后 %s 的计数没变 (%d -> %d, 期望 %d) -- 锚点失效"
                            % (call, before, after, before + copies - 1))
            return
        if after <= base_n:
            failures.append("R4: 注入后 %s 的计数 %d 没有超过基点 %d -- 门禁不会报 ERROR"
                            % (call, after, base_n))
            return
        code, out = run_gate()
        if code == 0 or "[ERROR]" not in out or "R4" not in out:
            failures.append("%s: expected R4 ERROR, got exit=%s; tail:\n%s"
                            % (rel, code, out[-600:]))
        else:
            print("OK  R4: %s -> %s (%d -> %d, base %d) gate went red (exit=%d)"
                  % (rel, call, before, after, base_n, code))
    finally:
        path.write_bytes(backup)


def main() -> int:
    failures: list[str] = []

    for rel, rule, anchor, inject in CASES:
        p = REPO / rel
        if not p.is_file():
            failures.append("%s: file not found" % rel)
            continue
        backup = p.read_bytes()
        try:
            text = backup.decode("utf-8", errors="replace")
            if anchor not in text:
                failures.append("%s: anchor not found -> %s" % (rel, anchor[:60]))
                continue
            p.write_text(text.replace(anchor, inject, 1), encoding="utf-8", newline="")
            code, out = run_gate()
            if code == 0 or "[ERROR]" not in out or rule not in out:
                failures.append(
                    "%s: expected %s ERROR, got exit=%s; tail:\n%s"
                    % (rel, rule, code, out[-600:]))
            else:
                print("OK  %s: %s -> gate went red (exit=%s)" % (rule, rel, code))
        finally:
            p.write_bytes(backup)

    check_r4(failures)

    code, out = run_gate()
    if code != 0:
        failures.append("after restore the gate still fails:\n%s" % out[-600:])
    else:
        print("OK  tree restored, gate green again")

    for f in failures:
        print("  !", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
