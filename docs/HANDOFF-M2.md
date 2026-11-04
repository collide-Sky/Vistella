# HANDOFF — M2（进行中）

> 最后更新：2026-11-03
> 基线 commit：`f8eb0b1`（M1-4/7/8，M1 全部完成）
> 分支：`dev`（本地领先 `origin/dev` 1 个 commit，**push 被网络阻塞**）

本文件是 M2 阶段的交接说明。M2 的 8 个子项里，**M2-3 / M2-5 / M2-6 / M2-7a / M2-8 已落地**，
M2-1 / M2-2 / M2-4 / M2-7b / M2-5c **尚未开工**。

---

## 1. M2 验收标准（来自 `docs/CODE_AUDIT_2026-10-26.md` §9）

1. 产品级走查：打开 → 多图层编辑 → 调整 → 保存 → 重开复现；
2. PS 对照表的主要项从「缺失 / 有但错」变成「部分 / 无」。

**当前状态：未达成** —— 调整（S2-6）已经真的生效，但保存/重开仍是扁平图（M2-1 未做），
面板系统仍是自研拖拽层（M2-2 未做），工具仍有缺口（M2-4 未做）。

---

## 2. 已完成的部分

### M2-3　菜单与 action 体系（`src/app/mainwindow.cpp/.h`）

- 12 个顶级菜单按 PS 顺序重排：文件 / 编辑 / **图像** / 图层 / 文字 / 选择 / 滤镜 / 视图 /
  设置 / 增效工具 / 窗口 / 帮助。实现方式是先 `new QMenu(..., mb)` 再统一 `mb->addMenu(...)`，
  否则顺序取决于构建顺序。
- 新增 **图像 → 调整** 子菜单（曲线 / 色阶 / 色相饱和度 / 黑白 / 通道混合器）。
  PS 把调整归在「图像」下而不是「滤镜」下；HSL 原来**根本没有菜单入口**，一并补上。
- 新增剪切 / 复制 / 粘贴：`QClipboard` + 自定义 mime `application/x-vistella-selection`
  （带选区蒙版）。文本委派 editor，图像走选区/整图复制，剪切清活动图层，粘贴为新图层。
- 新增 `updateActionStates()`：按活动 tab 类型（`DocWindow` / `ImageWindow`）统一启停 action，
  并跟随各文档自己的 undo/redo 可用性。接在 `onTabChanged()`。

### M2-5　选区体系（`src/media/selection/`、`src/media/tools/`、`src/media/imagewindow/ImageCanvas.cpp`）

- 羽化不再二值化，保留灰阶。
- 新增持久化选区模式（`setSelectionMode` / `selectionMode` / `selectionModeChanged`），
  模式归 `SelectionModel`，**切工具不丢**。
- 新增 `alphaAt()`（按蒙版 alpha 比例裁剪）与 `contourSegments()`（逐行扫描 run，输出**真实**轮廓线段）。
  蚂蚁线改成按真实轮廓绘制，不再画 bbox 矩形。
- 绘画裁剪：`ImageWindow::pushLayerImageEdit()` 按选区 alpha 裁剪。
- 测试：`tests/tst_M2_5_Selection.cpp`（15 例）。

### M2-6　调整与曲线

**M2-6a　实时预览 + 取消回滚**（`dialogs/AdjustDialogBase.*`、`imagewindow/AdjustmentPanel.*`、`imagewindow.cpp`）

- 五个调整对话框拖滑块即改画布；OK/Apply 固化成**恰好一条**撤销命令；
  Esc / Cancel 逐字节回滚到打开前的样子，并删掉预览期间新建的调整图层。
- 预览改成「**先 show 再 arm**」：构造期 `setInitialArgs()` → `setValue()` → `valueChanged` →
  `triggerPreview()` 会产生伪预览（宿主还没拍 baseline，回滚不了）。
- `accept()` / `reject()` 都被 override：Esc 键、窗口关闭按钮、程序化调用走同一条收尾路径
  （只挂按钮槽的话，Esc 就不会回滚）。
- 测试：`tests/tst_M2_6_AdjustPreview.cpp`（11 例）+ 破坏自检 `tools/break_check_m2_6_preview.py`。

**M2-6b　HSL / 黑白 / 通道混合器真实现 + 曲线分通道**

- 新增 `src/media/imageworker/layers/AdjustmentTransform.h/.cpp`：
  `AdjustKind{Lut,Hsl,BlackWhite,ChannelMixer}` + `AdjustmentParams` + `applyAdjustmentTransform()`
  + `applyChannelLuts()`。
- **架构决策**：1D LUT 保留给曲线/色阶；HSL / 黑白 / 通道混合走 per-pixel `AdjustmentParams`
  （1D 表表达不了逐像素 RGB 变换）。两条渲染路径（legacy `renderLegacy()` 与 compositor
  `LayerAdjustmentNode`）必须一致 —— 这是 `docs/CONTRACTS.md` 的既有不变量。
- `Layer` 新增 `adjustmentParams` + `adjustmentChannelLuts`；`LayerStack` 新增
  `setAdjustmentParams()` / `setAdjustmentChannelLuts()`。
- 撤销：新增 `LayerCommand::SetAdjustmentState`（`makeSetAdjustmentState()`），
  **整套调整状态**（LUT + per-pixel 参数 + 分通道 LUT）一起还原。
  只还原 LUT 的话，撤销一次 HSL 调整后逐像素参数还留在图层上，画面没回去。
- `AdjustmentPanel` 真正把参数写进调整图层（`buildTabParams()` / `buildTabChannelLuts()`），
  曲线支持分通道（RGB/R/G/B 各一条曲线）。
- 测试：`tests/tst_M2_6_AdjustTransform.cpp`（16 例）+ `tests/tst_M2_6_AdjustWiring.cpp`（8 例）
  + 破坏自检 `tools/break_check_m2_6_transform.py`（4 个注入点）。

### M2-7a　文件树打开路径走分发器（`src/app/mainwindow.cpp/.h`、`media/imageworker/ImageWorkspace.*`）

- 新增 `MainWindow::openDocument(path, err)`：按扩展名走 `MediaDispatcher::openFile()`。
  图片 → imageWorker → `ImageWorkspace`；音频/视频模块未注册时**明确报错**而不是当文本读出乱码；
  无模块认领的扩展名回落 `DocWindow`。
- 新增 `ImageWorkspace::releaseImageWindow()`：把 `ImageWindow` 从 layout 摘除并 reparent 到
  nullptr，使它能作为 tab 主体（tab 层用 `qobject_cast<ImageWindow*>` 识别）。

### M2-8　i18n + 暗色主题

- `buildActions()` 现在**可重入**：语言切换时销毁并重建菜单栏（而不是手工维护 234 处
  `tr()` 的 (action, sourceText) 对照表）。`destroyActions()` 只删 29 个成员 action，
  **不能**用 `findChildren<QAction*>()` 全删（会误删全局工具 action 和文档菜单的 action）。
- 15 个全局工具快捷键 action 抽出到 `buildGlobalToolActions()`，只建一次；
  `toolShortcuts()` 单一表供「创建」和「重译」共用。
- `src/app/styles/Vistella.qss` 修 3 处暗色硬编码：`QSplitter::handle` `#d0d0d0`→`{{BORDER}}`；
  `QTabBar::tab:selected` `#ffffff`→`transparent`（原本盖住了前面的 transparent 规则，暗色下
  出现刺眼白块）；`QToolButton#btnGpuStatus` `#555`→`{{TEXT_SUBTLE}}`。

---

## 3. 尚未开工的部分

| 子项 | 状态 | 关键事实 / 入口 |
|---|---|---|
| **M2-2** 面板系统换原生 `QDockWidget` + `saveState` | 未开工（用户已拍板） | `src/ui/dock/` 12 文件 1487 行；唯一自研拖拽实现是 `VDraggableTabWidget`（.h 174 + .cpp 609）。硬伤 UI-01~05：`new QDockWidget(this)` + flags=0 导致浮动失效；`e->pos()` 当全局坐标；`m_pages` 与 `QTabBar` 双索引；`movePageTo` 无 rebind + 9 个裸指针；`maybeStartDrag` 嵌套事件循环后继续访问 `m_activeDrag`。`RightPanelDock`（406 行）= 1 个 `QSplitter` 装 3 个 `VDraggableTabWidget`。`VPages.h` + `VTabPageBase.h/.cpp` 是 370 行适配层，要适配或删除。`mainwindow.cpp` 的 `onResetLayout` 读 `workspace_default.ini` 但全项目无 `saveState()` 写它（APP-19）。 |
| **M2-4** 工具补齐 | 未开工 | 渐变 / 油漆桶 / 橡皮擦 / 加深减淡 / 模糊锐化涂抹 / 抓手缩放旋转；修裁剪 / Shape / Pen / 画笔选项。相关债务：`A-MOVE`、`A-LAYERDRAG`、`A-TOOLCONFIG`、`A-SWATCH`、`A-TOOLICON`。 |
| **M2-1** 自定义 `.vst` 工程格式 + 自动保存 + 崩溃恢复 | 未开工（用户已拍板 `.vst` 优先，PSD 只读推迟） | 债务 `A-PROJECTFORMAT`（保存写 `currentImage()`，图层/蒙版/智能对象/文字全丢）、`A-AUTOSAVE`（崩溃后未保存内容全丢）。 |
| **M2-7b** 文件树换 `QFileSystemModel` | 未开工 | 债务 `A-FILETREEMODEL`（本次新登记）。`src/ui/FileTreeModel.*` / `FileTreeItem.*` / `FileTreeNodeData.h` 是自研 model：`scanChildren()` 在 UI 线程同步 `entryInfoList` + 每文件 `stat()`；`hasChildren()` 对所有目录恒 true（空目录永远有展开箭头）。改造要点：换 `QFileSystemModel` + `QSortFilterProxyModel`（目录永远放行，文件按 `FileExtensionRegistry::isSupportedExtension` 过滤）；`InfoTreeDock` 的公开 API（`setRootPath` / `rootPath` / `openFileRequested`）必须不变；需删 5 个旧文件 + `tests/tst_FileTreeModel.cpp`（用 `mavis-trash`）。 |
| **M2-5c** 椭圆 / 多边形选区工具 | 未开工 | 在 `src/media/tools/` 下加两个工具，接到 `ModeSelectorBar` 的模式栏。 |

---

## 4. 关键 API 事实（踩过的坑，抄这里）

- `MediaDispatcher::instance().openFile(path, parent)` 返回 `IWorkspace*`；
  图片模块返回 `ImageWorkspace*`（内含 `ImageWindow*`）。
  `ImageWorker::info().extensions` = png/jpg/jpeg/bmp/webp/tif/tiff；
  `FileExtensionRegistry` 额外含 RAW/音频/视频扩展（未注册模块时 `openFailed` 报 "module 'xxx' not registered"）。
- `ImageWindow` 公开 API：`currentImage()` / `selectionModel()` / `layerStack()` /
  `activeLayerImage()` / `activeLayerIndex()` / `pushLayerImageEdit()` / `refreshCanvasNow()` /
  `undoStack()` / `setDocumentImage()`。`ensureLayerStack()` 是 **private**，
  测试要用 `setDocumentImage()` 才能建出图层栈。
- `SelectionModel`：`setSelectionMode(Mode)` / `selectionMode()` / `selectionModeChanged` /
  `alphaAt(QPoint)` / `contourSegments()`（`Segment{QPointF a, b}`）。
  `contourSegments()` 对实心 8×5 矩形产出 **12** 段（1 上 + 1 下 + 5 左 + 5 右）—— 这是正确值。
- `AdjustDialogBase` 新信号：`previewSessionStarted()` / `previewSessionEnded(bool committed)`；
  `m_previewArmed` 在首次 `showEvent` 后才置 true。
- `AdjustmentParams` 的单位：色相是**度**（[-180,180]），OpenCV 的 8bit hue 平面是 0..179
  （= 0..358 度），所以 1 度 = 0.5 个 hue 单位。
- 通道混合矩阵是 **PS 顺序**：行 = 输出通道（R,G,B），列 = 输入通道（R,G,B）；
  像素是 OpenCV BGR 顺序，取值时按 R/G/B 解包。
- `Layer::adjustmentParams.kind != AdjustKind::Lut` 时渲染路径走 per-pixel 变换，
  否则走 1D LUT（可分通道）。**两条路径必须一致。**
- `AdjustmentPanel` 的 `applyCurrentTab()` 用 `m_currentTab` 决定写哪个 tab 的图层：
  对话框回填滑块会触发 `valueChanged` → `applyCurrentTab()`，
  所以 **必须先 `setCurrentIndex()` 再回填滑块**，否则会建出错 tab 的调整图层
  （M2-6b 修过一次这个 bug，`tst_M2_6_AdjustWiring` 里有断言守着）。

---

## 5. 门禁（`docs/CONTRACTS.md` 优先级最高）

```powershell
$py='C:\Users\tongl\AppData\Local\Programs\Python\Python313\python.exe'
Set-Location 'D:\Collide\MyCode\QT6Projects\MultiDoc'

# configure + build
cmd /c 'D:\Collide\MyCode\QT6Projects\MultiDoc\tools\build.bat'

# 全量 ctest（当前 87 条）
cmd /c "set PATH=D:\QT6\6.11.1\msvc2022_64\bin;%PATH% && cd /d D:\Collide\MyCode\QT6Projects\MultiDoc\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug && ctest -j 4 2>&1"

# 契约门禁（不可与 ctest 并行：破坏自检会临时改源文件）
& $py tools\check_contracts.py
& $py tools\break_test_contracts.py
& $py tools\break_check_m1_7_evict.py
& $py tools\break_check_m2_6_preview.py
& $py tools\break_check_m2_6_transform.py
```

**本轮基线**：build OK · ctest **87/87** · `check_contracts` **ERROR=0 WARN=24**
（R1 19 / R3 2 / R4 3）· 两个 M2-6 破坏自检 4 个注入点全部变红且文件逐字节还原。

---

## 6. 工作区约定（别再踩）

1. **源码注释一律英文**；用户面向的 UI 字符串（`tr()` 包裹）可以用中文。
2. **不要用 PowerShell 改源码** —— 用 Python 补丁脚本，锚点用 ASCII / 行号定位。
3. **清理注释后先看 diff 里的非注释行，再跑全量 ctest。** 中文注释清理脚本已经误删过两次真实代码
   （一次吃了 `ImageWorkspace.cpp` 18 行，一次吃了 18 行），都是靠 `git checkout --` 还原的。
4. **文件换行风格逐文件不同**（`mainwindow.cpp` = LF，`ImageWorkspace.cpp` / `Vistella.qss` = CRLF），
   脚本必须检测并保留。
5. 新增源文件必须显式登记进 `src/media/CMakeLists.txt`（没有 GLOB）；
   新增测试必须显式登记进 `tests/CMakeLists.txt`。
6. 改 `CMakeLists.txt` 后必须先跑 `cmake . -DCMAKE_BUILD_TYPE=Debug` 重新 configure
   （已收进 `tools/build.bat`）。
7. 删除文件走 `mavis-trash`：PowerShell 里用**一条** `rm -- "<绝对路径1>" "<绝对路径2>"`，
   路径必须是绝对路径，不能带 `cd` 前缀。
8. TODO 标记唯一格式：`// TODO(P8.4): <还缺什么> -- see docs/DEBT.md#A-XXXX`（两个 ASCII `-`）。
9. `translatefile/*.ts` 必须与代码同 commit（build 会自动跑 lupdate 重新生成）。
10. **像素级回归测试必须用随位置变化的 ramp 图**，不能用纯色图 —— 纯色图会把通道错位/偏移类 bug 完全掩盖。
11. **门禁脚本不可与 ctest 并行**（破坏自检会临时改源文件）。
12. 新测试必须做**破坏自检**（故意改坏 → 确认变红 → 还原，且文件逐字节还原）。
13. `git push` 目前被网络阻塞（代理 `127.0.0.1:7897` 未运行，直连 github.com:443 也不通）——
    commit 要先做，push 等网络恢复。

---

## 7. 已登记的开放债务（`docs/DEBT.md`）

M2 关闭的：`A-ADJUSTPREVIEW`（M2-6a）、`A-HUESATLUT`（M2-6b）、`A-MEDIADISPATCH`（M2-7a）。

M2 相关、仍然开放：

- `A-FILETREEMODEL`（本次新登记，M2-7b）
- `A-VTABAPI` / `A-DRAGTHRESHOLD` / `A-STACKWIDGET`（M2-2）
- `A-PROJECTFORMAT` / `A-AUTOSAVE`（M2-1）
- `A-MOVE` / `A-LAYERDRAG` / `A-TOOLCONFIG` / `A-SWATCH` / `A-TOOLICON`（M2-4）
- `A-THEME`（M2-8 只修了暗色 QSS 硬编码，主题画廊的「应用」按钮仍是空实现）
- `A-FILTERMENU`（滤镜菜单 5 个入口仍 `setEnabled(false)`）
