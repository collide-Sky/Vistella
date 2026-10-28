# M1-3 交接说明（2026-10-28）

> 本文件是 **M1-3 alpha/蒙版改造** 的交接单。新会话请先读这份，再读
> `docs/CONTRACTS.md` 与 `docs/DEBT.md`，然后按本文的「剩余工作清单」接着做。
>
> **当前仓库状态：HEAD = `0d8add7`（M1-2，已推送 origin/dev）。
> M1-3 的全部改动只在工作区，未提交。生产代码已能编译，但 5 个测试文件编译不过，
> 所以整个工程现在 build 是红的 —— 不要在这个状态下提交。**

---

## 1. 路线图位置

按 `docs/CODE_AUDIT_2026-10-26.md` §9 的 **M0 → M1 → M2 → M3** 顺序推进。

| 阶段 | 状态 | 提交 |
|---|---|---|
| M0 止血（9 项） | ✅ 完成并推送 | `c6d5c4f` |
| M1-1 像素真相收敛 | ✅ 完成并推送 | `cb0c813` |
| M1-2 图层身份 | ✅ 完成并推送 | `0d8add7` |
| **M1-3 alpha/蒙版** | 🔧 **进行中（工作区未提交）** | — |
| M1-4 ~ M1-9 | ⬜ 未开始 | — |
| M2 / M3 | ⬜ 未开始 | — |

---

## 2. M1-3 已拍板的方案（用户已确认，不要再改方向）

用户通过问卷明确选择：

1. **方案 A**：4 通道 BGRA 全链路 + 蒙版模型统一 + Porter-Duff 合成（**一次到位**）
   —— 不是「3 通道 + 独立 alpha 掩码」的方案 B。
2. **连带修 LYR-13 / LYR-14**，因此 `A-GROUPZORDER`（编组 z 序，已登记 P8.4）
   也在 M1-3 内一并解决。

### 2.1 像素与合成契约（已写进 `src/media/imageworker/layers/PixelFormat.h` 的文件头）

1. **图层像素**：栈内一律 `CV_8UC4 BGRA`，**直通 alpha（非预乘）**。
   入栈时由 `LayerStack` 统一 `toBgra()` 归一化，渲染层永远只面对一种格式。
2. **蒙版**：唯一模型是 `Layer::mask`（`LayerMask` 结构体）。
   覆盖度由 `LayerMaskRenderer::buildAlpha()` 统一折算（唯一入口），
   再由 `LayerMaskRenderer::applyAlpha()` 写进源层 alpha。
   **蒙版不直接参与合成，也不改颜色。**
3. **合成**：Porter-Duff `over`
   ```
   cov     = src.a * 图层不透明度
   out.a   = cov + dst.a * (1 - cov)
   out.rgb = (src.rgb * cov + dst.rgb * dst.a * (1 - cov)) / out.a
   ```
   混合模式只决定 `src.rgb` 怎么算。
4. **图层不透明度 ≠ 像素 alpha**：前者是图层属性（乘在覆盖度上），后者是像素自带透明。
5. 合成走两条路径（legacy / compositor），**共用** `layers::applyBlend`、
   `layers::compositeOver`、`LayerMaskRenderer::buildAlpha` —— parity 由构造保证。

---

## 3. M1-3 已落地的工作区改动（25 个文件 + 2 个新文件，713 增 / 983 删）

### 3.1 新增

| 文件 | 内容 |
|---|---|
| `src/media/imageworker/layers/PixelFormat.h` | 像素契约 + `toBgra` / `splitBgra` / `premultiply` / `unpremultiply` / `compositeOver` 声明 |
| `src/media/imageworker/layers/PixelFormat.cpp` | 上述实现；`compositeOver` 有 **快速路径**（源与画布 alpha 全 255 → `cv::addWeighted`，SIMD），否则走标量 Porter-Duff |

`src/media/CMakeLists.txt` 已登记这两个文件。

### 3.2 图层模型

- `Layer.h`
  - **删除** `cv::Mat layerMask` 与 `bool maskEnabled`（双份蒙版状态的另一半）
  - `image` 字段注释改为像素契约（栈内 4 通道 BGRA 直通 alpha）
  - `isValid()` 接受 3 或 4 通道
- `LayerMaskRenderer.h/.cpp`
  - **删除** `apply()`（RGB 逐通道相乘 —— 这就是 CP-07「变黑而不是透明」的来源）
  - **新增** `applyAlpha()`：`out.a = pixel.a * 蒙版覆盖度 / 255`，颜色不动
  - `buildAlpha()` 保持不变（唯一折算入口）
  - 补 `#include <QSize>`（API 直接用了 QSize，不能靠传递包含）
- `LayerStack.h/.cpp`
  - **删除**旧蒙版 API `addMask` / `clearMask` / `enableMask`
  - 入栈归一化：`appendLayerPtr` / `insertLayer` / `setBaseLayer` → `toBgra()`
  - `rasterize()`：Text/Vector **保留 alpha**（不再 `BGRA2BGR`，这是 LYR-01 画布变黑的根因）、Bitmap 走 `toBgra`
  - `renderLegacy()` 重写：文档级 4 通道白画布 → **全栈**循环（LYR-14）→ 底图走 `visible`（LYR-13）→ 组画布透明化 → LUT 只作用颜色通道
  - `renderOne()`：4 通道；蒙版走 `applyAlpha`；不透明度缩 **alpha** 不缩颜色
  - `compositeChildrenInto()`：4 通道 + 蒙版 + 嵌套组用透明画布
  - `mergeIntoGroup()`：组落在**最上面那个被选图层原来的位置**（PS 行为）→ 顺带解掉 `A-GROUPZORDER`
- `LayerCommand.h/.cpp`
  - **删除** `AddMask` / `ClearMask` / `EnableMask` 三个 Op、三个 factory、对应 `captureAfter` / `applyState` 分支
- `LayerPanel.cpp`：蒙版状态改读 `l->mask.hasData()` / `l->mask.enabled`
- `imagewindow.cpp`：三个蒙版 handler（add / clear / toggle）改用 `LayerMaskCommand`（before/after 快照），并加 `#include "imageworker/layers/LayerMaskCommand.h"`

### 3.3 混合模式

- `BlendMode.h/.cpp`
  - 15 个公开 `blendXxx` **一字未改**（3 通道旧行为完全不变 → `tst_BlendMode` 既有断言不受影响）
  - `applyBlend()` 增加 4 通道路径：拆 alpha → `blendColorOnly()` 算颜色 → `compositeOver()` 合成
  - 新增内部 `blendColorOnly()`（复用公开函数，opacity 传 1.0）

### 3.4 compositor 路径

- `LayerStackAdapter.cpp/.h`
  - `MaskBitmapNode` 改用 `LayerMaskRenderer::buildAlpha()` 产覆盖度
  - `makeRoot()` 改成**文档级白画布根**（底图不再当根 → 隐藏底图真的会露出下面内容）
  - `build()` 覆盖**全栈**
  - 编组分支用**透明** `SolidCanvasNode` 作为组画布
  - 移除 `LayerOpacityNode` 相关代码
- `SolidCanvasNode.cpp`：输出 4 通道（alpha 由调用方给：白画布 255 / 组画布 0）
- `LayerBitmapNode.cpp`：输出 4 通道（`toBgra`），padding 为透明
- `LayerMaskNode.cpp`：改为 alpha 语义（`out.a = base.a * coverage / 255`）
- `LayerBlendNode.cpp`：`fitToSize` 保持 4 通道，padding 透明
- `LayerAdjustmentNode.cpp`：LUT 只作用颜色通道，alpha 原样保留
- **删除** `LayerOpacityNode.h/.cpp`
  （底图不再需要「压黑」特判，它失去唯一生产调用者；按仓库「零调用者即不存在」规则删除）
  —— 已走 mavis-trash，`git status` 显示为 `D`

### 3.5 已完成的编译验证

- **生产代码（`src/`）全部编译通过** —— 最后一次 `cmake --build . -- -k 0` 只剩测试文件的错。

---

## 4. 剩余工作清单（按顺序做）

### 4.1 【阻塞】5 个测试文件编译不过

| 文件 | 错误数 | 怎么改 |
|---|---|---|
| `tests/tst_LayerMask.cpp` | 61 | **整个重写**。它测的是被删掉的旧 API（`addMask` / `enableMask` / `clearMask` + `LayerCommand::makeAddMask` / `makeClearMask` / `makeEnableMask`）。改成新模型：`addPixelMask` / `setMaskEnabled` / `clearMaskFull` + `LayerMaskCommand` 的 before/after 快照 |
| `tests/tst_LayerMaskStruct.cpp` | 12 | 6 处 `LayerMaskRenderer::apply(...)` → `applyAlpha(...)`；断言从 `Vec3b` 颜色改成 alpha 值，且图层改成 4 通道。**注意：这些断言原本写死了 CP-07 的 bug 语义**（"half-opacity mask halves pixel values"），必须改成新语义 |
| `tests/tst_VectorMask.cpp` | 4 | 2 处 `apply` → `applyAlpha`；同上改断言 |
| `tests/tst_P8_1_1.cpp` | 8 | 3 处 `layer.layerMask` / `layer.maskEnabled` → `layer.mask.pixel` / `layer.mask.enabled`；**另外**渲染输出现在是 4 通道，`Vec3b` 断言要改 `Vec4b` |
| `tests/tst_P8_RenderParity.cpp` | 2 | 同样换 `layer.mask` |

### 4.2 M1-3 尚未开工的子项

- [ ] **工具 / 滤镜链 4 通道**：像素已归一化成 4 通道，但这些地方仍按 3 通道假设工作
      （`src/media/imageprocessor.cpp`、`src/media/filters/FilterStrategy.cpp`、
      `src/media/brushes/*`、画笔 / 马赛克 / 克隆 / 修复 / 液化）
- [ ] **显示端**：`src/media/imageprocessor.cpp:112` `matToQImage()` 支持 4 通道 → `QImage::Format_ARGB32`
- [ ] **`src/app/mainwindow.cpp`** 的 3/4 通道分支（746 / 2601 / 2771 行附近）
- [ ] **LYR-41 蒙版操作可撤销**：`density` / `feather` / `invert` 三个 handler 现在**不 push 命令**
      （`imagewindow.cpp` 的 `setMaskDensityRequested` / `setMaskFeatherRequested` / `setMaskInvertRequested`）→ 改走 `LayerMaskCommand`
- [ ] **`docs/DEBT.md` 更新**
      - `A-LAYERMASK`（`LayerMaskRenderer` 只被 tests 调用）→ 已接线，状态改 `done`，**同时删掉** `LayerMaskRenderer.h` 里的源码 TODO 标记
      - `A-GROUPZORDER`（P8.4）→ 组 z 序 + LYR-14 已修，状态改 `done`，**同时删掉**源码 TODO 标记
      - `INV-ID-1`（§B）维持 `partial` 不变
- [ ] **`docs/CONTRACTS.md` 补像素/蒙版契约章节**（契约目前只写在 `PixelFormat.h` 文件头）
- [ ] **新增单测**（建议 `tests/tst_AlphaMask.cpp`）：alpha 契约、蒙版真生效且不遮黑、
      隐藏底图真露出、全栈合成、编组 z 序落位、legacy / compositor parity
- [ ] **验证**：全量 build → 全量 `ctest` → 门禁 `ERROR=0` → **破坏自检**（必须真的改坏确认变红再还原）
- [ ] **提交 M1-3**（翻译文件 `translatefile/*.ts` 必须同 commit）

---

## 5. 关键技术决定（新会话别推翻）

1. **图层面 = 4 通道 BGRA 直通 alpha；合成缓冲 = 预乘是「合成过程」的中间形态，
   但对外（存盘 / 显示）一律直通 alpha。** 不要引入「图层存预乘」——滤镜在非预乘上跑才正确。
2. **蒙版只改 alpha，颜色由合成阶段决定。** 任何「蒙版乘 RGB」的写法都是 CP-07 复发。
3. **两条渲染路径共用原语**（`applyBlend` / `compositeOver` / `buildAlpha`），
   不要在某个节点里另写一份合成数学，否则 parity 立刻漂移。
4. **组是独立合成栈**：子层合成到**透明**组画布，再按组不透明度叠回外层。
   legacy 与 compositor 两边都已按这个改。
5. **底图是普通图层**：不再当画布根，不再有「压黑」特判 → `LayerOpacityNode` 因此删除。
6. `mergeIntoGroup` 的插入点算法：**最上面那个被选图层原位置**
   = 它下面那些**没被选中**的图层数（被选中的已被移除）。

---

## 6. 陷阱清单（本轮实际踩到 / 已知的）

1. **`cv::Size::height` 是成员变量，不是函数** —— 写成 `canvas.height()` 会让 MSVC 报
   一串莫名其妙的 C2064/C2440（把 `QSize(a, b)` 误解析成函数式转换）。本轮为此浪费了一轮编译。
2. **头文件用了 `QSize` 就必须自己 `#include <QSize>`** —— `LayerMaskRenderer.h` 之前靠 `.cpp` 里
   `<QImage>` 的传递包含侥幸通过，换个 TU 包含就炸。
3. **本仓库的 Edit 工具会吞掉换行**（CRLF 文件下会把相邻两行合并）→ 一律用 Python 补丁脚本
   （`C:\Users\tongl\AppData\Local\Temp\` 下 m13_*.py 可作模板）。
4. **ninja 增量构建会留下陈旧 .obj**：构建前先
   `Get-ChildItem -Path src,tests -Recurse -Include *.cpp,*.h,*.hpp | ForEach-Object { $_.LastWriteTime = Get-Date }`。
   用 `copy`/`copy2` 还原文件会保留旧 mtime 导致 ninja 跳过重编（会出现假红）。
5. **补丁锚点偏移**：批量改写务必用**函数边界（花括号配对）**定位，不要手数行数；
   每批之后跑符号集合对比。本轮用 `m13_comp_b.py` 的 `find_func_block()` 模板。
6. **删除文件必须走 mavis-trash**：单条顶层命令 `rm -- "<绝对路径1>" "<绝对路径2>"`，
   不能和别的命令混在一条 PowerShell 里（会被安全策略拦）。
7. **门禁的 TODO 标记必须严格单行**：
   `// TODO(P8.4): <还缺什么> -- see docs/DEBT.md#A-XXXX`
   —— 分隔符是**两个 ASCII `-`**，ID 必须与 DEBT.md 逐字一致，且 DEBT 里状态为 `open`
   的条目必须在源码有对应 TODO 标记（双向校验）。
8. **翻译文件必须与代码同 commit**（build 会用 lupdate 重生成行号）。

---

## 7. 常用命令

```powershell
$vc='C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat'
$B='D:\Collide\MyCode\QT6Projects\MultiDoc\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug'
$py='C:\Users\tongl\AppData\Local\Programs\Python\Python313\python.exe'

# 构建 (先 touch 全部源文件, 再全量编)
Get-ChildItem -Path src,tests -Recurse -Include *.cpp,*.h,*.hpp | ForEach-Object { $_.LastWriteTime = Get-Date }
cmd /c "cd /d $B && call `"$vc`" >nul 2>&1 && cmake --build . -- -k 0"

# 测试
cmd /c "cd /d $B && call `"$vc`" >nul 2>&1 && ctest -j 4"

# 门禁
cd D:\Collide\MyCode\QT6Projects\MultiDoc
& $py tools\check_contracts.py            # 增量 (base HEAD)
& $py tools\check_contracts.py --all --verbose
& $py tools\break_test_contracts.py       # 门禁自身破坏自检 (R1~R4)

# 单跑一个测试 exe (在 build 根目录, 不是 build/tests/)
& "$B\tst_LayerIdentity.exe"
```

失败记录：`$B/Testing/Temporary/LastTestsFailed.log`

---

## 8. 复盘：本轮还发现并修掉的两个「门禁自身失效」

1. `tools/break_test_contracts.py` 的 R4 用例写死了 `src/media/tools/Brush.cpp`。
   M1-1 把该文件收敛成 `refreshCanvasNow()` 单一入口后，注入的 1 次刷新调用够不到
   R4 的「同一文件 ≥ 2 次」阈值 → **R4 覆盖静默失效**，而脚本只报
   "expected R4 ERROR"，看不出真正原因是目标已不存在重复。
   **已修**：R4 用例改成动态挑刷新调用最多的文件 + 与门禁共用同一套统计逻辑
   （`from check_contracts import REFRESH_CALLS, SOURCE_SUFFIXES, strip_code_and_comments`），
   并在注入后断言计数确实 +1。现在 R1~R4 四条全部能变红。
2. `tools/check_contracts.py` 与 `break_test_contracts.py` 曾用
   `Path.read_text()`（通用换行转换）与按字节解码混用，导致锚点在 CRLF 文件上
   「明明找到却匹配不上」。已统一成 `read_raw()`（按字节解码，保留换行）。

---

## 9. 当前未验证 / 未做的事（**交接时的事实，不要当成已完成**）

- M1-3 **从未跑过 `ctest`** —— 测试文件还编译不过。
- M1-3 **从未跑过门禁**。
- M1-3 **没做破坏自检**。
- M1-3 **没有提交、没有推送**。
- 工具 / 滤镜 / 显示链的 4 通道适配**一行都没做**（像素已归一化成 4 通道，
  但这些模块仍按 3 通道假设读写 —— 可能编译通过但语义已经错了，
  **必须在 4.2 的清单里逐个核对**）。
