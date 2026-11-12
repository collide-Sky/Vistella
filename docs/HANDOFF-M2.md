# HANDOFF — M2（进行中）

> 最后更新：2026-11-12
> 基线 commit：`58eef6a`（M2 收尾：`.vst` 工程格式 + 自动保存 + 视图变换单一写入方 + 刷新入口收敛）
> 分支：`dev`（本地领先 `origin/dev` 3 个 commit，**push 按用户指示暂缓**）

本文件是 M2 阶段的交接说明。M2 的 8 个子项**全部落地**：
M2-1 / M2-2 / M2-3 / M2-4 / M2-5 / M2-6 / M2-7 / M2-8。

---

## 1. M2 验收标准（来自 `docs/CODE_AUDIT_2026-10-26.md` §9）

1. 产品级走查：打开 → 多图层编辑 → 调整 → 保存 → 重开复现；
2. PS 对照表的主要项从「缺失 / 有但错」变成「部分 / 无」。

**当前状态：未达成** —— 调整（S2-6）已经真的生效，工具补齐了（M2-4），
面板系统换成了原生 `QDockWidget`（M2-2），**保存/重开走 `.vst` 工程格式了**（M2-1）。
剩下的差距在产品级走查本身（打开 → 多图层编辑 → 调整 → 保存 → 重开复现）还没做完。

---

## 2. 已完成的部分

### M2-1　`.vst` 工程格式 + 自动保存 + 崩溃恢复

见第 5 节。

### 结构性收尾　刷新入口收敛 + `.vst` blob 命名

见第 11 节。

### M2-5c　椭圆 / 多边形选框

见第 6 节。

### M2-4　工具补齐（`src/media/tools/`）

见第 7 节（尾巴）与第 2 节末尾（M2-4 主体的工具组 / 8 类新工具）。

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

### M2-7b　文件树换 `QFileSystemModel`（`src/ui/FileTreeProxyModel.*`、`src/ui/infotreedock.*`）

- 自研的 `FileTreeModel` / `FileTreeItem` / `FileTreeNodeData` / `tests/tst_FileTreeModel.cpp`
  **已删**（进回收站）。现在树是 `QFileSystemModel`（工作线程读目录）+ `FileTreeProxyModel`
  （`QSortFilterProxyModel` 子类）。
- 过滤规则：目录**永远放行**（用户要能走进去），文件按
  `FileExtensionRegistry::isSupportedExtension()` 过滤。
- 展开箭头：目录**已读且为空** → 没有箭头；**未读** → 箭头乐观显示（读是在后台线程做的，
  用户必须能在加载时打开目录）。已读集合来自 `QFileSystemModel::directoryLoaded` 信号。
- `InfoTreeDock` 的公开 API **不变**：`setRootPath()` / `rootPath()` / `openFileRequested(path)`。

**⚠ 本轮踩到的最重要的一条：proxy 的 `QModelIndex` 跨源模型变更即失效。**

`QSortFilterProxyModel` 把行映射放在堆对象里，并把它们的**裸指针**存进每个 proxy index 的
`internalPointer`。源模型一变（`QFileSystemModel` 每读一个目录就是一次 `rowsInserted`），
proxy 就把这些对象全部丢弃重建 —— 变更前拿到的 index 于是指向**已释放内存**，下一次用它就崩：

```
ASSERT: "it != source_index_mapping.constEnd()"   qsortfilterproxymodel.cpp:272
```

所以整棵树**以路径为唯一稳定身份**：`FileTreeProxyModel::indexForPath(path)` 现取现用，
`InfoTreeDock` 与测试都不缓存跨变更的 proxy index。

**已证伪的假设（别再走一遍）**：`QAbstractProxyModel::canFetchMore()/fetchMore()`
**自己会 `mapToSource()` 再转发**，所以 `QSortFilterProxyModel` 的默认实现是对的，
**不需要**覆写它们。（第一版覆写了，白写。）

**Qt 6.11 的事实**：`QFileSystemModel` 头文件在 `include/QtGui/qfilesystemmodel.h`（不是 QtCore）；
`FilePathRole = Qt::UserRole + 1`；**没有** `isDirLoaded()`（Qt 5 私有 API）；
子目录必须 `fetchMore()` 才读；`directoryLoaded` 只在异步读完后发；
`setRootPath()` 之后**顶层（无效 index）下是机器的盘符**（`系统 (C:)`），
而 `fs.index(rootPath)` 是顶层 index 的 row 0 —— 两者会撞车，
所以 `mapFromSource(fs.index(rootPath))` 在某些层级会拿到**无效 index**，导航要按路径走。

---

### M2-2　面板系统换原生 `QDockWidget` + `saveState`

**用户拍板的两点**：① 彻底删掉 `V*Page` 适配层，`RightPanelDock` 直接持有原始面板；
② 布局持久化做**保存 + 恢复 + 重置三件套**。

#### 删掉了什么

`src/ui/dock/` **整个目录**（12 文件 1487 行）已进回收站：
`VDraggableTabWidget` / `VDragManager` / `VDropIndicator` / `VTabPageBase` / `VPages` /
`VRenderPath` / `VCompositeStrategy` + `tests/tst_VDraggableTabWidget.cpp` +
`tests/tst_VTabPageBase.cpp`。

**为什么该删**：那套自研拖拽层**0% 可用**（审计 UI-01~05）——
`new QDockWidget(this)` + flags=0 导致浮动永远浮不起来；`e->pos()`（局部坐标）当全局坐标
传给 `mapFromGlobal` → drop 恒 `insertIdx=-1`；`m_pages` 与 `QTabBar` 两套索引当一套用；
`movePageTo` 不 rebind host → 跨窗口移动后裸指针 UAF。

**顺带消除的循环依赖**：`vistella_media` ↔ `vistella_ui_dock`（审计 §7.8 / INV-DEP-1/2）。

#### 现在的样子

```
RightPanelDock (QWidget)
└── QSplitter (Vertical)
    ├── QTabWidget 颜色组   → ColorTab / SwatchTab / GradientTab / PatternTab
    ├── QTabWidget 属性组   → PropertiesDock / AdjustmentPanel
    └── QTabWidget 图层组   → LayersDock / ChannelPathPanel / HistoryDock
```

`RightPanelDock` 直接持有 9 个真实面板，accessor 改名为
`colorTab()` / `swatchTab()` / `gradientTab()` / `patternTab()` /
`propertiesDock()` / `adjustmentPanel()` / `layerDock()` / `channelPathPanel()` / `historyDock()`，
外加 3 个 group accessor（`colorGroup()` / `propertyGroup()` / `layerGroup()`）。

**4 个 deprecated 空壳 accessor 已删**（原来返回 nullptr + log 警告）：
`propertiesDock()` / `layersDock()` / `channelPathPanel()` / `historyDock()`
→ 前三个变成真实实现，`layersDock()` 改名 `layerDock()`（"layerDock" 更准，它是图层面板不是 dock）。

#### ⚠ 本轮踩到的两条：原生 `QTabWidget` 不等于旧的自研 widget

**1. `QTabWidget::sizeHint()` 返回的是「所有页里最大的那个」，不是「当前页」。**

旧的自研 `VDraggableTabWidget::sizeHint()` 是「tab bar + **当前页**的 hint」。
换成原生 `QTabWidget` 之后，属性组同时装着 PropertiesDock（矮）和 AdjustmentPanel（高），
聚合 hint 取高的那个 → 属性组即使显示矮页面也占掉大半面板。
解法：子类 `CurrentPageTabWidget` 覆写 `sizeHint()` / `minimumSizeHint()`，跟当前页走。

**2. `sizePolicy` 的 `Maximum` 会把 widget 卡在 sizeHint 上，stretch factor 救不了。**

`QSplitter::setStretchFactor(layerGroup, 1)` 只是"优先分给它"，
但 `QSizePolicy::Maximum` 的**硬上限就是 sizeHint** → 图层组永远只有 48px。
解法：内容尺寸的两组用 `Maximum`，**图层组必须用 `Expanding`**。

实测（dock 900px 高）：颜色 90 / 属性 150 / 图层 648 —— 图层组吃剩余。

#### 布局持久化三件套（`src/app/DockLayoutManager.*`）

| 文件 | 作用 | 谁写 |
|---|---|---|
| `<AppDataLocation>/layout_current.ini` | **当前**排布（活布局） | 退出时 `closeEvent` |
| `<AppDataLocation>/layout_default.ini` | **用户声明的默认**排布 | 菜单「视图 → 保存当前布局为默认」 |

- `save()` —— 退出时落盘（`QSaveFile`，原子写）。
- `restore()` —— 启动时恢复（`MainWindow` 构造末尾，**必须在所有 dock 建好之后**，
  `restoreState()` 按 objectName 找回 dock，dock 还没建时调用会静默失败）。
- `reset()` —— 回到用户声明的默认（菜单「视图 → 重置布局」）。
- `saveAsDefault()` —— 把当前排布声明为默认。

**关键区分**：`reset()` 回的是**默认**，不是**活布局**。
「我挪了个面板」（活布局，退出时存）和「我要回到原来的排布」（默认）是两件事，
所以必须是两个文件。测试 `reset_doesNotGoBackToTheLiveLayout()` 专门盯这条。

**修掉了 APP-19**：老 `onResetLayout()` 读 `workspace_default.ini`，
**全项目没有任何地方 `saveState()` 写它** → 重置布局功能实际是坏的，永远只弹一句报错。

### M2-4　工具补齐（`src/media/tools/`）

这一轮把审计 §5.3 列的整类缺失工具补齐了，并把 4 个"有但错"的工具修好。

#### 新增的工具（8 类）

| 工具 | 文件 | 行为 |
|---|---|---|
| 橡皮擦 | `EraserTool.*` | 4 通道图层清 alpha，3 通道图层写背景色（PS 同款） |
| 渐变 | `GradientTool.*` | 线性 / 径向 / 角度三种，前景色 → 背景色，选区裁剪 |
| 油漆桶 | `PaintBucketTool.*` | 4 连通扫描线填充，容差可调，选区遮罩 |
| 加深 / 减淡 | `DodgeBurnTool.*` | exposure 近似，量可调 |
| 模糊 / 锐化 / 涂抹 | `BlurSharpenSmudgeTool.*` | 三合一，模糊只算笔刷区域（全画布滤镜太慢） |
| 抓手 | `ViewTools.h`（`HandTool`） | 平移画布 |
| 缩放 | `ViewTools.h`（`ZoomTool`） | 点击缩放，Alt 点击缩小 |
| 旋转视图 | `ViewTools.h`（`RotateViewTool`） | 旋转**视图**（不转像素） |

全部写进**活动图层**的像素（不是合成缓存），走 `activeLayerImage()` + `pushLayerImageEdit()`
+ `commitLayerEdit()` 的撤销契约。

#### 修好的工具（4 个）

| 缺陷 | 根因 | 修法 |
|---|---|---|
| 形状 / 钢笔画的图形**完全看不见** | `LayerStack::rasterize()` 按 `sz/100` 缩放路径，假设路径在 0-100 归一化空间，而工具写的是**像素坐标** | 矢量路径坐标契约改成**图像像素坐标**（1:1 绘制）；矢量层返回 BGRA 而非 BGR（否则透明区变不透明黑） |
| 键盘事件**零调用者** | `ToolContext::onKeyPress()` 没有调用者，`ImageCanvas::keyPressed` 信号没有订阅者 | `ImageCanvas::keyPressEvent/keyReleaseEvent` 转发给 `ToolContext` |
| 裁剪的宽高比**从不生效** | `m_aspect` 只在选项面板被赋值，`computeClampedRect` / `applyCrop` 从不读它 | 拖拽中真的按宽高比约束；combo 用 `findData` 取枚举（之前把索引当枚举值） |
| 画笔选项**不可达** | 路由条件是"选中画笔 && mosaic 工具存在"，而 `m_mosaic` **恒非空** → 永远显示 Mosaic 页，下面画笔分支成死代码 | 条件改成"选中画笔 && **马赛克模式开着**" |

#### ⚠ 本轮踩到的最重要的一条：工具和画布抢左键

`ImageCanvas` 常开 `QGraphicsView::ScrollHandDrag`，所以左键拖动**永远**是平移画布。
移动工具要拖图层根本抢不过它 —— 事件已经到 view 了，画布已经在滚了。

修法不是"在工具里跟平移较劲"，而是给 `ToolState` 加一个**意图声明**：

```cpp
// src/media/tools/ToolState.h
virtual bool wantsExclusiveMouse() const { return false; }
```

默认 `false`（不抢左键的画笔 / 吸管 / 形状工具照旧能平移画布），
需要拖东西的工具（移动工具 / 抓手 / 缩放 / 旋转视图）返回 `true`，
`ImageWindow::eventFilter` 在**转发给工具之后**消费掉这个 press → view 收不到 → 不启动平移。

#### 工具组（长按弹出）

`ToolGroupButton`（`src/media/tools/ToolGroupButton.*`）：一组一个按钮，
右下角小三角标记"还有兄弟工具"，**长按 500ms** 弹出同组工具，
弹出的工具成为该组的新默认（PS 行为）。7 组：

选框(矩形/套索/魔棒) · 修复(仿制/修补/污点/红眼) · 画笔(画笔/橡皮擦/蒙版画笔) ·
形状(形状/钢笔) · 填充(渐变/油漆桶) · 色调(加深减淡/模糊锐化涂抹) · 视图(抓手/缩放/旋转视图)

#### ⚠ ToolId 的值 = ImageOptionBar 的页索引

`ToolMediator::ToolId` 的整数值**就是** `QStackedWidget` 的页索引。
所以：

- 枚举**只能在末尾追加**（改了中间的值 = 改了别的工具的页）；
- 每个新工具都必须在 `ImageOptionBar.ui` 里加一个**占位页**，
  否则 `insertWidget()` 会把后面的页挤走；
- 加了 8 个工具 → 占位页从 17 个变成 25 个（`tst_ImageOptionBar` 盯着这个数）。

#### 共享的坐标钳制 helper

`src/media/tools/ToolGeometry.*`：`canvasSize()` / `clampToCanvas()` / `clampRectToCanvas()` /
`clampDeltaToCanvas()`。每个工具自己发明一套边界规则，是"这个工具坏了"而不是
"这个工具用了错的边界"这类 bug 的根源。

**命名空间坑**：`class ImageWindow;` 的前向声明必须写在 `namespace tools {}` **外面**，
否则被解析成 `tools::ImageWindow` → C2027。

---

## 3. 尚未开工的部分

| 子项 | 状态 | 关键事实 / 入口 |
|---|---|---|
| ~~**M2-1** 自定义 `.vst` 工程格式 + 自动保存 + 崩溃恢复~~ | ✅ **已完成**（见第 5 节） | `media/project/VstProject.{h,cpp}` + `media/project/AutoSave.{h,cpp}`；`.vst` 按扩展名分流，其它扩展名仍存扁平合成图。 |
| ~~**M2-5c** 椭圆 / 多边形选区工具~~ | ✅ **已完成**（见第 6 节） | `media/selection/SelectionStrategy.cpp` + `media/tools/EllipseSelect.{h,cpp}` + `PolygonSelect.{h,cpp}`。 |
| M2-4 尾巴 | ✅ **已完成**（见第 7 节） | 旋转中心 / 缩放到矩形 / 图案图章 / 历史记录画笔 / 选区裁剪单一真源，全部落地。 |

**M2 阶段 8 个子项全部落地。**

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
- **原生 `QTabWidget` 的两个坑**（M2-2 踩的）：
  ① `sizeHint()` 返回**所有页里最大的**那个，不是当前页 → 要跟当前页走必须自己覆写；
  ② `QSizePolicy::Maximum` 的硬上限就是 `sizeHint()` → 想让某个 widget 吃剩余空间，
     它的 policy 必须是 `Expanding`，光给 `QSplitter::setStretchFactor` **不够**。
- `QMainWindow::restoreState()` 按 dock 的 **objectName** 找回每个 dock →
  调用时 dock 必须已经建好，否则静默失败（不报错，只是布局没变）。
  `restoreState()` 在 dock 集合与保存时不一致时返回 `false`。
- `DockLayoutManager` 的**活布局**与**默认布局**是两个文件：
  `reset()` 回的是**默认**，不是活布局。「我挪了个面板」和「我要回到原来的排布」是两件事。
- **M2-4 的工具事实**：
  - `ToolMediator::ToolId` 的整数值**就是** `ImageOptionBar` 的 `QStackedWidget` 页索引
    → 枚举只能末尾追加；每个新工具必须在 `ImageOptionBar.ui` 加占位页。
  - `ToolState::wantsExclusiveMouse()`：返回 true 的工具会**消费掉**鼠标 press，
    view 收不到 → 画布不启动 `ScrollHandDrag` 平移。默认 false。
  - `ToolGeometry::clampToCanvas()` / `clampRectToCanvas()` / `clampDeltaToCanvas()`：
    所有工具共用的边界规则。`clampDeltaToCanvas` 只把 delta 限到画布大小，
    **不会**把满画布图层的 delta 钳到 0。
  - 矢量路径的坐标契约 = **图像像素坐标**（1:1）。`Layer::Vector` 的 `rasterize()`
    返回 **BGRA**（4 通道），跟 `Layer::Text` 一致。
  - `ToolGroupButton` 长按 **500ms** 弹出；鼠标移动超过 **4px** 取消长按。
  - `ToolOptionsPanel` 是 5 个新工具共用的选项页（按 `Kind` 显示/隐藏控件）。

---

## 5. M2-1：`.vst` 工程格式 + 自动保存 + 崩溃恢复

### 5.1 容器格式

`.vst` = 32 字节头 + **JSON 清单** + **二进制负载**：

```
0  .. 7   magic "VSTPROJ\0"
8  ..11   manifestOffset
12 ..15   manifestLength
16 ..19   payloadOffset
20 ..23   payloadLength
24 ..27   magic2 ("VSTP")
28 ..31   formatVersion
```

- **清单**（JSON）放**结构**：画布尺寸 + 每层的 kind / name / opacity / blend /
  visible / locked / linked / 文字 / 矢量路径 / 调整参数。
- **负载**放**大块数据**：图层像素、像素蒙版、LUT。

为什么不用 ZIP：`QZipReader` / `QZipWriter` 在 Qt 6.11 里是**私有 API**
（`QtCore/6.11.1/QtCore/private/qzipreader_p.h`），链不上。与其为一个容器
引一个 zip 依赖，格式自带。

`QPainterPath` 的序列化走 `QDataStream` 的 `operator<<`——
**这是 Qt 6 里唯一公开的路径序列化接口**：`writeSvg()` / `readSvg()` /
`elementList()` 全是私有（`QtGui/qpainterpath_p.h`），`QPainterPathIterator`
在 6.11 已被移除。这三条路都试过。

### 5.2 保存 / 读回

- `ImageIOController::isProjectPath()` 按**扩展名**分流：`.vst` 存整个工程，
  其它扩展名仍存扁平合成图（用户要 PNG 就给 PNG）。
- `ImageWindow::loadProjectFile()` 读回时**替换**整个图层栈（不是合并）。
- 图片路径和工程路径共用同一套接线 `setupAfterLayerStackReady()`
  （智能对象监听器 + 图层面板 + 面板信号接线 + 选区接线 + 属性面板）。
  以前这套东西内联在 `loadFile()` 里，工程路径没法复用，而复制 900 行接线
  是另一种坏——现在是**两个入口，一套 setup**。

### 5.3 自动保存 + 崩溃恢复

`media/project/AutoSave.{h,cpp}`：

- 文档有未保存改动时，每 5 分钟写一份**恢复副本**。
- 恢复副本**不覆盖用户自己的文件**——那份文件存的是用户上次主动保存的东西，
  偷偷覆盖等于销毁用户特意保留的状态。
- 副本用 `.vst` 格式（整个图层栈都在）。恢复副本丢图层 = 半个恢复。
- 原子写（`QSaveFile`）：写一半崩溃不会留下截断副本。
- 文档正常保存后副本被删（`ImageWindow::markSaved()` → `removeRecovery()`）。
- `findRecoverable()` 只挑**比文件本身新**的副本。比文件旧的是陈旧噪音，
  不是崩溃现场。

---

## 6. M2-5c：椭圆 / 多边形选框

- `EllipseSelectionStrategy`：拖出矩形，椭圆**内接**于矩形——矩形的四个角
  在椭圆**之外**。这是椭圆框和圆角矩形的区别。
- `PolygonSelectionStrategy`：单击加顶点、双击闭合；**2 个顶点不算多边形**
  （那是条线）。
- 选区羽化从 Lasso 里抽成共享的 `applyFeather()`（积分图盒式模糊），
  三个 marquee 共用一份，不再是三份可能走偏的拷贝。

---

## 7. M2-4 尾巴

### 7.1 视图变换只有一个写入方

以前**两个**：缩放在画布里（`applyTransform()` = `resetTransform()` + `scale`），
旋转在工具里（直接 `setTransform()`）。两个都不认识对方：

- `setTransform()` 替换整个矩阵 → 旋转把缩放冲掉；
- `applyTransform()` 开头是 `resetTransform()` → 之后任何一次缩放把旋转冲掉。

现在**画布拥有**旋转（`m_rotationDeg`），矩阵由 `applyTransform()` 从缩放和
旋转一起重建：

```
translate(centre) * rotate(angle) * translate(-centre) * scale(zoom)
```

绕**图像中心**转，不是绕原点甩。另外补了缩放到矩形
（`ImageCanvas::zoomToRect()`）和旋转角度累加（第二次拖动叠加，不是归零）。

### 7.2 图案图章 / 历史记录画笔

- **图案图章**（`PatternStampTool`）：Alt+单击抓一块源图案，拖动时按间距平铺。
  源图案是**快照**（从活动图层像素拷出来的），不是活引用——否则图案会自己
  涂自己。
- **历史记录画笔**（`HistoryBrushTool`）：把一份合成快照画回活动图层。
  注意撤销栈只存**改动区域的 patch**（M1-7 的内存优化：24MP 图层一个笔画
  从 288MB 降到 bbox 大小），拿不出「第 N 步的完整文档」，所以历史画笔的源
  是用户显式抓的快照。

### 7.3 选区裁剪只有一处

`ImageWindow::pushLayerImageEdit()` → `clipEditToSelection()` 已经统一裁剪
（羽化边缘按 alpha 混合）。但渐变和油漆桶**又自己裁了一遍**，而且是**硬切**
（`maskRow[x] == 0` 就跳过）：

- 两个裁剪器两套规则；
- 羽化边缘上硬切的那个赢了——像素停在旧颜色，中央裁剪再 old→old 混合，
  羽化效果整个丢失。

现在工具里**不再**裁剪，裁剪只由中央那一处做。

---

## 8. 门禁（`docs/CONTRACTS.md` 优先级最高）

```powershell
$py='C:\Users\tongl\AppData\Local\Programs\Python\Python313\python.exe'
Set-Location 'D:\Collide\MyCode\QT6Projects\MultiDoc'

# configure + build
cmd /c 'D:\Collide\MyCode\QT6Projects\MultiDoc\tools\build.bat'

# 全量 ctest（当前 89 条）
cmd /c "set PATH=D:\QT6\6.11.1\msvc2022_64\bin;%PATH% && cd /d D:\Collide\MyCode\QT6Projects\MultiDoc\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug && ctest -j 4 2>&1"

# 契约门禁（不可与 ctest 并行：破坏自检会临时改源文件）
& $py tools\check_contracts.py
& $py tools\run_all_break_checks.py   # 一次跑完 11 个破坏自检
```

**本轮基线**（M2-1 / M2-4 尾巴 / M2-5c / R4 收敛 / blob 命名之后）：
build OK · ctest **89/89** ·
`check_contracts` **ERROR=0 WARN=21**（R1 18 / R4 3）·
**11 个破坏自检**全部注入点变红且文件逐字节还原
（contracts / M1-7 evict / M2-6 preview / M2-6 transform / M2-7 filetree /
M2-2 docklayout / M2-4 vectorlayer / M2-4 tools / M2-4 newtools /
**M2-4 tail** / **M2-1 project**）。

> WARN 从 22 降到 21：`A-REFRESHDUP` 这条重复刷新债务本轮结清（见第 11 节），
> 少了一条存量 WARN。

> ctest 条数：M2-2 之后 86 → M2-4 加 `tst_M2_4_Tools` → 87 →
> M2-1 加 `tst_M2_1_Project` → 88 → M2-4 尾巴加 `tst_M2_4_Tail_Tools` → **89**。

**⚠ 破坏自检必须检查"哪条测试变红"，不能只检查"有测试变红"。**

M2-4 的 newtools 自检第一次跑的时候，7 个注入点里 6 个"通过"了 —— 但其中 2 个
报红的是**渐变测试**，不是它们该报的模糊/加深减淡测试。原因有两个叠在一起：

1. **注入没有真的改到被测代码路径**（加深减淡那条：测试只查了静态
   `burnChannel()` 函数，没查工具真的调用它 → 注入"烧变亮"这个 bug 测试看不见）。
2. **陈旧的 build 产物**让一次运行的二进制跟源码对不上。

修法两条：
- 每个注入点都写明**它必须让哪条测试变红**，脚本核对**测试名**而不是只看红/绿
  （`tools/break_check_m2_4_newtools.py` 的 `expect_red(label, expected_test, out)`）。
- 注入的是**成因**，不是"看起来像成因的东西"——加深减淡那条的修法是给测试补上
  **真实图层上**的 burn 路径断言（原来只有静态函数的断言）。

**⚠ 判红不能写成 `expected_test in out`。**

Qt Test 在 **PASS 行和 FAIL 行都会打印测试名**，所以 `in out` 对全绿输出同样成立
—— 这是**门禁自己的假绿**。M2-4 尾巴自检第一次跑就是这个毛病：7 个注入点里 3 个
报 `OK: the expected test went red`，但同一次输出是 `Totals: 19 passed, 0 failed`。
正确写法是**只从 `FAIL!` 行里取名字**：

```python
failed_lines = [ln for ln in out.splitlines() if "FAIL!" in ln]
```

判断一条门禁会不会假绿的方法：**什么都没坏的时候它会不会报 OK**。

---

## 9. 工作区约定（别再踩）

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
14. **`QSortFilterProxyModel` 的 `QModelIndex` 跨源模型变更即失效**（内部 mapping 被丢弃重建，
    旧 index 指向已释放内存 → `ASSERT: "it != source_index_mapping.constEnd()"`）。
    用 `QSortFilterProxyModel` 时**以路径为稳定身份**，index 现取现用，不要缓存
    （M2-7b 踩过，源码注释里也写了一份）。
15. **破坏自检的锚点必须是 ASCII**。中文注释一旦被翻译成英文，锚点就失效，
    脚本会报 "anchor not found" 而**不是**真的检出 bug —— 而且这个失败会被误当成
    "脚本坏了"。M2-6a 的预览自检就踩了这个（M2-6b 把注释改成英文后锚点失配）。
16. **断言要等到效果真的发生**。M2-6a 的 `repeatedPreview_neverTouchesTheUndoStack()`
    原来在 preview 之后**立刻**查撤销栈，而注入的 bug 是"启动 200ms debounce 定时器"
    —— 命令 200ms 后才入栈，所以断言看不到，测试一直是绿的。
    现在改成 `QTest::qWait(400)` 之后再查（并再等一次确认 cancel 之后也不冒出来）。
17. **破坏自检必须注入"真的会被检出"的缺陷**。M2-6a 的预览自检原先注入的是
    "preview 启动 debounce 定时器" —— 但那**根本不是 bug**：定时器只是触发器，
    真正决定要不要推撤销命令的是 `m_pendingValid[tab]`，而 preview 路径一直把它置 false。
    所以注入后测试仍然是绿的。已改成注入 `m_pendingValid[tab] = true`（真的会让
    200ms 后推出撤销命令）。**注入点要对着"效果的成因"，不是对着"看起来像成因的东西"。**
18. **换原生控件 ≠ 行为等价**。M2-2 把自研 `VDraggableTabWidget` 换成原生 `QTabWidget`，
    看着是"换个类"，实际丢了两个行为：`sizeHint()` 跟当前页（原生返回所有页里最大的）、
    widget 能突破 sizeHint 长大（`Maximum` policy 卡死上限）。
    **换控件后必须逐条比对新旧的行为差异，不能只看"能编译"。**
19. **布局类测试必须给每个用例独立目录**。布局文件落盘，多个用例共用一个临时目录时，
    前一个用例写的文件会让后一个"期望没有文件"的用例失败 —— 于是测试结果取决于用例顺序。
    M2-2 的 `tst_M2_2_DockLayout` 给每个用例一个 `freshDir()`。
20. **删掉文件后，引用它的门禁脚本也要一起改**。M2-2 删了 `src/ui/dock/VDragManager.h`，
    `tools/break_test_contracts.py` 的 R1 注入点正是它 —— 不改的话破坏自检会直接
    "anchor not found"，看起来像脚本坏了而不是"目标被删了"。
21. **破坏自检必须核对"哪条测试变红"，不能只核对"有测试变红"**。
    M2-4 的 newtools 自检第一次跑，7 个注入点里报红的却是**别的**测试
    （渐变测试替模糊测试报了红）。两个原因叠在一起：① 注入没改到被测代码路径
    （加深减淡那条只查了静态函数，没查工具真的调用它）；② 陈旧的 build 产物
    让二进制跟源码对不上。修法：每个注入点写明**它必须让哪条测试变红**，
    脚本核对**测试名**（`expect_red(label, expected_test, out)`）。
22. **断言的探针点必须能区分"修好的实现"和"坏掉的实现"**。
    M2-4 的加深减淡测试原来只断言静态 `burnChannel()`，所以"工具把 burn 写成 dodge"
    这个 bug 看不见 —— 现在补上了**真实图层上**的 burn 路径断言（探针必须落在一个
    本来就**不是黑色**的像素上，否则"变暗"和"本来就是黑"分不开）。
23. **门禁脚本之间的 build 会互相踩**。破坏自检一个一个跑（`tools/run_all_break_checks.py`
    一次跑完 11 个），**不要**并行 —— 两个脚本同时改同一个源文件 / 同时重建会互相踩。
24. **判红不能写成 `expected_test in out`**。Qt Test 在 **PASS 行和 FAIL 行都打印测试名**，
    所以 `in out` 对全绿输出同样成立 —— 这是**门禁自己的假绿**。M2-4 尾巴自检第一次跑
    就有 3 个注入点报 `OK: the expected test went red`，而同一份输出是
    `Totals: 19 passed, 0 failed`。正确写法是**只从 `FAIL!` 行里取名字**：
    `failed_lines = [ln for ln in out.splitlines() if "FAIL!" in ln]`。
    判断一条门禁会不会假绿的方法：**什么都没坏的时候它会不会报 OK**。
25. **Qt 6.11 的几个序列化接口是私有的，写文件格式前先确认**：
    - `QZipReader` / `QZipWriter` → 私有（`QtCore/private/qzipreader_p.h`）
    - `QPainterPath::writeSvg()` / `readSvg()` / `elementList()` → 私有（`QtGui/qpainterpath_p.h`）
    - `QPainterPathIterator` → 6.11 已移除
    - **`QDataStream &operator<<(QDataStream&, const QPainterPath&)` → 公开**（`.vst` 用的就是它）
    - `QPainterPath::writeSvg` 的替代：`QDataStream`（无损，保住控制点和 double 精度）
26. **`.vst` 的清单里存的是"文档画布尺寸"，不是"底图尺寸"**。`LayerStack::canvasSize()`
    优先返回**第 0 层**的图像尺寸（LYR-05：独立用 LayerStack 时以第一层为准），
    而裁剪/缩放之后文档尺寸是显式设的、跟底图不一样。工程格式要的是后者 →
    用 `LayerStack::documentCanvasSize()`。

27. **增量门禁的"新增"判据是"计数比基点高"，不是"这行被改过"。**
    破坏自检往里注入时，注入份数必须**按基点算**：收敛过的文件当前计数可能
    远低于基点，固定 +1 根本够不到阈值，门禁只报 WARN 不报 ERROR，自检就假绿。
    `break_test_contracts.py` 的 R4 注入现在按 `基点 - 当前 + 2` 算份数
    （+2 而不是 +1：锚点块里原本有 1 行调用，涨的是 `copies - 1`）。

28. **收敛一个重复调用时，先确认剩下的"单独调用"是不是故意的。**
    把成对的调用合并进单一入口后，剩下那些**只出现一半**的调用各有各的理由
    （信号处理器只标脏、预览路径只重绘），不能顺手一起改掉。

---

## 10. 已登记的开放债务（`docs/DEBT.md`）

M2 关闭的：`A-ADJUSTPREVIEW`（M2-6a）、`A-HUESATLUT`（M2-6b）、`A-MEDIADISPATCH`（M2-7a）、
`A-FILETREEMODEL`（M2-7b）、`A-VTABAPI` / `A-DRAGTHRESHOLD` / `A-STACKWIDGET`（M2-2）、
`A-MOVE` / `A-KEYBOARD` / `A-CROP` / `A-BRUSHOPTIONS` / `A-TOOLICON` /
`A-TOOLGROUP` / `A-TOOLSET` / `A-VECTORCOORD` / `A-CROPASPECT`（M2-4）、
`A-VIEWTRANSFORM` / `A-TOOLTAIL` / `A-SELECTCLIP`（M2-4 尾巴）、
`A-MARQUEE`（M2-5c）、
`A-PROJECTFORMAT` / `A-AUTOSAVE` / `A-VSTBLOB`（M2-1）、
`A-REFRESHDUP`（刷新入口收敛，见第 11 节）。

M2 相关、仍然开放：

- `A-LAYERDRAG` / `A-TOOLCONFIG` / `A-SWATCH`（`A-SWATCH` 是色板/渐变/图案面板的假数据）
- `A-THEME`（M2-8 只修了暗色 QSS 硬编码，主题画廊的「应用」按钮仍是空实现）
- `A-FILTERMENU`（滤镜菜单 5 个入口仍 `setEnabled(false)`）

---

## 11. 结构性收尾：刷新入口收敛 + `.vst` blob 命名

### 11.1 刷新入口收敛（结清 `A-REFRESHDUP`）

R4 规则（契约 3.5）说：同一个文件里同一个刷新入口不该散落多处。上一轮抽
`setupAfterLayerStackReady()` 时往 `imagewindow.cpp` 里搬了 1 个
`invalidateCurrentCache()` + 1 个 `renderToView()`，计数从 46/43 涨到 47/44，
增量门禁直接报 ERROR。

修法不是"把这次多出来的那个调用删掉"——那是糊补丁。真正的修法是**把重复本身
消掉**：

- 新增私有 `ImageWindow::refreshCanvas()`：标脏 + 重绘，**全类只有这一份实现**。
- 37 处手写 `invalidateCurrentCache(); renderToView();` 全部改成调它。
- 公开的 `refreshCanvasNow()` 也转发到同一个实现。
- 剩下**故意只标脏不重绘**的 11 处（图层变更信号处理器、只改元数据的操作）
  和**故意只重绘不标脏**的 8 处（滤镜预览路径、异步渲染回调）保持原语义。

结果：`renderToView` 44 → 8，`invalidateCurrentCache` 47 → 11，都远低于基点，
R4 归零。**顺带把"漏掉一半就是静默陈旧帧"这个结构性问题消掉了**——以前
53 处手写组合，漏一处就是 bug；现在物理上写不出"只标脏不重绘"的错误组合。

> 剩下那 11 处单独的 `invalidateCurrentCache()` **不是**漏改：图层变更信号
> 每来一次就重绘一次太浪费，它们要的就是"标脏，等下一次渲染统一算"。
> 同理，滤镜预览路径的 `renderToView()` 不能配标脏——预览图不是文档。

### 11.2 `.vst` blob 命名：改用文件内 layer index

**原来的 bug**：blob 名字用 `layer.stableId`。而 `stableId` 来自**进程全局
计数器**，读回时又原样恢复 —— 于是两个 session 可以让两个**不同**的图层拿到
同一个 id（session A 存了 id 7；session B 新建图层，计数器又发 7；B 再存）。
blob 表按名字索引，两个图层的像素 blob 撞名，**一层的像素被另一层静默覆盖**。

**修法**：blob 名字改用**该图层在本文件里的下标**——它在这个文件内天然唯一。
`stableId` 仍然写在清单里（那是图层身份），只是不再拿来当 blob 名字。

回归测试：`tst_M2_1_Project::layersThatShareAStableIdKeepTheirOwnPixels()`
—— 直接把两个图层的 `stableId` 改成同一个值，断言两层各自拿回**自己的**
像素（用 ramp 图，不是纯色，否则撞名也看不出来）。

破坏自检：M2-1 自检新增第 9 个注入点（blob 名字改回用 `stableId`），
预期变红的测试就是上面那条。
