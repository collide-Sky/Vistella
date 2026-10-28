# 技术债与跨阶段前置登记表（DEBT）

> **依据**：`docs/CONTRACTS.md` §2.2（跨阶段前置协议）、§7（架构不变量）。
> **作用**：本文件是本项目**唯一**的「已知的、故意留着的、尚未完成的东西」清单。
> **强制规则**：
> - 源码里凡出现「占位 / 暂不 / 后续 / will wire / 留接口 / not wired」性质的承诺，**必须**在本文件有对应条目，否则按 `CONTRACTS.md` 规则 2.5 视为「没做」。
> - 源码中的占位注释**必须**写成下面这一行（格式由 `tools/check_contracts.py` 正则校验，写错会被拦下）：
>   ```cpp
   // TODO(P8.4): <还缺什么> -- see docs/DEBT.md#A-XXXX
   ```
>   - 阶段号必须与本表「建议阶段」一致；冒号后一句话说清缺什么；`-- see` 后的 ID 与本表逐字一致。
> - 「建议阶段」一列是 **AI 提出的排期提案，尚未经用户拍板**（`CONTRACTS.md` 规则 9.1：阶段划分由用户定）。用户可直接改这一列；`--stage` 门禁按本列执行「到期优先偿还」。> - 进入某阶段时，该阶段对应的条目**优先偿还**（`CONTRACTS.md` 规则 2.4），排在当阶段新功能之前。
> - 条目状态流转：`open` → `in-progress` → `done`（必须同时删除源码里的 TODO 标记）/ `wont-fix`（须用户批准并记入 §C）。

**初始建立**：2026-10-26　**来源**：全项目代码审计（394 条缺陷）+ 源码全文扫描（占位注释 20 处）
**审计依据**：`docs/CODE_AUDIT_2026-10-26.md`、`audit/modules/`

---

## §A　跨阶段前置登记

### A.0　阶段划分（提案，待用户拍板）

| 阶段 | 主题 | 主要覆盖 |
|---|---|---|
| `P8.4` | 像素真相与渲染管线 | `INV-PIXEL` / `INV-FMT` / 蒙版渲染 / 刷新收敛 / 渲染路径 |
| `P8.5` | 状态唯一 owner 与工具配置 | `INV-STATE` / 工具配置 / 实时预览 / 吸管 / 颜色链 / 快捷键 |
| `P8.6` | 撤销、图层与工程生命周期 | `INV-UNDO` / `INV-ID` / 自动保存 / 图层拖拽 / 通道面板 / 分发器 |
| `P8.7` | 画笔预设与视觉资产 | 画笔预设 / 色板·渐变·图案 / 液化 |
| `P8.8` | 工程格式与外部服务 | 工程文件格式 / 登录与 AI |
| `P8.9` | 依赖方向与死代码收口 | `INV-DEP` / 线程池 / `Result`·`Async` / GPU 后端 / 死 API |

> 排期规则：同一不变量偏差与其修复项**必须落在同一阶段**，不允许把「登记」与「偿还」拆到不同阶段。

### A.1　占位 API（声明完整、实现或接线缺失）

> 判据：public API 存在，但零生产调用点，或返回 nullptr 兼容层。
> 依据：`CONTRACTS.md` 规则 2.1（零调用者即不存在）。

| ID | 位置 | 缺什么 | 用户可见影响 | 影响的能力路径 | 建议阶段 | 验收方式 | 状态 |
|---|---|---|---|---|---|---|---|
| `A-SETCOMPOSITOR` | `LayerStack.h:314` | 通道归一化（1ch→BGR / 4ch→BGR）+ 异常自动回退 legacy 已经就位，灰度/带 alpha 文档不再全黑；但 `setCompositorEnabled()` 在设置页没有入口 → 用户遇到 compositor 渲染异常时没法手动切回 legacy 路径（05-35 / CP-01） | 灰度 PNG / 带 alpha PNG 已能正常打开；渲染异常时用户没有自救手段 | 渲染路径切换 | P8.4 | 设置页有渲染路径开关，切换后画布立刻换成 legacy 路径 | open |
| `A-TOOLCONFIG` | `ToolMediator.cpp:19-45` | `applyToolConfig/currentConfig/resetToolConfig/toolConfigApplied/toolConfigReset` 五个 API 零调用者 → 每个工具的设置**切走再切回全部丢回默认值**（04-20） | 调好画笔大小/羽化/容差 → 切到别的工具再切回来 → 参数全没了 | 工具选项持久化 | P8.5 | 切工具往返后参数保持 | open |
| `A-ADJUSTPREVIEW` | `AdjustDialogBase.cpp:67` | `preview(QVariantMap)` 信号零 connect → 曲线/色阶/黑白/通道混合器/HSL 五个对话框**无实时预览**（09-02） | 调曲线只能点 OK 才生效，来回试错 | 图像 → 调整 → 曲线/色阶 | P8.5 | 拖动滑块实时预览 | open |
| `A-RENDERBACKEND` | `RenderGraph.h:98` | `setBackend()` 零调用点 → 状态栏「Auto/Force GPU」对渲染**零影响**；`BackendCapabilities::isGpuAvailable()` 恒 false；readback 上下翻转（GPU-01/02/03） | 用户切「强制 GPU」毫无变化，状态栏骗人 | GPU 模式切换 | P8.9 | 切 GPU/CPU 渲染结果确实不同 + 状态栏与实际一致 | open |
| `A-MEDIADISPATCH` | `core/MediaDispatcher.cpp:112` | `MediaDispatcher::openFile` / `IModule::openFile` / `IWorkspace` 生产路径零调用 → `MainWindow` 另写两份「先 ImageWindow 后 DocWindow」启发式；`.mp3/.mp4` 走 `cv::imread` 失败后当文本整块读入（CI-19 / UI-12 / APP-18） | 打开视频/音频 → 满屏乱码，大文件卡死/OOM | 打开非图像文件 | P8.6 | 打开 .mp4 走视频模块或明确拒绝并提示 | open |
| `A-LAYERMASK` | `layers/LayerMaskRenderer` | `LayerMaskRenderer` 仅被 tests 调用 → 新 `LayerMask` 模型**完全不参与渲染**，蒙版只读旧字段（LYR-15 / CP-18） | 加/反转/羽化/密度/矢量蒙版**全都没有视觉效果** | 图层 → 蒙版 | P8.4 | 加蒙版后图层真的被遮住 | open |
| `A-CAMERARAW` | `imagewindow/ImageIOController.cpp:49` + `camera_raw/CameraRawLoader.h:49` | `ImageIOController::onOpen` **全项目无调用者**（`MainWindow` 直接 `img->loadFile`）→ RAW 文件根本进不了 Camera Raw；即使进去，`m_filePath` 变成临时 PNG，Ctrl+S 存到 `%TEMP%`（05-24） | 打开 .RAF/.CR2 → 走普通图像路径，色温滑杆无效 | Camera Raw | P8.6 | 打开 RAW 进 Camera Raw + Ctrl+S 覆盖原文件 | open |
| `A-EYEDROPPER` | `EyedropperTool.cpp:35-58` | `sampledColor()` 零消费者 → 吸管**只改状态栏文字**，不改前景色、不改画笔色、颜色面板不动（04-13 / 04-14） | 吸管取色后画笔还是黑色 | 吸管 → 前景色 | P8.5 | 吸管取色后画笔颜色跟着变 | open |
| `A-COLORDOCK` | `media/docks/RightPanelDock.h:130` | `RightPanelDock::colorDock()` 是 deprecated accessor，`return nullptr` + warning；而 `mainwindow.cpp:3408` 仍在调它 → 工具栏取色链**整条断着**；旧 `ColorDock` 类只被 `RightPanelStack` 持有，与工具栏无关（09-01 / 09-08） | 左侧工具栏改前景/背景色**完全无效** | 前景色/背景色 | P8.5 | 工具栏取色 → 画笔 + 颜色面板三者同步 | open |
| `A-VTABAPI` | `ui/dock/VTabPageBase.h:45` | `preferredRenderPath()/preferredCompositeStrategy()/contentWidget()/pageTitleChanged` 与 `setCompositeStrategy` 全工程无消费，注释却称「用户拍板/1 行接入」（UI-35） | 无直接用户影响，但渲染路径选择被架空 | 渲染路径 | P8.4 | 页面声明的渲染路径真的被采用 | open |
| `A-BRUSHPRESET` | `brushes/BrushPreset.h:66` / `BrushPickerDialog` / `AbrParser` | `BrushPresetManager` 无调用方，其 5 个方法 `addUserPreset`/`removeUserPreset`/`findByName`/`saveUserPresets`/`loadUserPresets` 也全部零调用者；ABR 解析器拒绝 v9/v10；画笔预设面板在 UI 中不存在（13 个内置预设无缩略图）（06-31 / 06-32） | 画笔选项栏看不到预设；导入 ABR 弹「将会实现」 | 画笔预设 | P8.7 | 画笔选项栏出现预设缩略图 + ABR 可导入 | open |
| `A-TRANSFORMBOX` | `transform/TransformBox.cpp:249` | `toTransform()` 只返回「平移到 topLeft」，无 scale/rotate/skew，**谁用谁错**；是 P0-6.1 遗留简化实现未删（06-22） | 无直接用户影响（当前无调用方），但留着会误导下一个开发者 | 自由变换 | P8.4 | 删除或委托 `computeTransform`，并有守护测试 | open |
| `A-IMAGEPROCASYNC` | `imageworker/ImageProcessorAsync` | 无生产调用者；`TaskContext` 参数被丢弃；无取消（03 报告） | 无 | 异步处理 | P8.9 | 删除或接线 | open |
| `A-THREADFACILITIES` | `core/ThreadPool` | `parallel_for` / `task_graph` / `RealtimeThread` / `SpscRing` / `SerialExecutor::run_sync` / 取消树 零调用点（§2.2） | 无 | 基础设施 | P8.9 | 删除或接线 | open |
| `A-RESULTASYNC` | `core/Result.h` / `core/Async.h` | `Q_TRY` 在 MSVC **编不过**（按文档用必然失败）；`Result.h`/`Async.h` 基本无人使用（CI-24 / CI-25） | 无 | 基础设施 | P8.9 | 删除或改写并接线 | open |
| `A-AUTH` | `user/` `network/` `ai/` | `AuthClient` 对任意输入返回 mock success；`UserManager` 密码参数被 `Q_UNUSED`；`AIManager`/`HttpClient` 全是 stub；`src/network` 连 `Qt6::Network` 都没链接（03 报告） | 登录对话框点了只弹提示；明文存 access/refresh token | 登录 / AI | P8.9 | 入口置灰或实现 | open |
| `A-TEXTEDITORFRAME` | `src/media/texteditorframe.cpp` | 整文件 260 行**不在任何 CMake target**，全仓库无 `TextEditorFrame` 声明（06-34） | 无 | — | P8.6 | 删除文件或加入构建 | open |
| `A-IMAGESAVESIGNAL` | `imagewindow/ImageIOController.h:35-40` | `ImageIOController` 的 3 个信号 (`filePathChanged`/`closeRequested`/`dirtyChanged`) 零 connect；`MainWindow` 侧靠 `ImageWindow::savePublic()` 代理转发（05-31 / 09 报告） | 另存为失败**静默返回**，用户不知道 | 另存为 | P8.5 | 失败有明确提示 | open |
| `A-QTBRIDGE` | `core/ThreadPool/backends/qt_bridge.hpp` | 整个文件**从未参与编译**：缺 `#pragma once`、namespace 写成 `multidoc::tp`（其余线程池代码一律 `vistella::tp`）、`TP_USE_QT` 在 `config.hpp` 默认 0，所以文件内全部代码被 `#if TP_USE_QT` 排除（CI-36） | 无直接用户影响，但它是**第二套线程池后端**的假象 | 线程池 | P8.9 | 删除文件或补齐并加入构建矩阵 | open |
| `A-DRAGTHRESHOLD` | `ui/dock/VDragManager.h:61` | `setDragStartThreshold(int px)` 零调用者 → 拖拽阈值不可配置（死 API） | 无 | 面板拖拽 | P8.9 | 接线或删除 | open |
| `A-LOGGERAPI` | `logger/Logger.h:172-177` + `:200-202` | 简化版 `trace`/`debug`/`warn`/`critical` 与 `setLevelQt`/`setLogDirQt`/`setModuleNameQt` 全部零调用者（日志宏走的是 `::vistella::log()`，两套 API 并存）；`info`/`error` 因与 `spdlog::info`/`spdlog::error` 同名而无法静态区分 | 无直接用户影响，但**运行时改日志级别/日志目录**没有任何入口 | 日志配置 | P8.9 | 设置页能实时改日志级别与目录，且旧 API 删除或接线 | open |
| `A-BRUSHCURSOR` | `media/imagewindow/ImageCanvas.h:52-53` | `setBrushCursorRadius()` / `moveBrushCursor()` 零调用者 —— 实际光标由 `ImageWindow::m_brushCursor` 直接操作（`imagewindow.cpp:333/2676`），`ImageCanvas` 这两个方法是**重复 owner**（违反 `INV-STATE-1`） | 画笔光标半径写死在 `imagewindow.cpp`，用户无法配置 | 画笔光标 | P8.5 | 光标半径可配置，且只剩一个 owner | open |
| `A-TEXTCOLOR` | `media/imagewindow/TextOverlayController.h:87` | `onColorSelected()` 零调用者 —— 文字取色实际由 `ImageWindow::onColorClicked` 走 `QColorDialog` 完成（`imagewindow.cpp:2381`），`TextOverlayController` 这条是**重复链路** | 文字面板改颜色与文字对象改颜色可能不同步 | 文字颜色 | P8.5 | 文字颜色只有一个 owner，面板与画布同步 | open |
| `A-STACKWIDGET` | `ui/dock/VDraggableTabWidget.cpp:156-158` | `insertPage()` 里**先** `m_tabBar->insertTab()` **后** `m_stack->insertWidget()`。空 tab bar 插入首个 tab 时 Qt 会自动 `setCurrentIndex(0)` 并 emit `currentChanged` → 此时 page 还没进 `m_stack` → `m_stack->setCurrentWidget(page)` 必然报 "widget not contained in stack"。每个 tab widget 只在加第一个页面时触发一次 → 右侧面板 3 个 sub-dock 正好 3 条。**根因是这两行的顺序** | 每次构造带右侧面板的窗口都刷 Qt 警告；首个 tab 的页面在栈为空时被跳过显示 | 右侧面板切页 | P8.5 | 构造 ImageWindow 零 Qt 警告（`tst_Path_OpenEditUndoRedoSave::constructing_window_emits_no_unknown_qt_warning` 转绿） | done |

### A.4　重复刷新逻辑（`CONTRACTS.md` 规则 3.5）

> 由 `tools/check_contracts.py` 的 R4 规则机械检出。数字会随代码变动更新。

| ID | 位置 | 重复次数 | 问题 | 用户可见影响 | 建议阶段 | 验收方式 | 状态 |
|---|---|---|---|---|---|---|---|
| `A-REFRESHDUP` | `src/media/imagewindow.cpp` | `renderToView` **44** 次 / `invalidateCurrentCache` **51** 次 | 53 处手写 `invalidate + render` 组合，**漏一处就是静默 bug** —— 12+ 个操作（刷新智能对象 / 切换嵌入链接 / 改图层名 / 应用智能变换）改完画面不刷新，要等下一次别的操作才变（05-13） | 改了图层/智能对象/蒙版后画面不更新 | P8.4 | 收敛为单一 `invalidateAndRender()` 入口；R4 规则归零 | open |
| `A-REFRESHDUP2` | `src/media/tools/Brush.cpp` | `renderToViewPublic` **0** 次 | 画笔改走 `ImageWindow::refreshCanvasNow()` 单一入口；笔画同时写进活动图层而不是合成缓存 | 笔画落笔路径与其它编辑路径行为一致 | P8.4 | 同上 | done |

### A.2　功能占位（接口在、功能不工作）

| ID | 位置 | 缺什么 | 用户可见影响 | 建议阶段 | 验收方式 | 状态 |
|---|---|---|---|---|---|---|
| `A-CROP` | `tools/Crop.cpp` | — | 裁剪走 `setDocumentImage()`（文档级几何变更），`ImageEditCommand` 的 undo 还原原图、redo 重放裁剪结果 | 拖框松手 → 图像尺寸真的变，Ctrl+Z / Ctrl+Y 都能用 | P8.5 | 裁剪后尺寸真的变 + 可撤销可重做 | done |
| `A-MOVE` | `tools/MoveTool.cpp:17` | 22 行空壳，注释「F-N will wire drag behavior」；不拖图层/文字/选区（04-18） | 移动工具只有状态栏提示，光标是 SizeAll 但拖不动 | P8.5 | 按 V 能拖当前图层/文字 | open |
| `A-BRUSHOPTIONS` | `ImageOptionBar.cpp:230` | `onToolChanged` 先判 `if (id==Brush && m_mosaic)` 而 `m_mosaic` 恒非空 → 永远显示 Mosaic 页，下面 Brush 分支成**不可达死代码**（04-05） | 选中画笔后**看不到任何画笔选项**，画笔锁死 13px 黑笔 | P8.5 | 选中画笔出现大小/硬度/不透明度/流量/预设 | open |
| `A-KEYBOARD` | `ToolContext.cpp:106` | `ToolContext::onKeyPress/onKeyRelease` **零调用者**；`ImageCanvas::keyPressed` 信号无订阅者 → 工具键盘逻辑全是死代码（04-07） | 多边形永远闭合不了、钢笔回车提交不了、Esc 取消不了、Shift 等比失效 | P8.5 | 键盘能完成工具关键操作 | open |
| `A-SHORTKEYS` | `mainwindow.cpp:494` | 12 个单键 QAction 用 `Qt::ApplicationShortcut`，优先级高于焦点 widget（04-08） | 用文字工具打字时 V/M/L/W/C/T/B/I 会切工具而不是输入文字 | P8.5 | 编辑态下这些键正常输入 | open |
| `A-LAYERDRAG` | `layers/LayerPanel.cpp:161` | 无 `setDragDropMode`、无 drop 事件；组子项只读（LYR-25 / LYR-31） | 图层不能拖拽排序/入组/出组 | P8.6 | 能拖拽排序 | open |
| `A-CHANNELPATH` | `docks/ChannelPathPanel.cpp:130` | `channelSelected`/`pathSelected` 无 connect；通道列表是 6 项硬编码静态标签（09-05） | 点「通道/路径」只有日志，什么都不发生 | P8.6 | RGB 分解 + 单通道可见性 | open |
| `A-SWATCH` | `docks/SwatchTab/GradientTab/PatternTab` | 15 色板/8 渐变/6 图案**全是硬编码假数据**；渐变/图案无对应工具（09-07） | 色板/渐变/图案面板是摆设 | P8.7 | 至少做到可读展示并标注未实装 | open |
| `A-LIQUIFY` | `filters/liquify/LiquifyDialog.cpp:105` + `LiquifyEngine.h:48` + `LiquifyMesh.h:56` | 6 个液化工具只有 1 个可达；面板参数被 `Q_UNUSED`；ONNX 路径写死 `D:/Collide/...`；结果红蓝互换（LQ-01/05/12、CP-12）；`LiquifyEngine::samplePixel` 与 `LiquifyMesh::setVertexDisplacement` 零调用者 | 液化面板点了没反应 | P8.7 | 6 个工具可用 + 路径不写死 | open |
| `A-AUTOSAVE` | `core/SessionManager` | 无自动保存；session 只存路径且只在干净退出时存（CI-07 / APP-16） | 崩溃后未保存的修图**全丢** | P8.6 | 崩溃恢复能找回未保存内容 | open |
| `A-PLUGINMENU` | `app/mainwindow.cpp:544` | 「增效工具」菜单是空壳（`aPluginsMgr->setEnabled(false)`），插件管理入口不生效 | 菜单里第 9 项点不动，插件体系没有入口 | P8.9 | 菜单能加载真实插件，或整项删除 | open |
| `A-FILTERMENU` | `app/mainwindow.cpp:1078/1090/1093/1096/1102` | 滤镜菜单 5 个入口（Neural Filters / 自适应广角 / Camera Raw / 镜头校正 / 消失点）全部 `setEnabled(false)` | 用户点这些滤镜没有任何反馈，看着像坏了 | P8.9 | 接通或移除这些入口 | open |
| `A-THEME` | `ui/themegallerydialog.cpp:131` + `themegallerydialog.h:39` | 「应用」按钮只弹 QMessageBox，`ThemeManager::setAccentColor/setMode/persistToSettings` 三行是注释，主题画廊不接 ThemeManager | 选了颜色点了应用，界面完全不变 | P8.9 | 点应用后主题真的变并持久化 | open |
| `A-TOOLICON` | `media/tools/LeftToolBar.cpp:28/31` | 9 个工具用自绘 SVG，剩下 6 个（Pen/Clone/Heal/Patch/RedEye/MaskBrush）仍用 QStyle 内置图标 | 工具栏图标一半是系统默认样式，和整体风格不一致 | P8.9 | 6 个工具补齐 SVG，或明确统一为同一套图标 | open |
| `A-ICCTIFFTAG` | `media/icc/IccTiffEmbed.cpp:206` | TIFF 的 ICC tag 条目里 value offset 写死成常量，嵌入的 profile 实际读不出来 | 导出带 ICC 的 TIFF 后再打开，颜色管理失效 | P8.9 | 导出的 TIFF 能被其它软件正确读到 profile | open |
| `A-HUESATLUT` | `media/imageprocessor.cpp:825` | `applyLut` 接受 hue/sat pairs 但只应用 lightness 的 1D LUT，色相/饱和度滑条**完全无效** | HSL 面板拖 Hue/Saturation 画面没反应 | P8.5 | Hue/Saturation 滑条真的改变画面 | open |
| `A-MOSAICBACKUP` | `media/imagewindow/MosaicTool.cpp` | — | `ensureBackup()` 现在快照的是**活动图层**的像素，涂抹落在图层上，撤销走 `LayerCommand::SetLayerImage` | 马赛克有真实备份链路，撤销能还原图层像素 | P8.4 | 马赛克前有真实备份且可恢复 | done |
| `A-TEXTOVERLAYAPPLY` | `media/imagewindow/TextOverlayController.cpp:244` | `applyStyleToCurrent()` 空实现，保留接口没有调用者 | 文字样式改动没有统一的生效入口 | P8.5 | 文字样式通过该入口生效，或删除该接口 | open |
| `A-PROJECTFORMAT` | `ImageIOController.cpp:68` | 保存写 `currentImage()`，**无任何工程格式** → 保存后重开只剩一张扁平图，图层/蒙版/智能对象/文字全丢（LYR-23 / §5.2） | 「非破坏修图」定位不成立 | P8.8 | 自定义 .vst 格式往返 | open |

### A.3　源码中的「以后会做」承诺（20 处散落注释，尚未登记）

> 这些是**原始证据**：`grep -rn "wiring|will wire|未接线|后续 P|留接口|暂不|待接线" src/` 的全部命中。
> 规则 2.3 要求把它们逐条转成上表条目并统一标记；标「待定」的仍是**未登记的占位**。

| 位置 | 承诺内容 | 归入 |
|---|---|---|
| `app/CMakeLists.txt:37` | ThreadPool v2 的 Backpressure/PoolStats 暂不 link | `A-THREADFACILITIES` |
| `ui/dock/CMakeLists.txt:3` | GPU/CPU wiring 留在 P8.2/P8.4 | `A-RENDERBACKEND` |
| `app/mainwindow.cpp:544` | 插件管理后续 P7.11 | `A-PLUGINMENU` |
| `core/IModule.h:68` | audioWorker 暂不支持 | `A-MEDIADISPATCH` |
| `media/imageprocessor.cpp:914` | 参数为 API 完整性保留，P0-3.x 接 AdjustmentLayer 用 | `A-HUESATLUT` |
| `media/imagewindow.cpp:1800` | `dpi=72` 硬编码，后续 P0-8 ICC embed 流程读 EXIF | 待定（色彩管理） |
| `camera_raw/CameraRawLoader.h:8` | 接口齐全，wiring 未做 | `A-CAMERARAW` |
| `compositor/nodes/LayerStackAdapter.cpp:245` | mask + blend wiring | `A-LAYERMASK` |
| `imagewindow/ImageIOController.h:35-40` | signals mirror，MainWindow wiring | `A-IMAGESAVESIGNAL` |
| `imagewindow/TextOverlayController.cpp:244` | 占位：保留接口 | `A-TEXTOVERLAYAPPLY` |
| `media/texteditorframe.cpp:101` | 保留接口以便将来扩展 | `A-TEXTEDITORFRAME` |
| `media/tools/LeftSidePanel.h:64` | accessors for downstream wiring | 待定 |
| `media/tools/LeftToolBar.cpp:27` | 后续 P5 接 SVG 图标 | `A-TOOLICON` |
| `media/tools/LeftToolBar.cpp:31` | 6 个工具暂用 QStyle 占位图标 | `A-TOOLICON` |
| `media/icc/IccTiffEmbed.cpp:206` | ICC tag 的 value offset 写死 | `A-ICCTIFFTAG` |
| `ui/themegallerydialog.cpp:131` | 主题画廊不接 ThemeManager | `A-THEME` |
| `layers/BlendMode.cpp:307` | Phase 2 暂不支持，fallback Normal | 待定（混合模式） |
| `media/tools/MagicWand.h:46` | P2.1 option UI hooks | 待定 |
| `layers/LayerPanel.cpp:296` | 暂不用，Phase 1 UI 不显示 saved 标记 | 待定 |
| `layers/LayerPanel.cpp:778,904,1146` | LayerCommand wiring（P0 leftover 3） | 待定（图层命令） |
| `media/tools/MoveTool.cpp:17` | F-N will wire drag behavior | `A-MOVE` |
| `media/tools/Text.cpp:303` | 后续 P0-7.x 完善 undo | 待定（文字 undo） |

---

## §B　架构不变量偏差登记

> 这些不是「以后会做」，是**当前就在破坏不变量**（`CONTRACTS.md` §7）。
> 状态：`violated` = 当前违反中；`planned` = 已排期。

| 不变量 | 当前状态 | 违反点 | 审计编号 | 影响的用户路径 | 建议阶段 | 状态 |
|---|---|---|---|---|---|---|
| `INV-PIXEL-2`（`m_current` 只读） | violated | `m_current` 同时是渲染缓存与可编辑画布；`setCurrentImage` 会写 `m_original` 并把合成图写进 base layer | 04-02 / 05-02 / 05-03 / 05-10 / 05-15 / 05-30 / 05-36 / CP-11 | 画笔 / 滤镜 / 调整 / 涂抹 / 保存 | P8.4 | planned |
| `INV-ID-1`（图层身份唯一） | violated | `LayerPtr` 地址 / `int` 下标 / `cv::Mat::data` 像素指针三种混用；`m_groups` 以地址为 key | LYR-06 / LYR-12 / LYR-16 / LYR-27 | 图层删除/重排/编组/智能滤镜 | P8.6 | planned |
| `INV-UNDO-1/2/3`（撤销架构统一） | violated | 4 种命令风格并存；除 Merge 外 30+ 个 `redo()` 是 no-op；undo 静默改字段不发信号不重绘；整图快照 144MB/笔 | LYR-07 / LYR-08 / 05-09 / 04-16 / 06-13 / 06-14 / 06-21 | Ctrl+Z / Ctrl+Y | P8.6 | planned |
| `INV-STATE-1`（状态唯一 owner） | violated | 前景背景色 3 份、工具配置 3 份、面板显隐 2 份、Quick Mask 3 份、选中图层 2 份、主题 2 份、窗口模式 2 份 | 09-01 / 04-14 / APP-05 / APP-12 / APP-14 | 颜色 / 工具 / 面板 / 主题 / 窗口最大化 | P8.5 | planned |
| `INV-LIFE-1/2/3`（生命周期） | violated | `invokeMethod(qApp,...)` + 裸 `this`（CI-03/05-04/LYR-10）；`ImageCanvas::m_box` 裸指针全项目无处置空（04-01）；选项页裸指针（05-05） | 04-01 / 05-04 / 05-05 / 05-06 / LYR-10 / UI-01 | 关 tab / Ctrl+T 后切工具 / 跨窗口移面板 | P8.4 | planned |
| `INV-FMT-1/2`（通道契约） | violated | 全链路 CV_8UC3；4 通道配 `Format_RGB888`；16bit `convertTo` 用 alpha=1 饱和 | 06-02 / 05-08 / 06-03 / 06-39 / 05-07 / LYR-01 / LYR-35 | 打开 RGBA/16bit/灰度图 | P8.4 | planned |
| `INV-DEP-1/2`（依赖方向） | violated | `vistella_media` ↔ `vistella_ui_dock` 循环 include + 互相 link；`ui` 反向依赖 `media` 的 GPU 符号；公共头泄漏 `opencv2/core.hpp` | §7.8 / UI-38 / 08 | 构建稳定性 | P8.9 | planned |
| 窗口状态唯一真源 | violated | `m_mode` 与 `windowState()` 双真相源；`changeEvent` 只同步 `m_isMaximized` 不同步 `m_mode`；`closeEvent` 强制 Maximized→Normal | APP-12 / APP-24 | 系统最大化/还原后点「□」无反应、记忆失效 | P8.5 | planned |

---

## §C　例外批准登记

> `CONTRACTS.md` §11：任何偏离必须由用户书面批准并登记在此。
> **AI 不得自行批准例外。**

| ID | 偏离内容 | 批准人 | 日期 | 影响范围 | 恢复条件 | 状态 |
|---|---|---|---|---|---|---|
| | | | | | | | |

（当前无例外）

---

## §D　维护规则

1. **新增占位必须同 PR 登记**：改了 `src/` 且引入任何「以后会做」的承诺 → 必须同时在本文件加行 + 源码写 `TODO(Px.y): <还缺什么> -- see docs/DEBT.md#<ID>`（格式见规则 5）。
2. **还债必须同 PR 删行**：实现某个条目 → 本文件状态改 `done` **且**源码里的 TODO 标记被删除，缺一不可（由 `tools/check_contracts.py` 双向检查）。
3. **阶段边界检查**：进入 P<x.y> 时，先处理本文件中建议阶段 = P<x.y> 的全部 `open` 条目（`CONTRACTS.md` 规则 2.4）。
4. **本文件不记 bug**：功能缺陷走审计报告与阶段能力清单，不进 DEBT。DEBT 只记「故意留着的」。
5. **标记格式唯一**：`// TODO(P<x.y>): <还缺什么> -- see docs/DEBT.md#<ID>`。破折号必须是两个 ASCII `-`，分隔符必须是 ` -- `（前面一个空格、后面一个空格）。写成 `—` 或省略 `<还缺什么>` 都会被门禁拦下。

---

## §E　存量注释违规清理（R2 规则 2.6a）

> `tools/check_contracts.py` 的 R2 规则禁止变更日志式注释。
> 规则是**新增即 ERROR**，但本仓库在规则建立之前已经积累了大量违规，因此分档清理。

| ID | 范围 | 数量 | 违规类型 | 建议阶段 | 验收方式 | 状态 |
|---|---|---|---|---|---|---|
| `A-COMMENTDATE` | `src/app/mainwindow.cpp` / `src/media/docks/LayersDock.h` / `RightPanelDock.{h,cpp}` / `src/media/imagewindow/ImageCanvas.h` / `imagewindow.cpp` / `layers/LayerPanel.cpp` / `tools/ImageOptionBar.{h,cpp}` / `tools/LeftSidePanel.{h,cpp}` / `src/ui/dock/VDraggableTabWidget.h` / `VPages.cpp` | **26** | 日期戳注释（`P8.3.2 (2026-10-26): ...`），来自提交 `34030c1` | P8.5 | `check_contracts.py --base origin/dev` 不再报 ERROR | done |
| `A-COMMENTSTAGE` | `src/ai/AIManager.{h,cpp}` / `src/network/AuthClient.{h,cpp}` / `src/user/UserManager.{h,cpp}` 等 | 约 200 | `阶段 N:` 式注释（"阶段 0 占位" / "阶段 5+ 才做"） | P8.6 | 对应文件内 `阶段 \d+` 命中归零 | done |
| `A-COMMENTREST` | 其余 `src/` | 约 1050 | 变更日志式 / 自称已完成 / 默认值声明 / 未登记占位承诺 | P8.9 | `check_contracts.py --strict` 通过 | done |

> **已清理**：R2 存量从 **1229 条**降到 **0 条**（2026-10-27）。做法是两条线：
> 变更日志脚手架（日期戳 / 阶段号 / `P<ver> fix` 标签 / 裸工作项标签）机械剥离并保留正文；
> 真正的「以后会做」承诺改写成契约要求的 `TODO(Px.y): ... -- see docs/DEBT.md#<ID>` 标记，
> 原文只写现状（`mock` / `未实装` / `未接线`），承诺部分落到 §A.2 的登记项里。
> 附带修掉 5 处泄漏到 UI 的字符串（状态栏 / 错误消息里出现「阶段 0 仅占位」）。

> **仍未纳入 R2 的残留**：约 **700** 条注释带裸工作项标签（如 `P7.6.x v3:`、`Q4.1 INVARIANT:`）。
> 当前 R2 规则只禁「带日期的」变更日志脚手架，裸标签不在禁词表里，因此门禁不报。
> 其中一部分是**有意义的交叉引用**（如 `Q4.1 INVARIANT`），不能机械剥离 —— 需要先定义
> 「引用」与「脚手架」的区分规则，再决定是否收进 R2。

---

## §F　维护规则（补充）

1. **R2 的自指豁免**：本文件与 `docs/CONTRACTS.md` 里的示例代码块**不参与** R2 扫描（R2 只扫 `src/`）。
2. **豁免名单**：`tools/contract_allowlist.txt` 每行一个符号名 + `#` 原因。新增豁免必须写原因，否则视为无效。
3. **门禁失效即失败**：拿不到 diff 范围时脚本以退出码 2 退出，**不允许**退化成"全部通过"。
4. **增量判定以基点版本为准**：R1（零调用者）与 R4（重复刷新）的 ERROR 判定标准是
   「本次改动是否**新引入**了这个问题」，而不是「这行被改过」。
   R1 比对基点版本里该头文件已声明的 public 方法名；R4 比对基点版本里该文件的刷新调用次数。
   早期实现用「改动行号是否命中」判定，导致**只改一行注释**就会把早已存在的历史欠债
   重新判成新增违规 —— 门禁因为与本次改动无关的旧债挡住提交。这是误报，
   误报会让门禁被整体忽略。
5. **门禁必须能变红**：改完 `tools/check_contracts.py` 的规则后，必须跑
   `python tools/break_test_contracts.py` —— 它会往真实文件里注入一条该规则的违规
   （新加一个零调用者 public 方法 / 新写一条带日期戳的注释 / 新加一次重复刷新调用 /
   指向未登记 DEBT 的 TODO 标记），确认门禁真的报 ERROR，再把文件还原。
   没跑过这条自检不许提交。
6. **纯注释改动必须有机器证据**：只改注释的提交必须跑
   `python tools/verify_comment_only.py` —— 它把新旧版本都去掉注释后逐字节比较，
   证明代码没被顺手改掉。故意要改代码的文件登记在 `tools/comment_only_allowlist.txt`
   （每行必须写原因）。
