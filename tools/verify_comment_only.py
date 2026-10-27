#!/usr/bin/env python3
"""verify_comment_only.py -- prove a change touched comments only.

For every file changed under src/, compare the OLD (git HEAD) and NEW versions
with all comments removed. The code must be byte-identical: a "comment cleanup"
that also edited code is a silent behaviour change, and that is exactly the
kind of thing that must never slip through a 260-file diff.

Comment removal understands string / char / raw literals, so a "//" inside a
string literal is not mistaken for a comment.

Usage:  python tools/verify_comment_only.py [--base HEAD] [--allow FILE]
Exit:   0 = code untouched, 1 = code changed, 2 = base ref unusable.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
ALLOWLIST = REPO / "tools" / "comment_only_allowlist.txt"

_RAW_PREFIX = re.compile(r'R"([^ ()\\\t\n]{0,16})\(')


def load_allowlist(path: pathlib.Path) -> dict[str, str]:
    """白名单: 相对路径 -> 原因。空文件 = 无豁免。"""
    out: dict[str, str] = {}
    if not path.is_file():
        return out
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "#" in line:
            name, reason = line.split("#", 1)
        else:
            name, reason = line, ""
        name = name.strip().replace("\\", "/")
        if name:
            out[name] = reason.strip()
    return out


def scan(text: str) -> list[list[tuple[str, str]]]:
    """Split source text into lines of (kind, text) segments.

    kind is 'code' or 'comment'. Only the comment CONTENT is returned; the
    '//' and '/* */' delimiters are dropped because the comparison only cares
    about what is left.
    """
    out: list[list[tuple[str, str]]] = []
    in_block = False
    for raw in text.splitlines():
        body = raw
        if body.endswith("\r"):
            body = body[:-1]
        segs: list[tuple[str, str]] = []
        code: list[str] = []
        comment: list[str] = []

        def flush() -> None:
            if code:
                segs.append(("code", "".join(code)))
                code.clear()

        if in_block:
            end = body.find("*/")
            if end == -1:
                comment.append(body)
                segs.append(("comment", body))
                out.append(segs)
                continue
            comment.append(body[:end])
            in_block = False
            body = body[end + 2:]
            flush()

        i, n = 0, len(body)
        while i < n:
            ch = body[i]
            if ch == '"':
                m = (_RAW_PREFIX.match(body, i - 1)
                     if i >= 1 and body[i - 1] == "R" else None)
                if m:
                    close = ")" + m.group(1) + '"'
                    j = body.find(close, m.end())
                    if j == -1:
                        code.append(body[i:])
                        break
                    code.append(body[i:j + len(close)])
                    i = j + len(close)
                    continue
                j = i + 1
                while j < n:
                    if body[j] == "\\":
                        j += 2
                        continue
                    if body[j] == '"':
                        j += 1
                        break
                    j += 1
                code.append(body[i:j])
                i = j
                continue
            if ch == "'":
                j = i + 1
                while j < n:
                    if body[j] == "\\":
                        j += 2
                        continue
                    if body[j] == "'":
                        j += 1
                        break
                    j += 1
                code.append(body[i:j])
                i = j
                continue
            if ch == "/" and i + 1 < n:
                nxt = body[i + 1]
                if nxt == "/":
                    flush()
                    segs.append(("comment", body[i + 2:]))
                    i = n
                    break
                if nxt == "*":
                    end = body.find("*/", i + 2)
                    if end == -1:
                        flush()
                        segs.append(("comment", body[i + 2:]))
                        in_block = True
                        i = n
                        break
                    flush()
                    segs.append(("comment", body[i + 2:end]))
                    i = end + 2
                    continue
            code.append(ch)
            i += 1
        flush()
        out.append(segs)
    return out


def code_only(text: str) -> list[str]:
    """Everything that is not a comment, one entry per non-blank line.

    A comment-only line that was deleted entirely leaves an empty entry here;
    dropping blank entries makes the old/new comparison meaningful.
    """
    lines = []
    for segs in scan(text):
        line = "".join(t for kind, t in segs if kind == "code")
        if line.strip():
            lines.append(line)
    return lines


def git_text(base: str, rel_name: str) -> str | None:
    r = subprocess.run(["git", "show", f"{base}:{rel_name}"], cwd=REPO,
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=60)
    if r.returncode != 0:
        return None
    return r.stdout


def main() -> int:
    ap = argparse.ArgumentParser(
        description="verify that the changed files only differ in comments")
    ap.add_argument("--base", default="HEAD",
                    help="base ref to compare against (default: HEAD)")
    ap.add_argument("--allow", default=str(ALLOWLIST),
                    help="allowlist file: 'path  # reason' per line")
    args = ap.parse_args()

    allow = load_allowlist(pathlib.Path(args.allow))

    names = subprocess.run(
        ["git", "diff", "--name-only", "--", "src"], cwd=REPO,
        capture_output=True, text=True, encoding="utf-8",
        errors="replace").stdout.split()
    names = [n for n in names if pathlib.Path(n).suffix in SOURCE_SUFFIXES]
    if not names:
        print("[verify_comment_only] no changed source files - OK")
        return 0

    bad: list[str] = []
    excused: list[str] = []
    for name in names:
        old = git_text(args.base, name)
        if old is None:
            continue
        new = (REPO / name).read_text(encoding="utf-8", errors="replace")
        if code_only(old) == code_only(new):
            continue
        if name.replace("\\", "/") in allow:
            excused.append(name)
            continue
        bad.append(name)

    print(f"[verify_comment_only] checked {len(names)} changed source files")
    if excused:
        print(f"[verify_comment_only] {len(excused)} file(s) changed code on purpose "
              f"(allowlisted): {', '.join(excused)}")
    if bad:
        print(f"[verify_comment_only] FAILED - code changed in {len(bad)} files:")
        for n in bad:
            print(f"  {n}")
        return 1
    print("[verify_comment_only] OK - code (comment-stripped) is byte-identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
