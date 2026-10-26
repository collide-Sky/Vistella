# AGENTS.md — Vistella / MultiDoc

> **给任何 AI 代理 / 新开发者**：在**读取或修改 `src/` 之前**，必须先读
> [`docs/CONTRACTS.md`](docs/CONTRACTS.md)。那是本项目的最高约束。
> 本文件只是入口，规则正文在 CONTRACTS.md。

---

## 0. 强制前置

| 动作 | 先读 |
|---|---|
| 改任何 `src/**` | `docs/CONTRACTS.md` 全文 |
| 开始一个新阶段 | `docs/CONTRACTS.md` §1（交付单位）+ §4（功能链验收清单） |
| 留任何占位 | `docs/CONTRACTS.md` §2.2 + `docs/DEBT.md` |
| 引入/修改状态 | `docs/CONTRACTS.md` §3（唯一 owner + 订阅模式）+ §7（不变量） |
| 写任何测试 | `docs/CONTRACTS.md` §5（禁止回声测试 + 强制破坏自检） |
| 提交前 | `python tools/check_contracts.py` 必须 ERROR=0 |

---

## 1. 三条最容易违反、且后果最严重的规则

**① 交付的是"一件用户能用的能力"，不是"一段代码"。**
阶段开始前先写出用户视角的操作步骤；阶段结束前，四道门禁（G1 构建 / G2 能力 / G3 不变量 / G4 回归）全绿 + 演示这条路径。
**门禁没全绿之前不要问用户"下个阶段做什么"。**

**② 测试必须能在实现被破坏时失败。**
写完测试后**故意改坏实现一次，确认测试变红**，再改回来。没做过这个自检的测试不许提交。
断言"结构存在""转发次数""cursor() 返回什么"的测试是**回声测试**，禁止。

**③ 注释只写"为什么"，不写"已完成"。**
源码里禁止 `P0-x fix:` / `阶段 N:` / `已完成` / `Default is ON|OFF` 这类会随实现漂移的陈述。
上一轮那个"窗口最大化反复坏"的问题，根因就是一段自称"唯一入口"的假不变量注释 —— 注释让后续排查误以为不用再查第二真相源。

---

## 2. 机器门禁

```powershell
# 增量门禁: 只对相对 HEAD 的新增行报 ERROR (本地开发用)
python tools/check_contracts.py

# 全量盘点: 列出所有存量违规
python tools/check_contracts.py --all --verbose

# 严格: 存量也报 ERROR (清存量时用)
python tools/check_contracts.py --strict

# CI: 对比主分支
python tools/check_contracts.py --base origin/main
```

已接入 CTest：`ctest -R contract_check`。
退出码：`0` 通过 · `1` 有 ERROR · `2` 门禁失效（拿不到改动范围，**不算通过**）。

---

## 3. 环境与命令

- **Qt 6.11.1 · C++17 · OpenCV 4.12 · VS 2022 (vcvars64)**
- 构建目录：`build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug`（Ninja）
- **构建前必须先 kill 掉 `Vistella.exe`**（windeployqt 后置步骤会因 exe 被占用而失败）
- `CMakeLists.txt` 的 `add_link_options(/INCREMENTAL:NO)` **不要删** —— MSVC 增量链接会产出导入表全空的损坏 PE

```powershell
$vc='C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "cd /d D:\Collide\MyCode\QT6Projects\MultiDoc\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug && call `"$vc`" >nul 2>&1 && cmake --build ."
cmd /c "cd /d D:\Collide\MyCode\QT6Projects\MultiDoc\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug && call `"$vc`" >nul 2>&1 && ctest -j 4"
```

---

## 4. 环境事实（不要重复踩）

| 事实 | 后果 |
|---|---|
| Windows `WM_LBUTTONDBLCLK` **不伴随第二个 press** | 双击序列 = press, release, dblclick, release；画布要判断"双击的前半段是否落在自己身上" |
| Qt 的双击判定是 **QWindow 级**，不是 widget 级 | 第二个 press 落在哪个 widget，dblclick 就发给那个 widget |
| `slots` / `signals` / `emit` 在 Qt 6 是保留宏 | 不能作变量名 |
| `cv::Scalar(200)` 只设第一个通道 | 四通道图会写花 |
| PowerShell 的 `rm` 是 `Remove-Item` 的别名 | 删文件必须 `rm -- "<绝对路径>"`，一次一个目标 |
| `.bak*` 一律进 `_backup_archives/` | 不留在源码树（CONTRACTS.md 规则 6.5） |
| 翻译文件是代码改动的"投影" | 每次 commit 前 `git status --short` 看 `translatefile/`，有 `M` 就一起 add |
| 源码注释一律英文 | `QStringLiteral` 不能用于中文（会乱码） |

---

## 5. 相关文档

| 文件 | 作用 |
|---|---|
| `docs/CONTRACTS.md` | **工程契约（最高约束）** |
| `docs/FUNC-ACCEPTANCE.md` | 完整功能链验收清单模板（18 项） |
| `docs/DEBT.md` | 跨阶段前置登记表 + 架构不变量偏差 |
| `docs/acceptance/` | 各阶段填好的验收清单 |
| `docs/CODE_AUDIT_2026-10-26.md` | 全项目审计总报告（394 条缺陷 / 34 条 P0） |
| `audit/modules/` | 9 份模块审计报告 |
| `tools/check_contracts.py` | 机器可执行检查 |
| `tools/contract_allowlist.txt` | 豁免名单（必须写原因） |

---

## 6. 汇报格式（CONTRACTS.md §9 的可操作版本）

每次汇报必须包含：

1. **覆盖了哪几条用户路径**（逐条列出）
2. **四道门禁的实际输出**（G1/G2/G3/G4）
3. **本次没有验证什么**（盲区清单）
4. **本次改动是否需要 ASAN / 人工验证**

禁止只写"全绿 / 没问题 / 好了"。
