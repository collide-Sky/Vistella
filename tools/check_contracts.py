#!/usr/bin/env python3
"""check_contracts.py — Vistella 工程契约的机器可执行检查。

实现 docs/CONTRACTS.md 中标记为 [CI] 的规则:

  R1  零调用者即不存在        (规则 2.1a)
  R2  注释禁词               (规则 6.1a / 6.2)
  R3  跨阶段 TODO 双向登记    (规则 2.3a)
  R4  重复刷新逻辑           (规则 3.5a)

设计要点
--------
* 增量门禁: 只对**本次提交新增的行**报 ERROR; 存量违规报 WARN。
  这样存量可以逐步清理, 而新代码必须一次干净。
* 白名单: tools/contract_allowlist.txt, 每行 "符号名  # 原因"。
  新增豁免必须写原因, 否则视为无效。
* 退出码: 有 ERROR 时返回 1。

用法
----
  python tools/check_contracts.py                # 增量检查 (对比 HEAD)
  python tools/check_contracts.py --all          # 全量检查
  python tools/check_contracts.py --stage P8.4   # 额外检查该阶段的 DEBT 到期项
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"
DOCS = REPO / "docs"
ALLOWLIST = REPO / "tools" / "contract_allowlist.txt"
DEBT = DOCS / "DEBT.md"

SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}

# Windows 控制台默认 GBK, 脚本输出含中文/箭头会抛 UnicodeEncodeError
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


# --------------------------------------------------------------------------- #
# 基础设施
# --------------------------------------------------------------------------- #

@dataclass
class Finding:
    rule: str
    level: str          # ERROR | WARN | INFO
    where: str
    message: str


@dataclass
class Report:
    findings: list[Finding] = field(default_factory=list)

    def add(self, rule: str, level: str, where: str, message: str) -> None:
        self.findings.append(Finding(rule, level, where, message))

    def count(self, level: str) -> int:
        return sum(1 for f in self.findings if f.level == level)

    def by_rule(self, rule: str) -> list[Finding]:
        return [f for f in self.findings if f.rule == rule]


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def source_files() -> list[Path]:
    if not SRC.is_dir():
        return []
    return [p for p in sorted(SRC.rglob("*")) if p.is_file() and p.suffix in SOURCE_SUFFIXES]


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(REPO)).replace("\\", "/")
    except ValueError:
        return str(path)


def load_allowlist() -> dict[str, str]:
    """白名单: 符号名 -> 原因。空文件 = 无豁免。"""
    out: dict[str, str] = {}
    if not ALLOWLIST.is_file():
        return out
    for raw in read_text(ALLOWLIST).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "#" in line:
            name, reason = line.split("#", 1)
        else:
            name, reason = line, ""
        name = name.strip()
        if name:
            out[name] = reason.strip()
    return out


def git_added_lines(base: str = "HEAD") -> set[tuple[str, int]] | None:
    """返回本次改动新增行的 (相对路径, 行号) 集合。

    返回 None 表示**无法确定增量范围** —— 调用方必须把这条当作门禁失效处理,
    绝不能静默退化成"全部通过"(那等于没有门禁)。
    """
    cmd = ["git", "diff", "--unified=0", base]
    try:
        raw = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if raw.returncode != 0 or raw.stdout is None:
        return None

    added: set[tuple[str, int]] = set()
    current = ""
    for line in raw.stdout.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:].strip()
        elif line.startswith("@@"):
            m = re.search(r"\+(\d+)(?:,(\d+))?", line)
            if m:
                start = int(m.group(1))
                count = int(m.group(2) or 1)
                for i in range(count):
                    added.add((current, start + i))
    return added


# --------------------------------------------------------------------------- #
# 注释提取
# --------------------------------------------------------------------------- #

def strip_code_and_comments(text: str) -> tuple[str, list[tuple[int, str]]]:
    """返回 (去掉注释的代码, [(行号, 注释内容)])。

    逐行扫描, 处理 // 行注释与 /* */ 块注释, 保留行号对应关系。
    """
    code_lines: list[str] = []
    comments: list[tuple[int, str]] = []
    in_block = False
    for idx, raw in enumerate(text.splitlines(), start=1):
        line = raw
        kept: list[str] = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end == -1:
                    comments.append((idx, line[i:]))
                    i = len(line)
                else:
                    comments.append((idx, line[i:end]))
                    in_block = False
                    i = end + 2
                continue
            start = line.find("//", i)
            bstart = line.find("/*", i)
            if bstart != -1 and (start == -1 or bstart < start):
                kept.append(line[i:bstart])
                in_block = True
                i = bstart + 2
                continue
            if start != -1:
                kept.append(line[i:start])
                comments.append((idx, line[start + 2:]))
                i = len(line)
                continue
            kept.append(line[i:])
            i = len(line)
        code_lines.append("".join(kept))
    return "\n".join(code_lines), comments


# --------------------------------------------------------------------------- #
# R1  零调用者即不存在
# --------------------------------------------------------------------------- #

DECL_RE = re.compile(
    r"^\s*(?P<ret>[A-Za-z_][\w:<>,\s\*&]*?)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*\((?P<args>[^;{]*)\)\s*"
    r"(?P<tail>(const)?\s*(noexcept)?\s*(override)?\s*;)\s*$"
)

SKIP_RET = {"return", "if", "for", "while", "switch", "catch", "sizeof", "decltype"}
SKIP_NAME = {"Q_OBJECT", "Q_PROPERTY", "Q_SIGNALS", "Q_SLOTS", "signals", "slots", "emit"}

# Qt 的 signals / slots 段不是"零调用者"意义的 API:
#   signals 通过 connect 的 PMF 绑定, 源码里不会写成 signalName(...)
#   slots  同理
# 因此这些段的声明一律跳过。
QT_SECTION_RE = re.compile(r"^\s*(?:public\s+|protected\s+|private\s+)?"
                           r"(?:Q_SIGNALS|Q_SLOTS|signals|slots)\s*:")

# 函数体起始 (签名与 { 同行)
FUNC_BODY_RE = re.compile(
    r"^\s*(?:[A-Za-z_][\w:<>,\s\*&]*?\s+)?(?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*\s*"
    r"\([^;{}]*\)\s*(?:const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:final)?\s*\{"
)

# 函数签名 ( { 在下一行)
FUNC_SIG_RE = re.compile(
    r"^\s*(?:[A-Za-z_][\w:<>,\s\*&]*?\s+)?(?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*\s*"
    r"\([^;{}]*\)\s*(?:const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:final)?\s*"
    r"(?:=\s*0\s*)?$"
)

CONTROL_RE = re.compile(r"^\s*(if|for|while|switch|catch|do|else|try|return)\b")

# 标识符后紧跟 ( —— 调用点的粗略特征
IDENT_CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")

# PMF 用法: &Class::method / &method
PMF_USE_RE = re.compile(r"&(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\b")


def collect_public_decls(path: Path) -> list[tuple[int, str, str]]:
    """返回 [(行号, 方法名, 完整行)], 只取**类作用域**的 public 非虚函数声明。

    关键: 必须跳过函数体内部, 否则局部变量 (例如
    `std::lock_guard<std::mutex> lk(mu);`) 会被误当成 public API,
    门禁一旦报噪音, 就会被整体忽略。
    """
    text = read_text(path)
    if not text:
        return []
    code, _ = strip_code_and_comments(text)
    access = "private"       # C++ 类默认 private
    results: list[tuple[int, str, str]] = []
    depth = 0
    func_base = 0
    in_func = False
    pending_sig = False
    for idx, raw in enumerate(code.splitlines(), start=1):
        stripped = raw.strip()
        opens = raw.count("{")
        closes = raw.count("}")

        if in_func:
            depth += opens - closes
            if depth <= func_base:
                in_func = False
            continue

        if pending_sig:
            pending_sig = False
            if stripped.startswith("{"):
                func_base = depth
                depth += opens - closes
                in_func = True
                continue

        if not stripped:
            continue

        if QT_SECTION_RE.match(raw):
            access = "skip"    # signals / slots 段整体跳过
            depth += opens - closes
            continue
        m = re.match(r"^(public|protected|private)\s*:", stripped)
        if m:
            access = m.group(1)
            depth += opens - closes
            continue
        if opens > 0 and FUNC_BODY_RE.match(raw) and not CONTROL_RE.match(raw):
            func_base = depth
            depth += opens - closes
            in_func = True
            continue
        if opens == 0 and FUNC_SIG_RE.match(raw) and not CONTROL_RE.match(raw):
            pending_sig = True
            depth += opens - closes
            continue
        depth += opens - closes
        if access != "public":
            continue
        if "{" in stripped or "(" not in stripped:
            continue
        low = stripped.lower()
        if any(k in low for k in ("virtual", "static ", "template<", "friend ", "typedef", "using ")):
            continue
        m = DECL_RE.match(raw)
        if not m:
            continue
        name = m.group("name")
        ret = m.group("ret").strip()
        if name in SKIP_NAME or ret in SKIP_RET:
            continue
        if "=" in m.group("tail"):       # 纯虚 / 默认参数
            continue
        if m.group("args").strip() == "":   # 无参 -> 可能是 getter/工厂, 暂不报
            continue
        results.append((idx, name, stripped))
    return results


def _is_decl_or_def_line(line: str, name: str) -> bool:
    """该行是 name 的声明或定义, 而不是对 name 的**调用**吗?

    统计"零调用者"时必须排除声明行与定义行, 否则
    `void Foo::bar() { ... }` 会被当成一次调用, 缺陷就被放过了。
    """
    pat = (r"^\s*[A-Za-z_][\w:<>,\s\*&]*?\s+"
           r"(?:[A-Za-z_]\w*::)*" + re.escape(name) + r"\s*\([^;{]*\)\s*"
           r"(?:const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:;|\{)")
    return re.match(pat, line) is not None


def build_call_index(files: list[Path]) -> dict[str, list[tuple[Path, int, str]]]:
    """符号名 -> [(文件, 行号, 该行)], 一次性建索引, 避免 O(声明数 x 全仓行数)。"""
    index: dict[str, list[tuple[Path, int, str]]] = {}
    for p in files:
        code, _ = strip_code_and_comments(read_text(p))
        for i, line in enumerate(code.splitlines(), start=1):
            for m in IDENT_CALL_RE.finditer(line):
                index.setdefault(m.group(1), []).append((p, i, line))
            for m in PMF_USE_RE.finditer(line):
                index.setdefault(m.group(1), []).append((p, i, line))
    return index


def rule_zero_callers(report: Report, allow: dict[str, str], added: set[tuple[str, int]]) -> None:
    files = source_files()
    # 只用**代码**统计调用点。
    # 注释里提到符号名**不算调用** —— 否则一条 TODO 标记或说明性注释
    # 就能伪造出"有调用者", 把真正的零调用者缺陷藏掉 (门禁形同虚设)。
    index = build_call_index(files)

    for path in files:
        if path.suffix not in {".h", ".hpp"}:
            continue
        for lineno, name, line in collect_public_decls(path):
            if name in allow:
                continue
            hits = 0
            for other, i, l in index.get(name, ()):
                if other == path and i == lineno:
                    continue                    # 排除声明行本身
                if _is_decl_or_def_line(l, name):
                    continue                    # 排除声明/定义行
                hits += 1
                break
            if hits:
                continue
            where = f"{rel(path)}:{lineno}"
            is_new = (rel(path), lineno) in added
            report.add(
                "R1", "ERROR" if is_new else "WARN", where,
                f"public 方法 '{name}' 在 src/ 内没有任何调用点 (零调用者即不存在): {line[:90]}",
            )


# --------------------------------------------------------------------------- #
# R2  注释禁词
# --------------------------------------------------------------------------- #

BANNED_COMMENT_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bP\d+(\.\d+)*\s*[-:]?\s*(fix|Fix|FIX|修复)\b"), "变更日志式注释 (P0-x fix)"),
    (re.compile(r"阶段\s*\d+"), "变更日志式注释 (阶段 N)"),
    (re.compile(r"\b20\d{2}[-/]\d{1,2}[-/]\d{1,2}\b"), "日期戳注释 (应写进 commit message)"),
    (re.compile(r"(已完成|已修复|已实现|已接入)"), "声称'已完成'的注释 (会随实现漂移)"),
    (re.compile(r"Default\s+is\s+(ON|OFF|on|off)\b"), "会漂移的默认值声明"),
    (re.compile(r"(占位|留接口|暂不实现|待接线|not\s+wired|will\s+wire)\b", re.I),
     "未登记的占位承诺 (必须登记到 docs/DEBT.md 并写 TODO(Px.y) 标记)"),
]


def rule_banned_comments(report: Report, added: set[tuple[str, int]]) -> None:
    for path in source_files():
        text = read_text(path)
        if not text:
            continue
        _, comments = strip_code_and_comments(text)
        for lineno, comment in comments:
            for pattern, why in BANNED_COMMENT_PATTERNS:
                if pattern.search(comment):
                    where = f"{rel(path)}:{lineno}"
                    is_new = (rel(path), lineno) in added
                    report.add(
                        "R2", "ERROR" if is_new else "WARN", where,
                        f"{why}: {comment.strip()[:100]}",
                    )
                    break


# --------------------------------------------------------------------------- #
# R3  跨阶段 TODO 双向登记
# --------------------------------------------------------------------------- #

# 宽松匹配: 只用于**定位**哪些行带了 TODO 标记 (包括格式写错的行)。
TODO_ANY_RE = re.compile(r"TODO\(P(?P<ver>\d+(?:\.\d+)*)\)")

# 严格匹配: 唯一合法的写法 (CONTRACTS.md 规则 2.3)。
#   TODO(P<x.y>): <还缺什么> -- see docs/DEBT.md#<ID>
# 注意 "-- see" 部分**不能**是可选项: 若可选, 非贪婪的 what 会在空串处
# 提前结束匹配, 导致每条合法标记都被误判成"未指向登记项"。
TODO_STRICT_RE = re.compile(
    r"TODO\(P(?P<ver>\d+(?:\.\d+)*)\)\s*[:：]\s*"
    r"(?P<what>[^\n]+?)\s*"
    r"--\s*see\s+docs/DEBT\.md#(?P<ref>[A-Z][A-Z0-9\-]*)"
)

TODO_FORMAT_HINT = "TODO 标记格式必须是: TODO(P<x.y>): <还缺什么> -- see docs/DEBT.md#<ID>"

# DEBT.md 里代表"阶段"的合法列名 (表格列序在 A.1 / A.2 / A.4 之间并不一致,
# 所以必须按**表头名字**定位列, 不能写死列下标)。
STAGE_HEADER_NAMES = {"建议阶段", "计划阶段", "所属阶段"}


def debt_tables(text: str) -> list[tuple[list[str], list[list[str]]]]:
    """解析 DEBT.md 里的所有 markdown 表格, 返回 [(表头, 行列表), ...]。"""
    tables: list[tuple[list[str], list[list[str]]]] = []
    header: list[str] | None = None
    rows: list[list[str]] = []

    def flush() -> None:
        nonlocal header, rows
        if header is not None:
            tables.append((header, rows))
        header, rows = None, []

    for line in text.splitlines():
        s = line.strip()
        if not s.startswith("|"):
            flush()
            continue
        cells = [c.strip() for c in s.strip("|").split("|")]
        if cells and all(set(c) <= set("-: ") for c in cells):
            continue                      # markdown 分隔行
        if header is None:
            header = cells
            continue
        rows.append(cells)
    flush()
    return tables


def rule_todo_registry(report: Report) -> None:
    debt_text = read_text(DEBT)
    if not debt_text:
        report.add("R3", "ERROR", "docs/DEBT.md", "DEBT.md 不存在, 跨阶段前置无法登记")
        return

    # DEBT.md 中登记过的 ID
    debt_ids: set[str] = set()
    for m in re.finditer(r"`(A-[A-Z0-9\-]+)`", debt_text):
        debt_ids.add(m.group(1))

    # 状态为 open 的条目 (表格最后一列 == open, 且首列是 ID)
    #
    # 只有**占位类**登记项才要求源码里有对应的 TODO 标记:
    #   §A.1 / §A.2 / §A.4 的表头都含 "缺什么" 或 "问题" -> 占位类, 必须在源码落标记
    #   §E 的存量注释清理表不含这两列 -> 它清理的是散落在几百个文件里的注释,
    #     无法一一落标记, 因此**不**要求源码标记 (但 --stage 仍会按期催办)
    PLACEHOLDER_HEADER_KEYS = ("缺什么", "问题")
    open_ids: set[str] = set()
    for header, rows in debt_tables(debt_text):
        if not any(k in header for k in PLACEHOLDER_HEADER_KEYS):
            continue
        for cells in rows:
            if len(cells) >= 2 and cells[-1] == "open" and cells[0].startswith("`"):
                open_ids.add(cells[0].strip("`"))

    seen: set[str] = set()
    for path in source_files():
        text = read_text(path)
        if not text:
            continue
        for idx, raw in enumerate(text.splitlines(), start=1):
            if not TODO_ANY_RE.search(raw):
                continue
            m = TODO_STRICT_RE.search(raw)
            if not m:
                report.add("R3", "ERROR", f"{rel(path)}:{idx}",
                           f"{TODO_FORMAT_HINT} | 实际: {raw.strip()[:110]}")
                continue
            ref = m.group("ref")
            what = (m.group("what") or "").strip()
            if ref not in debt_ids:
                report.add("R3", "ERROR", f"{rel(path)}:{idx}",
                           f"TODO 指向的登记项 '{ref}' 在 docs/DEBT.md 中不存在")
            elif not what:
                report.add("R3", "ERROR", f"{rel(path)}:{idx}",
                           f"TODO(P{m.group('ver')}) 没写清楚'还缺什么' (规则 2.3)")
            seen.add(ref)

    for ref in sorted(open_ids - seen):
        report.add("R3", "WARN", "docs/DEBT.md",
                   f"登记项 '{ref}' 状态为 open, 但源码里找不到对应 TODO 标记 (可能已实现未更新表)")


# --------------------------------------------------------------------------- #
# R4  重复刷新逻辑
# --------------------------------------------------------------------------- #

REFRESH_CALLS = [
    "renderToView", "invalidateCurrentCache", "updateActionStates",
    "invalidateAndRender", "refreshAll", "rebuildCurrentCache", "renderToViewPublic",
]


def rule_duplicate_refresh(report: Report, added: set[tuple[str, int]]) -> None:
    sites: dict[str, list[str]] = {name: [] for name in REFRESH_CALLS}
    for path in source_files():
        text = read_text(path)
        if not text:
            continue
        code, _ = strip_code_and_comments(text)
        for idx, raw in enumerate(code.splitlines(), start=1):
            for name in REFRESH_CALLS:
                if re.search(r"\b" + name + r"\s*\(", raw):
                    sites[name].append(f"{rel(path)}:{idx}")
    for name, locs in sites.items():
        if len(locs) < 2:
            continue
        # 分散在不同文件属于正常; 同一文件内重复 >= 3 次才提示收敛
        by_file: dict[str, list[str]] = {}
        for loc in locs:
            by_file.setdefault(loc.split(":")[0], []).append(loc)
        for fname, flocs in by_file.items():
            if len(flocs) >= 3:
                report.add("R4", "WARN", fname,
                           f"'{name}' 在同一文件出现 {len(flocs)} 次, 应收敛到单一刷新入口: {', '.join(flocs[:6])}")


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #

def main() -> int:
    ap = argparse.ArgumentParser(description="检查 docs/CONTRACTS.md 中标记为 [CI] 的规则")
    ap.add_argument("--all", action="store_true",
                    help="全量扫描并报告所有存量违规 (存量仍为 WARN, 用于盘点)")
    ap.add_argument("--strict", action="store_true",
                    help="全量扫描, 存量违规也报 ERROR (清存量时用)")
    ap.add_argument("--base", default="HEAD",
                    help="增量基线 ref, 默认 HEAD; CI 里用合并基点, 例如 --base origin/dev")
    ap.add_argument("--verbose", action="store_true", help="逐条列出 WARN (默认只汇总计数)")
    ap.add_argument("--stage", default="", help="额外检查该阶段到期的 DEBT 条目, 例: P8.4")
    args = ap.parse_args()

    if not SRC.is_dir():
        print(f"[check_contracts] 找不到 {SRC}", file=sys.stderr)
        return 2

    report = Report()
    allow = load_allowlist()

    if args.all or args.strict:
        added: set[tuple[str, int]] | None = set()   # 无增量范围: 存量只报 WARN
        strict = args.strict
    else:
        added = git_added_lines(args.base)
        strict = False
        if added is None:
            print(f"[check_contracts] ERROR: 无法取得相对 '{args.base}' 的改动范围 "
                  f"(不是 git 仓库? 或 ref 不存在?)", file=sys.stderr)
            print("[check_contracts] 门禁失效, 拒绝静默通过。", file=sys.stderr)
            print("[check_contracts] 提示: 用 --all 盘点, 或换用正确的 --base。", file=sys.stderr)
            return 2

    rule_zero_callers(report, allow, added)
    rule_banned_comments(report, added)
    rule_todo_registry(report)
    rule_duplicate_refresh(report, added)

    if strict:
        for f in report.findings:
            if f.level == "WARN":
                f.level = "ERROR"

    if args.stage:
        debt_text = read_text(DEBT)
        # 阶段列按**表头名字**定位: A.1 / A.2 / A.4 三张表的列序不同,
        # 写死列下标会漏掉整张表 (A.2 的 11 项曾因此永远查不出来)。
        for header, rows in debt_tables(debt_text):
            idx_stage = next((i for i, h in enumerate(header)
                              if h.replace("`", "").strip() in STAGE_HEADER_NAMES), None)
            if idx_stage is None:
                continue
            for cells in rows:
                if len(cells) <= idx_stage or cells[-1] != "open":
                    continue
                if cells[idx_stage].replace("`", "").strip() != args.stage:
                    continue
                report.add("R3", "ERROR", "docs/DEBT.md",
                           f"阶段 {args.stage} 到期条目 '{cells[0]}' 仍为 open (规则 2.4: 到期必须优先偿还)")

    errors = report.count("ERROR")
    warns = report.count("WARN")

    if not report.findings:
        print("[check_contracts] OK - 契约检查全部通过")
        return 0

    for rule in ("R1", "R2", "R3", "R4"):
        items = report.by_rule(rule)
        if not items:
            continue
        errs = [f for f in items if f.level == "ERROR"]
        wrns = [f for f in items if f.level == "WARN"]
        print(f"\n=== {rule}: ERROR={len(errs)} WARN={len(wrns)} ===")
        for f in errs:
            print(f"  [ERROR] {f.where}: {f.message}")
        if args.verbose:
            for f in wrns:
                print(f"  [WARN ] {f.where}: {f.message}")
        elif wrns:
            print(f"  (存量 WARN {len(wrns)} 条, 用 --verbose 逐条查看)")

    print(f"\n[check_contracts] ERROR={errors} WARN={warns}")
    if errors:
        print("[check_contracts] FAILED - 有 ERROR, 必须修复后才能提交")
        return 1
    print("[check_contracts] PASSED (仅存量 WARN) - 存量违规请逐步清理, 并登记到 docs/DEBT.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
