# 04 · 工具系统（15 工具 / 工具栏 / 选项栏 / 工具状态机）— 审计报告

> 范围：`src/media/tools/**` 57 文件 / 6837 行（不含 `icons/*.svg` 内容；只抽样 1 个 svg 用于判定图标主题适配），
> 另加只读耦合面：`src/media/mediators/ToolMediator.*`（38+99 行）、`src/media/imagewindow/ImageCanvas.*` 的
> `m_box`/`drawForeground`、`src/media/selection/{SelectionStrategy,SelectionModel}.cpp`、
> `src/media/imageworker/layers/LayerStack.cpp` 的 Vector 渲染、`src/media/imagewindow.cpp` 的 eventFilter。
> 审查者：audit-tools（逐个文件通读 57/57）

## 1. 模块概览

工具系统 = **状态机内核 + 15 个 ToolState 子类 + 左侧 4 块 UI**。

- **内核**：`tools::ToolState`（非 QObject 抽象基类，只有 5 个鼠标/键盘回调 + `id/pageTitle/cursor/optionPage`，
  `ToolState.h:36-68`）→ `tools::ToolContext`（持有 `std::unique_ptr<ToolState>`，转发事件，`ToolContext.h:57-129`）
  → `mediators::ToolMediator`（每 ImageWindow 一个，只负责 `switchTool` + 一个**从未被使用**的 per-tool QVariantMap 配置表）。
- **谁调用它**：`ImageWindow::eventFilter`（装在 `ImageCanvas::viewport()` 上，`imagewindow.cpp:324`）把
  鼠标 Move/Press/Release 转发给 `m_ctx`（`imagewindow.cpp:2686/2744/2823`）；`LeftToolBar` 按钮和
  `MainWindow` 的 15 个全局快捷键 → `ToolMediator::switchTool` → **两条并行路径**都调 `ToolContext::setState`
  （`ToolContext.cpp:45-51` 与 `LeftToolBar.cpp:249-311`）。`Ctrl+T` 绕过 Mediator 直接
  `m_ctx->setState(std::make_unique<TransformTool>())`（`imagewindow.cpp:2956`）。
- **它调用谁**：`selection::*SelectionStrategy`（产出 mask）→ `SelectionModel::setMask`；`brushes::BrushEngine`
  （stamp 生成/叠加）→ 直接写 `host->currentImage()`；`ImageEditCommand`/`LayerCommand`/`MaskBrushCommand`
  → `host->undoStack()`；`LayerStack`（Shape/Pen 建 Vector 层、MaskBrush 写 mask）。
- **UI 层**：`LeftSidePanel`（1 个 dock 内 4 块：`LeftToolBar` 56px 竖列 + `ImageOptionBar` 300px 竖列 +
  `ModeSelectorBar` + `AppBar`）；`ImageOptionBar` 用 `QStackedWidget` 按 `ToolId` 数值当页号（0..16）。
- **真源数量**：当前工具在 3 处各存一份（`ToolMediator::m_current`、`ToolContext::m_current`、
  `LeftToolBar` 按钮 check 状态），选区模式在 3 处（`ToolMediator::m_configs` 未用、各工具实例成员、
  `ModeSelectorBar` 按钮状态），画笔参数在 4 处（`ToolContext::m_brushShared`、`Brush::m_preset`、
  `Brush::m_paintColor/m_blendMode`、`BrushOptionsPanel` UI）。

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| 04-01 | P0 | imagewindow.cpp:2956-2959 + ImageCanvas.h:160 + TransformTool.cpp:59 | crash | Ctrl+T 进入自由变换后，点任意工具按钮/快捷键切走，下一次画布重绘必崩（UAF） | `ImageCanvas::m_box` 是**裸指针 weak ref**，只被 `setTransformBox(tool->box())` 赋值、**全项目无任何地方置空**；`TransformTool::onExit` 里 `m_box.reset()` 释放对象，canvas 仍持野指针并在 `drawForeground` 里 `m_box->rect()/handlePos()` | `ToolState` 加 `onExit` 后回调节点：`ImageWindow` 监听 `ToolContext::toolChanged`，若旧工具是 Transform 则 `m_canvas->setTransformBox(nullptr)`；或把 `m_box` 换成 `QPointer`/`shared_ptr<TransformBox>` |
| 04-02 | P0 | Brush.cpp:187-243,245-262 + imagewindow.cpp:879-884,1850-1861 | data-loss | 画笔笔画只写进"合成缓存"`m_current`，**任何触发 `invalidateCurrentCache()` 的操作（切换图层可见性/改图层/加删层/撤销/裁切）都会让已画的笔画从画布消失**；撤销笔画时 `replaceCurrentImage(m_imgBefore)` 把整张合成图写进 base layer，破坏图层结构 | Brush 全程只用 `host->currentImage()`（`Brush.cpp:131/190/251`），**从不碰 `LayerStack`**；而 `layerChanged/layerAdded/layerRemoved` → `invalidateCurrentCache()`（`imagewindow.cpp:879-884`）→ `renderToView()` 时 `m_current = m_layerStack->render()` 覆盖 | 笔画必须写进"当前活动图层的 `Layer::image`"（或临时 stroke layer），`layerChanged` 后再合成；`ImageEditCommand` 的 image 路径要改成"按图层 before/after"而不是整图 |
| 04-03 | P0 | Brush.cpp:199-203,242 + BrushEngine.cpp:215-263 | data-loss | 在带 alpha 的 PNG（`CV_8UC4`）上刷一笔，**整块像素被写花/颜色错位**；灰度图上不管选什么色都画成白色 | `applyStamp` 只分 `ch==1` 与"否则按 `cv::Vec3b`（3 通道）寻址"（`BrushEngine.cpp:215-217,248`），对 4 通道图 `at<Vec3b>(y,x)` 的偏移是 `x*3` 而非 `x*4`，逐像素错位写入；灰度分支硬编码 `255*w`（`BrushEngine.cpp:246`）忽略 `m_paintColor` | `applyStamp` 先 `cv::cvtColor` 到 BGRA/统一格式再画；或用 `channels()` 分支处理 4 通道（`Vec4b`，保留 alpha）；灰度按 `color` 亮度映射 |
| 04-04 | P1 | Crop.cpp:64-93 | logic | 裁剪工具"拖框→松手"**什么都没发生**：图像尺寸不变，只在撤销栈里多出一条“Crop”空记录 | `applyCrop` 只 `clone()` 出 `backup`/`cropped` 并 `undoStack()->push(new ImageEditCommand(...))`，**从不调用 `host->setCurrentImage()`/`replaceCurrentImage()`**；而 `ImageEditCommand::redo()` 对 image 路径是 no-op（`imagewindow.cpp:257-277` 注释明写"caller 已应用 after"），Crop 恰好没应用 | `applyCrop` 里在 push 之前/之后调 `host->setCurrentImage(cropped)`（并同步 `LayerStack` 尺寸、`SelectionModel::setSize`、TransformBox/mask 尺寸），或改用 `LayerStack` 的 resize API |
| 04-05 | P1 | ImageOptionBar.cpp:230-234 + imagewindow.cpp:363-366 | state-sync | 选中画笔后**看不到任何画笔选项**（大小/硬度/不透明度/流量/间距/预设全不可达），画笔永远停在默认 13px 硬边黑笔 | `onToolChanged` 先判 `if (id==Brush && m_mosaic) { ensureMosaicPage(); return; }`，而 `m_mosaic` 恒非空（`ImageWindow` ctor 无条件 `make_unique<MosaicTool>`）→ 永远显示 Mosaic 页；下面 `if (idx==Brush && m_brushOpt)`（246-282 行）成**不可达死代码** | 删除 Mosaic 抢占分支（Mosaic 页应挂在 Mosaic 开关上，而不是 Brush），或把 Mosaic 参数并入 BrushOptionsPanel；`m_brushOpt` 分支上移 |
| 04-06 | P1 | ToolContext.cpp:45-51 + LeftToolBar.cpp:249-311 + mainwindow.cpp:3388-3399 | state-sync | 每次切工具都**构造 2 个 ToolState、进入 2 次、退出 1 次**（`LOG_INFO setState` 每次操作打两遍）；选项页被建两遍再删一遍；工具资源双份初始化 | `toolSwitched` 有 2 个接收者：`ToolContext` 自己的 lambda 和 `LeftToolBar::onToolMediatorSwitched`，二者互不知情都调 `setState`；`LeftToolBar` 还完整复制了一份 `ToolStateFactory` 的 switch（196-210 + 258-309） | 只保留一条路径：Mediator 信号 → `ToolContext::setState`；`LeftToolBar` 只做按钮高亮；`onToolMediatorSwitched` 里删除 `m_ctx->setState(...)` 整段 |
| 04-07 | P1 | imagewindow.cpp:2602-2614,2616-2835 | logic | **所有工具的键盘逻辑全是死代码**：Shape 多边形/自定义形状永远无法闭合、Pen 回车无法提交、Esc 无法取消、Clone 的 Esc、Transform 的 Shift 等比全部无效 | `ToolContext::onKeyPress/onKeyRelease` 全项目**零调用者**（`ToolContext.cpp:106-114` 只有定义）；`ImageWindow::keyPressEvent` 只处理 Ctrl+Z/Y，不转发；`ImageCanvas::keyPressEvent` emit 的 `keyPressed` 信号**无订阅者**（grep 仅定义） | 在 `ImageWindow::keyPressEvent` 里 `if (m_ctx) m_ctx->onKeyPress(e);`（未 accept 再走 base），并在 `ToolState` 增加 `onMouseDoubleClick` |
| 04-08 | P1 | mainwindow.cpp:494-527 | state-sync | 用文字工具打字时，**字母 V/M/L/W/C/T/B/I/U/P/S/J 会切换工具而不是输入文字**；任何输入框里打这些字母也会切工具 | 15 个 QAction 用 `Qt::ApplicationShortcut` + 无修饰单键（`act->setShortcutContext(Qt::ApplicationShortcut)`），优先级高于焦点 widget；`m_globalToolActions` 只保存从不 `setEnabled(false)`（grep 无第二处使用） | 改成 `QShortcut` 挂在 `ImageCanvas`（`Qt::WidgetWithChildrenShortcut`），或在 `GraphicsTextItem` 编辑态/任何 `QLineEdit` 获焦时 `setEnabled(false)`；用 `QApplication::focusChanged` 统一开关 |
| 04-09 | P1 | TransformTool.cpp:31-52 | crash | 自由变换框永远是 800×600 固定矩形，与选区无关；若 `selectionModel()` 返回 null 则直接空指针解引用 | `if (!sel && !sel->isEmpty())` 条件**写反**（应为 `if (sel && !sel->isEmpty())`）——`sel` 非空时短路为 false，永远走 fallback；`sel` 为空时反而解引用 null | 改为 `if (sel && !sel->isEmpty()) initRect = sel->boundingRect();`，fallback 用 `host->imageRect()`（当前不存在，需补） |
| 04-10 | P1 | ShapeTool.cpp:140-146,188-195 + PenTool.cpp:56-62 + LayerStack.cpp:1428-1431 | logic | Shape/Pen 画出的图形**位置/大小完全错**（基本看不见） | 工具用 scene/图像像素坐标建 `QPainterPath`（`buildPath(m_pressScenePos, scenePos)`），渲染端却假设路径坐标在 0..100：`p.scale(sz.width/100.0, sz.height/100.0)` 再 `drawPath` → 1000px 处的点被放大 10 倍画到 10000px 外 | 渲染端改用 1:1 图像坐标（删除 `p.scale`），并让 `LayerStack::render` 的 Vector 分支用 `canvasSize()` 坐标系；补一个"矢量层坐标契约"注释 |
| 04-11 | P1 | HealTool.cpp:36-65 | perf | 修复画笔在 24MP 图上按下并拖动，**每次鼠标移动都卡若干秒**（UI 假死） | 每次 `onMouseMove` 都对**整幅图**做 `cv::inpaint(img, mask, ..., 3, TELEA)`（`HealTool.cpp:57`），mask 是整图尺寸；`cv::inpaint` 成本随整图面积而非 brush 面积增长；随后还整图 `qImageToMat`/`setCurrentImage` | 只在 `brushRect` + 一定 padding 的 ROI 上 inpaint，再 `copyTo` 回 ROI；或改 OpenCV `cv::xphoto`/自研 patch 复制；mask 用 ROI 尺寸 |
| 04-12 | P1 | CloneTool.cpp:137-141,156-161 | logic | 仿制图章**源点不跟随**：一笔拖过去只是把源点附近同一小块反复印上去（拉丝），而非 PS 的平行偏移克隆 | press 时 `offset = m_lastDstPos - scenePos`（同一值 → 0），move 时 `offset = scenePos - m_lastDstPos` **只取最后一次增量**，`paintAt(qimg, scenePos, m_samplePoint + offset)` 的源点因此停在采样点附近抖动；正确应为 `src = m_samplePoint + (scenePos - m_strokeStartPos)` | 记录 `m_strokeStart`（press 位置），每帧 `src = m_samplePoint + (scenePos - m_strokeStart)`；加"对齐/非对齐"选项 |
| 04-13 | P1 | EyedropperTool.cpp:35-58 | logic | 吸管**只改状态栏文字**：不改前景色、不改画笔颜色、右侧颜色面板不动；采样大小（点/3×3/5×5/11×11）完全无效；在灰度/4 通道图上取到垃圾色甚至越界读 | `m_sampled` 只被 `sampledColor()` 读，**全项目无消费者**（grep 只有声明）；采样是 `img.at<cv::Vec3b>(y,x)` 单点（无视 `m_sampleSize`）；对 `CV_8UC1` 用 `Vec3b` 寻址 → 每像素跨 3 字节，末像素**行外读** | 采样后发 `colorSampled(QColor)`（ToolState 改 QObject 或用回调/`ToolContext` 信号）→ 写 `ColorDock`/前景色；按 `m_sampleSize` 做矩形均值；先 `cvtColor` 到 BGR 再取值 |
| 04-14 | P1 | Brush.cpp:39-50,199-203 + imagewindow.cpp:2533-2543 | state-sync | 选了红色前景色，画笔仍画黑色；只有"当前工具恰好是 Brush 时"改色才生效，切走再切回立刻恢复黑色 | `Brush` 每次切工具都 `new Brush()`，ctor 里 `m_paintColor(Qt::black)`；`ImageWindow::onForegroundColorChanged` 只在**当时** `dynamic_cast<Brush*>(m_ctx->currentState())` 成功时才 `setPaintColor`；且 LeftToolBar 前景色→ColorDock 的链路本身是死的（见 09 报告 09-01） | 前景色存 `ToolContext`（或 ImageWindow）单一真源，`Brush::onEnter` 从真源取；`Brush::paintSingleStamp` 直接用真源而非成员副本 |
| 04-15 | P1 | RectSelect.cpp:42-68 + Lasso.cpp:45-75 + Crop.cpp:95-116 + PatchTool.cpp:129-160 + ShapeTool.cpp:109-114 + ImageCanvas.cpp:37 | ux | 选框/套索/裁剪/修补/形状**拖动过程中画布上没有任何框线反馈**，松手才见结果；而且拖动的同同时画布还在平移（`ScrollHandDrag`） | 所有拖拽工具只把点存进成员（`m_cropB`/`m_p1`/`m_lastScenePos`），**没有临时 QGraphicsItem 预览**；工具回调是 `void` 且从不 `accept()` 事件，`ImageCanvas` 又固定 `setDragMode(ScrollHandDrag)`，事件继续被 view 消费 | 工具在 `onMousePress` 创建临时 `QGraphicsRectItem/PathItem`（橡皮筋）在 `onMouseMove` 更新、`onExit/onRelease` 删除；进入需要拖拽的工具时把 `ImageCanvas` 切到 `NoDrag`，由 `ToolState` 增加返回 `bool accepted` 的接口 |
| 04-16 | P1 | imagewindow.cpp:2774-2805 | logic | **任何工具**下双击画布都会创建文字 item 并进入编辑；双击也会先进一次 press（画笔先落一个点） | 双击分支只判 `!(m_mosaicTool && m_mosaicTool->isEnabled())`，**不判当前工具是不是 Text**；而 `ImageCanvas::mouseDoubleClickEvent` 也不转发给 `ToolState`（`ToolState` 没有该虚函数） | 双击分支加 `if (currentToolId()==Text)`；否则只转发给当前 tool；同时给 `ToolState` 补 `onMouseDoubleClick` |
| 04-17 | P1 | RectSelect.cpp:65 + Lasso.cpp:72 + MagicWand.cpp:73 + SelectionModel.cpp:84-147 | data-loss | 用选框/套索/魔棒改选区**不能 Ctrl+Z 撤销**；撤销反而回退上一次图像编辑（滤镜/笔画），用户会以为"选区没改动却丢了图像" | 三个工具直接 `sel->setMask()`，**不 push `SelectionCommand`**；而菜单的 SelectAll/Deselect/Invert 会 push（`mainwindow.cpp:2136-2160`）→ 撤销栈语义不一致 | 工具侧统一 `undoStack->push(new SelectionCommand(sel, Op::SetMask, newMask, mode))`；`SelectionCommand` 增加 SetMask/snapshot 形式 |
| 04-18 | P1 | MoveTool.cpp:13-26 + MoveTool.h:21-26 | logic | 移动工具是**空壳**：只有 statusBar 提示，不拖图层、不拖文字、不拖选区、不显示边界框，光标是 `SizeAllCursor` 但没有拖拽行为 | `MoveTool` 只有 22 行，不 override 任何鼠标回调；"F-N will wire drag behavior" 注释停在 2026-09-09 | 实现最小版：press 命中当前图层/文字 item → move 时 `offset` → release 推 `LayerCommand::Move` 或 `TextItemCommand::Move`；有选区时移动选区像素 |
| 04-19 | P1 | PatchTool.cpp:67-73 | logic | 修补工具**大多数操作静默失败**：两次拖框尺寸几乎不可能逐像素相等，一旦不等就 `LOG_WARN` 后 return，用户看不到任何提示 | `if (srcClip.width()!=dstClip.width() \|\| ...) { LOG_WARN; return; }`（P0-9.4 "简化"）；PS 会自动缩放/裁剪目标框 | 目标框按源框尺寸归一化（`cv::resize` 源到目标框，或裁目标框到源尺寸），至少弹 statusBar 错误 |
| 04-20 | P1 | ToolMediator.cpp:19-45 + 各工具 optionPage | state-sync | 每个工具的大小/羽化/容差/模式**切走再切回全部丢回默认值**；选项页也被销毁重建 | `ToolMediator::applyToolConfig/currentConfig/resetToolConfig/toolConfigApplied/toolConfigReset` 五个 API **全项目零调用者**；选项页状态只存在 ToolState 实例成员里，而实例每次切换都 new | 用 `ToolMediator::m_configs` 做 per-tool 设置持久化：`ImageOptionBar` 改值 → `applyToolConfig(id,cfg)`；`ToolState::onEnter` 从 `currentConfig(id())` 恢复；或引入 `ToolSettings` 单例 |
| 04-21 | P1 | imagewindow.cpp:2619-2643 | perf | 每个鼠标事件都往 **`D:/vistella_events.log`** 追加一行并 `flush()`（硬编码绝对路径、静态 QFile 从不关闭）——工具输入路径上每次移动都同步磁盘 IO | 调试代码未清理：`static QFile efLog("D:/vistella_events.log"); ... efLog.write(...); efLog.flush();` | 删除该块（或 `#ifdef VISTELLA_EVENT_DEBUG` 包裹 + `spdlog` 异步 logger） |
| 04-22 | P2 | Crop.cpp:118-147,105-109 | logic | 裁剪比例 1:1/4:3/16:9 选了没用；无旋转、无透视裁剪；松手立即裁剪（没有确认/取消） | `m_aspect` 只在 `setAspectRatio/optionPage` 里读写（grep 全文仅 4 处），`onMouseMove`/`computeClampedRect` 从不使用；也没有 `CropOverlay` 交互层 | `onMouseMove` 按 `m_aspect` 约束 `m_cropB`；加 Enter 确认 / Esc 取消；补旋转（`QTransform` + `warpAffine`） |
| 04-23 | P2 | RectSelect.cpp:80-98 + ModeSelectorBar.cpp:211-236 | logic | 矩形选框的"模式"下拉框**完全没接线**（纯装饰）；RectSelect 也没 `setSelectionMode`，`ModeSelectorBar` 只给 Lasso/MagicWand 做模式页 | `optionPage` 里 `modeCombo` 创建后**没有 connect**（`RectSelect.cpp:88-94`）；`ToolState` 派生的 RectSelect 缺少 `selectionMode()` 成员 | 把模式存进 ToolContext/`applyToolConfig`；`RectSelect` 加 `setSelectionMode`；`ModeSelectorBar::showPageForTool` 增加 RectSelect case |
| 04-24 | P2 | Brush.cpp:200,242 + ModeSelectorBar.cpp:308-316 + BrushEngine.cpp:249-253 | logic | 选项栏的画笔混合模式按钮（Normal/Multiply/Screen）**按下无效果**；面板里 6 种模式中"叠加/变暗/变亮"静默退化成 Normal | 每次落笔 `m_blendMode = ss.blendMode`（`Brush.cpp:200`）把 `ModeSelectorBar::onBlendBtn` 刚写进实例的值覆盖回 0；`applyStamp` 只实现 `blendMode==1/2`，其余走 Normal 分支 | 删掉实例副本，直接用共享状态（`ss.blendMode`）；`applyStamp` 补 Overlay/Darken/Lighten；`ModeSelectorBar` 与面板共用同一个 blendMode 枚举 |
| 04-25 | P2 | RedEyeTool.cpp:53-99,124-176 | logic | "Pupil Size" 滑杆无效（只影响 UI label） | `m_pupilSize` 仅在 optionPage 中出现（grep 全文 3 处），`applyRedEyeAt` 从不读它；实际按 R>1.5G/1.5B 逐像素判红，与 pupil 尺寸无关 | 用 `m_pupilSize` 限定处理圆的半径（`d<=r`）并对该圆做去红，或删除该控件 |
| 04-26 | P2 | SelectionStrategy.cpp:103-142 | perf/memory | 套索"羽化"在 24MP 图上每次松手分配约 **96MB** 全图积分图（`(W+1)*(H+1) int`）并全图两遍遍历，明显卡顿 | mask 是整图尺寸，羽化也按整图算，没有先裁 bbox | 先用已选区域的 `bbox` 扩展 `R` 做 ROI，只对 ROI 做 box blur |
| 04-27 | P2 | SelectionStrategy.cpp:184-235 | perf/logic | 魔棒在大图上**非常慢**（每像素 `image.pixel()` + `qGray`，多次格式分派）；且只比灰度，**亮度相同但颜色不同的像素会被选中** | BFS 内 `qGray(image.pixel(x,y))` 逐像素取色；`seedGray`/`g` 只有一维 luma，不是 RGB 距离 | 进函数前 `convertToFormat(QImage::Format_RGB32)` 并缓存 `constScanLine` 指针数组；改为 RGB 欧氏距离（或 HSV） |
| 04-28 | P2 | ImageCanvas.cpp:284-338 | ui-visual/perf | 蚂蚁线**只是选区 bbox 的一个矩形**（套索/魔棒的形状完全不显示）；ant 每 200ms 全 viewport 重绘，mask overlay 打开时每帧全图 O(W×H) 扫描 | `drawForeground` 只 `p->drawRect(sceneBbox)`（337 行），注释里说的"scan each row 找 runs"没实现；`MarchingAnts` 200ms tick → `viewport()->update()` | 用 `QPainterPath` 从 mask 提取轮廓（marching squares，或 `QRegion(QBitmap)` 边界）缓存一次，动画只改 dash offset 不重建；overlay 合成用 `QImage` 掩码缓存 |
| 04-29 | P2 | ToolContext.h:29-53 + Brush.cpp:187-243 | logic | 共享画笔 9 字段里**只有 4 个真正进入落笔路径**（blendMode/opacity/flow/size）；hardness/angle/spacing 只能靠 preset 且面板不可达（04-05）；scatter 端到端**完全未实现**；smoothing 只由 preset 决定 | `paintSingleStamp` 读 `ss` 后只覆盖 `s.size` + 两个系数（199-217 行）；`BrushEngine::scatterPoint/scatterOffset` 全项目无调用者；`ss.hardness/spacing/angle` 从未被读 | 把 9 字段全部落到 `BrushSettings`（hardness/angle/spacing）并在 `onMouseMove` 用 `ss.scatter` 调 `scatterPoint`；删掉 `m_preset` 的同名副本，只保留 shape/dynamics |
| 04-30 | P2 | Brush.cpp:42-49,127-179 + ToolState.h:51-55 | logic | **压感完全没有**：`paintSingleStamp(host,pos,1.0)` 恒传 1.0；`PressureCurve` 分配后从不使用；笔尖倾斜/旋转同理 | `ToolState` 只有 5 个 mouse/key 回调，没有 `QTabletEvent` 入口；`ImageWindow::eventFilter` 也不监听 `TabletPress/Move/Release`；`m_pressureCurve` 只有 getter | `ToolState` 增加 `onTabletEvent(QTabletEvent*)`；`eventFilter` 加 3 个 Tablet 分支；把 `pressure/tilt` 透传到 `resolve()` |
| 04-31 | P2 | CloneTool.cpp:133-141,153-161 + Brush.cpp:252-259 | perf | 仿制/修复/红眼每帧把整幅图 `matToQImage` + `qImageToMat` + `setCurrentImage`（后者再 `clone` 两次并 `refreshAll`），24MP 图拖动明显掉帧 | `CloneTool::onMouseMove` 全图转换 + `setCurrentImage`（同时重置 `m_original`）；`Brush::finalizeStroke` 用 `ImageEditCommand` 存两份全图 clone | 改成 ROI 级 `cv::Mat` 就地改 + `invalidateRect()`；`setCurrentImage` 只保留给"整图替换"语义；笔画历史用 ROI 差分或 tile |
| 04-32 | P2 | BrushEngine.cpp:25-75 | perf/memory | 面板允许 brush size 到 2000px；一个 stamp 生成 `generateFalloff` + `generateElliptical` 两个 `CV_32FC1`（各 16MB）+ 循环 400 万像素；一次 move 可能生成多个 stamp → 明显卡顿/瞬时内存尖峰 | `generateStamp` 每次调用都重新分配并重复计算（falloff 只依赖 diameter/hardness，可缓存） | 按 (diameter,hardness,shape) 上限 32 项的 LRU 缓存 stamp/falloff；大尺寸用低分辨率 stamp + 上采样 |
| 04-33 | P2 | MaskBrushTool.cpp:23-26,146-170 + MaskBrushCommand.cpp:38-54 | data-loss | 画笔拖到一半切换工具/切 tab：mask 已被改但**没有任何 undo 记录**，用户无法撤销这次涂抹；另外首笔撤销后 mask 变"空 Mat + kind=Pixel"的半非法状态 | `onExit` 只 `m_painting=false`，`m_strokeBefore` 直接丢弃；`undo()` 用 `applyToLayer(m_before)`，首笔时 `m_before` 为空 Mat，仍把 kind 置 Pixel 且 `enabled=true` | `onExit` 里若 `m_painting` 就 `commitStroke()`（push 命令）；命令里额外保存 `beforeKind/beforeEnabled` 并恢复 |
| 04-34 | P2 | MaskBrushTool.cpp:96-100 + MaskBrushTool.cpp:36-41 | perf/memory | 麻点级问题：mask 画笔**每个 stamp** 都 `emit stack->layerChanged` → 触发 `invalidateCurrentCache()`，一次 mousemove 插值出 n 个 stamp 就标脏 n 次；`MaskOptionsPanel` 以 `this`（工具）为 model，工具每次切换都重建 → 每切一次建/删一个面板 | 合成粒度 = stamp 而不是 stroke；面板生命周期跟工具绑定而不是跟 ToolContext 绑定 | 笔画期间只 `viewport()->update()`，release 时统一 emit 一次；面板做成常驻、由 `MaskBrushSharedState`（像 Brush 那样）驱动 |
| 04-35 | P2 | Brush/Clone/Heal/Patch 全文件（无 `selectionModel()` 引用） | logic | **选区对绘画类工具完全没有约束**：有选区时画笔/仿制/修复照样画到选区外 | 四个工具都不读 `host->selectionModel()`；PS 语义是"选区外不可编辑" | 在 stamp/像素写入前用 mask 值加权 `w *= mask[y*stride+x]/255`（描边外 alpha=0） |
| 04-36 | P2 | LeftToolBar.cpp:212-238,240-247 + mainwindow.cpp:3371-3399 | state-sync/memory | 每切一次 tab 就 `attach()` 一次且**不先 disconnect**：同一 tab 反复切换会累积 `toolSwitched` 连接（切一次工具触发 N 次→叠加 04-06 的双份）；`detach()` 只断 mediator，15 个按钮 lambda 永不断开 | `bindLeftDockWidgets` 每次都 `leftToolBar->attach(...)`；`attach` 内直接 `connect`；按钮 connect 的 context 是 `this` 而非 mediator，`detach` 清不到 | `attach` 首行 `detach()`；按钮连接改为一次性（ctor 里 connect 到 `m_toolMed` 的可空转发槽），或 QSignalBlocker+重连 |
| 04-37 | P2 | LeftToolBar.ui:211-241 + LeftToolBar.cpp:52-55 + icons/tool-*.svg | ui-visual | 前景/背景色块用**绝对几何互相重叠**（`fgSwatch` 0..50、`bgSwatch` 46..56、`swapBtn` 40..52，后创建者在上）：点前景色块右部会弹出背景色或触发交换；图标固定 16px（28px 按钮内），SVG 里写死 `fill="#202020"`，**暗色主题下几乎看不见**；tooltip 不提示快捷键 | `.ui` 用 fixed geometry 无 layout；`setIconSize(QSize(16,16))`；SVG 硬编码单色 | 用 layout/qss 画 PS 双色块（前景左上、背景右下、交换箭头右上不重叠）；图标改 `currentColor`/运行时按 palette 着色（`QIcon::pixmap` + `QPainter` 重着色）；tooltip 加 `(V)/(M)...` |
| 04-38 | P3 | AppBar.cpp:65-77 + AppBar.ui | i18n/ui-visual | AppBar 3 组按钮只有单字母 "QM/S/F/M/C"，采集菜单中文硬编码无 `tr()` | 占位实现未走 i18n | 换图标 + `tr()`，屏幕模式用 `QButtonGroup` 三态图标 |
| 04-39 | P3 | ToolStateFactory.cpp:40-43 + LeftToolBar.cpp:196-210,258-309 + ImageOptionBar.cpp:187-225 | design | "单一真源"名不副实：Factory 对 `Transform` 返回 `nullptr`（而 TransformTool 真实存在），LeftToolBar 又完整复制一份 switch；`ImageOptionBar` 里 `idx>=count` 的两个"动态 addPage"分支永远不可达（.ui 有 17 页，Transform=9、MaskBrush=16 都 < 17） | 多轮 P 阶段叠加未清理 | 删除 LeftToolBar 的 15 个 create 包装与 switch；Factory 支持 Transform；删除 OptionBar 不可达分支 |
| 04-40 | P3 | MaskBrushTool.h:70 + MaskBrushTool.cpp:36-41 | memory | `optionPage` 缓存 `MaskOptionsPanel*`（parent=ImageOptionBar，持有 `MaskBrushTool*`）；工具实例每次切换被销毁，面板若仍在 stackedWidget 里则 `m_tool` 悬垂（当前隐藏不可点，属潜在 UAF） | 面板与工具生命周期耦合，且面板归属 OptionBar | 面板持有 `ToolContext*`/共享状态结构而不是工具实例；或 `onExit` 时 `m_optionPage->deleteLater()` |
| 04-41 | P3 | BrushEngine.cpp:359-366 + Brush.cpp:215-220 | logic | 潜在压感双重缩放：`resolve()` 里 `diameter = s.size*...*pressure`，随后 `s.size=rp.diameter` 传给 `generateStamp`，后者又乘一次 `pressure` → 接入压感后尺寸按 pressure² 衰减 | 两处都应用压力，职责重叠 | 只在 `generateStamp` 或只在 `resolve` 里应用一次压力 |
| 04-42 | P3 | 各工具 pageTitle / optionPage | i18n | 标题/选项文案中英混排且用 `QStringLiteral` 硬编码（"形状"/"钢笔"/"文字"/"Mask Brush"/"Move"）；`ToolState` 非 QObject 导致用 `QCoreApplication::translate("tools::X", ...)` 上下文串易与 `.ts` 脱节 | 无统一 i18n 策略 | `ToolState` 增加 `translate()` 助手或改为 QObject；统一英文 key + `.ts` |

### 04-01 TransformBox 悬垂指针 → `drawForeground` UAF (P0)

- **位置**：`src/media/imagewindow.cpp:2956-2959`；`src/media/imagewindow/ImageCanvas.h:157-160`；`src/media/tools/TransformTool.cpp:54-60`
- **证据**：

```cpp
// ImageCanvas.h —— 裸指针，注释自称 "weak ref, ImageWindow::m_box 持所有权"
transform::TransformBox*   m_box  = nullptr;

// imagewindow.cpp onFreeTransform —— 唯一赋值点（grep 全项目无第二处）
m_ctx->setState(std::make_unique<tools::TransformTool>());
if (auto* tool = dynamic_cast<tools::TransformTool*>(m_ctx->currentState())) {
    if (m_canvas) m_canvas->setTransformBox(tool->box());   // 拿的是 unique_ptr 内部地址
}

// TransformTool.cpp onExit —— 任何人切走工具都会走到这里
m_box.reset();                                               // 对象已 delete
```

- **现象 / 复现路径**：打开图片 → Ctrl+T（出现变换框）→ 点左侧任意工具按钮（或按 V/M/B…）→ 画布下一次重绘（鼠标移过即可）→ `ImageCanvas::drawForeground` 里 `m_box->rect()` / `m_box->handlePos()` 访问已释放内存 → 崩溃（Release 下可能是随机花屏/偶发崩）。
- **根因**：`ImageCanvas` 只有 `setTransformBox()`，**没有 clear 路径**；`TransformBox` 由 `TransformTool` 独占（`unique_ptr`），`onExit` 释放时 canvas 不知情；`QPointer` 也没用（TransformBox 非 QObject）。
- **修复建议**：`ImageWindow` 连接 `ToolContext::toolChanged`，在切换前把 `m_canvas->setTransformBox(nullptr)`；更彻底：`TransformBox` 由 `ImageWindow` 持有、`TransformTool` 只持引用，或 `ImageCanvas` 用 `QPointer<TransformTool>` 并在 paint 前判活。
- **关联**：`TransformBox`/`ImageCanvas`（05 画布模块）、`Ctrl+T` 菜单入口。

### 04-02 画笔笔画只存在于合成缓存，任何图层操作都会"擦掉" (P0)

- **位置**：`src/media/tools/Brush.cpp:131,190,251`；`src/media/imagewindow.cpp:879-884,1891-1972,1850-1861`
- **证据**：

```cpp
// Brush.cpp —— 全程只碰 host->currentImage()（= layerStack.render() 的缓存）
cv::Mat& target = host->currentImage();
... BrushEngine::applyStamp(target, cv::Point(ix, iy), stamp, ...);
host->renderToViewPublic();

// imagewindow.cpp —— 任一图层信号就把缓存标脏，renderToView 时整体重算
connect(m_layerStack.get(), &layers::LayerStack::layerChanged,
        this, [this](int){ invalidateCurrentCache(); });
...
void ImageWindow::renderToView() {
    if (m_currentDirty) rebuildCurrentCache();   // m_current = m_layerStack->render()
```

- **现象 / 复现路径**：新建/打开图片 → 画笔涂几笔 → 在图层面板点一下"眼睛"（或改图层不透明度/新建图层/Ctrl+Z 一次别的操作）→ **笔画消失**，图像变回涂之前；若此时保存，落盘的是没有笔画的内容。
- **根因**：工具层的写入目标是"展平缓存"而不是"活动图层的 `Layer::image`"。`LayerStack` 才是真源，但 Brush/Heal/Patch/RedEye 都绕过它；只有 MaskBrush 正确走了 `LayerStack`。`ImageEditCommand` 的 `redo()` 是 no-op（`imagewindow.cpp:257-277`）意味着 `push` 也不会把 after 回写图层，所以"缓存里的笔画"永远进不了任何 layer。撤销时 `replaceCurrentImage(m_imgBefore)`（`imagewindow.cpp:831-846`）再把整张合成图塞进 base layer，进一步破坏图层结构。
- **修复建议**：①在 `ImageWindow` 增加 `activeLayerImage()`/`commitLayerEdit(idx, before, after)`；②画笔笔画写 `activeLayer->image`（可加临时 stroke layer 提升性能），release 时 `LayerCommand` 记录该图层 before/after；③`ImageEditCommand` 的整图路径只保留给"整图替换"类操作（Crop/resize/滤镜），并显式在调用方 apply。
- **关联**：07 图层模块、05 画布模块、撤销栈设计；与 09 报告"保存 flattens text"同一类"工具数据不进文档模型"问题。

### 04-04 裁剪工具完全不生效 (P1)

- **位置**：`src/media/tools/Crop.cpp:64-93`
- **证据**：

```cpp
cv::Mat backup = img.clone();
cv::Mat cropped = img(cvRect).clone();
if (auto* stack = host->undoStack()) {
    stack->push(new ImageEditCommand(host, backup, cropped, ...));   // redo() 对 image 路径是 no-op
}
m_hasCrop = false;      // ← 全程没有 host->setCurrentImage / replaceCurrentImage / layerStack 操作
```

- **现象 / 复现路径**：选裁剪工具 → 在画布上拖一个框 → 松手 → 画布毫无变化；`Ctrl+Z` 会回退（把同一张图再写一遍），历史面板多一条"Crop"。
- **根因**：`ImageEditCommand::redo()` 在 `m_useImage=true` 时按 P2.5 约定是空实现，要求 caller 先应用 after。Crop 是唯一没遵守该约定的工具（对比 `RedEyeTool.cpp:89`、`CloneTool.cpp:141` 都先 `setCurrentImage`）。
- **修复建议**：push 之后立刻 `host->setCurrentImage(cropped)`，并在 `ImageWindow` 里增加"尺寸变化通知"（`SelectionModel::setSize`、TransformBox 重置、`LayerStack` base layer 尺寸），否则裁剪后选区/蒙版尺寸会与图像不匹配（`SelectionModel::setMask` 会直接 size mismatch return，导致后续所有选区工具静默失效）。撤销路径同样改 `replaceCurrentImage(cropped)`。
- **关联**：05 画布、07 图层、selection mask 尺寸同步。

### 04-05 画笔选项面板不可达，画笔被锁死在默认参数 (P1)

- **位置**：`src/media/tools/ImageOptionBar.cpp:230-234,246-282`；`src/media/imagewindow.cpp:363-366`
- **证据**：

```cpp
// imagewindow.cpp —— m_mosaicTool 总是存在
m_mosaicTool = std::make_unique<MosaicTool>(this);

// ImageOptionBar::onToolChanged
if (id == mediators::ToolId::Brush && m_mosaic) {
    ensureMosaicPage();
    ui->stackedWidget->setCurrentIndex(m_mosaicPageIdx);   // 永远显示 Mosaic 页（idx=17）
    return;                                                // ← BrushOptionsPanel 永远轮不到
}
...
if (idx == static_cast<int>(mediators::ToolId::Brush) && m_brushOpt) { /* 死代码 */ }
```

- **现象 / 复现路径**：打开图片 → 点画笔（B）→ 选项栏显示的是"MosaicSize/MosaicType"而不是画笔参数；用户无法改大小/硬度/不透明度/流量/预设/笔尖种类；落笔恒为 `ToolContext::BrushSharedState` 默认值（13px、hardness 50、opacity/flow 100、Normal）。
- **根因**：P5.1 把 Mosaic 页"路由到 Brush"的设计与 P7.6.x 后加的 BrushOptionsPanel 冲突，后者被挤到不可达分支；`m_brushOpt` 相关 1 处 `presetChanged/pickerRequested` 连接也因此从未执行。
- **修复建议**：Mosaic 页由 `MosaicTool::isEnabled()` 驱动（在 ModeSelectorBar 或面板加"马赛克模式"开关），Brush 恒显示 BrushOptionsPanel；把 `installBrushOptionsPage` 的持久页设为 Brush 页默认内容。
- **关联**：`BrushSharedState` 全部 9 字段实际都取默认值（04-29）、`ModeSelectorBar` 混合模式按钮失效（04-24）。

### 04-06 工具切换双真源：每次切工具构造两个 ToolState (P1)

- **位置**：`src/media/tools/ToolContext.cpp:45-51,68-89`；`src/media/tools/LeftToolBar.cpp:249-311`
- **证据**：

```cpp
// ToolContext::attach —— 接收者 A
connect(m_toolMed, &mediators::ToolMediator::toolSwitched, this, [this](mediators::ToolId id) {
    auto* st = createToolState(id);
    setState(std::unique_ptr<ToolState>(st));
});
// LeftToolBar::onToolMediatorSwitched —— 接收者 B（同样的创建 + setState）
case mediators::ToolId::Move:
    m_ctx->setState(std::unique_ptr<ToolState>(createMoveTool()));
    break;
```

- **现象 / 复现路径**：任意切工具，日志出现 `[ToolCtx] setState` 两次；`ImageOptionBar` 的动态页（Shape/Pen/Clone/Heal/Patch/RedEye/Transform/MaskBrush）被创建两次、删除一次；`onEnter/onExit` 时序变成 `onExit(old) → onEnter(new1) → onExit(new1) → onEnter(new2)`，任何在 `onEnter` 里分配硬件/注册资源的工具都会泄漏或状态错乱。
- **根因**：P1.3.4 为了让"菜单/快捷键"也能切工具，在 ToolContext 里加了连接，但没删除 LeftToolBar 里的同职责代码；`LeftToolBar.h:80-96` 还保留 15 个 `createXxxTool()` 包装，而 `ToolStateFactory.h:9-13` 声称"single source of truth"。
- **修复建议**：`onToolMediatorSwitched` 只保留 `setChecked` 高亮；`setState` 唯一入口 = ToolContext 的 lambda；`LeftToolBar` 删除 15 个 factory 包装与 switch。
- **关联**：04-39、选项页生命周期。

### 04-07 键盘事件从不进入工具层（多边形形状无法完成）(P1)

- **位置**：`src/media/tools/ToolContext.cpp:106-114`；`src/media/imagewindow.cpp:2602-2614`；`src/media/imagewindow/ImageCanvas.cpp:163-171`；`src/media/tools/ShapeTool.cpp:164-209`；`src/media/tools/PenTool.cpp:99-113`
- **证据**：

```cpp
// ToolContext.cpp —— 两个函数全项目无调用者
void ToolContext::onKeyPress(QKeyEvent* e)   { if (m_current) m_current->onKeyPress(e, m_host.data()); }

// ImageWindow::keyPressEvent —— 只处理撤销/重做，不转发
if (e->modifiers()==Qt::ControlModifier && e->key()==Qt::Key_Z) { onUndo(); return; }
QMainWindow::keyPressEvent(e);

// ImageCanvas::keyPressEvent —— emit 了没人接
emit keyPressed(e);
```

- **现象 / 复现路径**：形状工具选"多边形" → 在画布上依次点 4 个点 → 按 Enter 想闭合 → **毫无反应**，点会无限累积；钢笔工具按 Enter 不提交、Esc 不清空；Clone 的 Esc"清除采样"、Transform 的 Shift 等比约束、Text 的 Esc 退出编辑全部失效。叠加 04-16（双击被文字工具截走），**Polygon/Custom 形状在任何操作下都无法完成**。
- **根因**：`ToolState` 只有 `onKeyPress/onKeyRelease` 两个虚函数，但转发链路从未接通；`ImageCanvas::keyPressed` 信号是更早的 P0-1.1 遗留、P0-1.4 未接。
- **修复建议**：`ImageWindow::keyPressEvent` 首行 `if (m_ctx) { m_ctx->onKeyPress(e); if (e->isAccepted()) return; }`；`ToolState` 增加 `onMouseDoubleClick(QMouseEvent*, ImageWindow*, QPointF)` 并把 `eventFilter` 的 dblclick 分支转发进去；Polygon 同时支持双击闭合。
- **关联**：04-16、04-09（Shift 约束）、Text/Clone/Pen/Shape。

### 04-13 吸管"看起来能用但什么也没影响" (P1)

- **位置**：`src/media/tools/EyedropperTool.cpp:35-58,63-93`
- **证据**：

```cpp
const cv::Vec3b bgr = img.at<cv::Vec3b>(y, x);      // 假设一定是 8UC3
m_sampled = QColor(bgr[2], bgr[1], bgr[0]);
... statusBar()->showMessage("Eyedropper: (...)= #...");
// 结束。没有 signal、没有 ToolContext 调用、没有 setForegroundColor。

// optionPage: 采样大小 combo 只 setSampleSize(m_sampleSize)，采样函数里从不读 m_sampleSize
```

- **现象 / 复现路径**：点吸管 → 点画布任意处 → 只有状态栏闪一行 hex；左侧前景色块、右侧颜色面板、画笔颜色都不变（`sampledColor()` 全项目无消费者）。在灰度图（`CV_8UC1`）上点吸管，`Vec3b` 每像素跨 3 字节 → 末行像素读到行外/下一行数据（潜在越界读，UB）。
- **根因**：F-C 阶段占位实现，F-J 计划的 DialogMediator 推送从未落地；`ToolState` 非 QObject 也没有采样结果信号。
- **修复建议**：`ToolState` 增加 `virtual void onColorSampled(const QColor&)` 或让 `ToolContext` 发 `colorSampled(QColor)`；`ImageWindow` 收后写 `ColorDock/ColorTab::setForegroundColor` + `Brush::setPaintColor`（与 04-14 统一真源）；采样前 `cvtColor` 统一到 BGR；按 `m_sampleSize` 求均值。
- **关联**：09 报告 09-01（颜色真源）、04-14。

### 04-14 前景色对画笔无效（每次切工具都回到黑色）(P1)

- **位置**：`src/media/tools/Brush.cpp:39-50,187-243`；`src/media/imagewindow.cpp:2533-2543`
- **证据**：

```cpp
// Brush ctor —— 每个实例都是黑笔
Brush::Brush(...) : ..., m_paintColor(Qt::black) { ... }

// ImageWindow::onForegroundColorChanged —— 只在"那一刻"当前工具是 Brush 时才写
if (auto* brush = dynamic_cast<tools::Brush*>(m_ctx->currentState())) brush->setPaintColor(c);
```

- **现象 / 复现路径**：在颜色面板选红色（此时工具是 Move）→ 按 B 切画笔 → 画出来是黑色；即使正在画笔时改色成功，切到 Move 再切回画笔（工具实例被 new）→ 又变黑。
- **根因**：颜色真源在 `ColorTab/ColorDock`，工具层只做一次性 dynamic_cast 推送，没有"进入工具时从真源恢复"的机制；`Brush` 实例由 `setState` 每次重建。
- **修复建议**：前景色放到 `ToolContext`（或 `ImageWindow` 的 document model），`Brush::onEnter` 与 `paintSingleStamp` 都从该真源取色；`LeftToolBar` 前景色块同样读写该真源（现在 LeftToolBar→ColorDock 的同步因 09-01 是死链）。
- **关联**：09-01、04-13、`Text` 的默认颜色同理（Text 走 `TextOverlayController`，是另一份真源）。

### 04-18 移动工具是空壳 (P1)

- **位置**：`src/media/tools/MoveTool.cpp:13-26`；`src/media/tools/MoveTool.h:21-26`
- **证据**：整个实现只有 `onEnter` 里 `statusBar()->showMessage("Move tool (V) - F-N will wire drag behavior")` 和 `onExit` 的 `clearMessage()`；没有 `onMousePress/Move/Release` override。
- **现象**：选移动工具后拖画布 = 平移视图（`ScrollHandDrag`），图层/文字/选区位图都不动；PS 里移动工具是最常用工具之一。
- **根因**：F-C 只做了 State 骨架，F-N 的"wiring"从未完成；`ImageWindow::eventFilter` 里文字 item 的拖动是独立实现（`m_textCtrl->handleDragItem`），与 MoveTool 无关。
- **修复建议**：最小实现：press → 命中当前层/文字 item/选区 bbox → 记录起点；move → 临时平移（文字 `setPosition`、位图用 `LayerCommand` 的 move 或临时 preview）；release → push 一条命令。同时把 `ImageCanvas` 的 DragMode 在 MoveTool 激活时切成 `NoDrag`。
- **关联**：07 图层（`Layer::offset` 或 `transform`）、TextOverlayController、05 画布。

### 04-03 画笔在 4 通道图上写花像素 (P0)

- **位置**：`src/media/tools/Brush.cpp:199-203,242`；`src/media/brushes/BrushEngine.cpp:215-263`
- **证据**：

```cpp
const int ch = target.channels();
const bool grayscale = (ch == 1);          // 只有 1 和"其他"两档
...
} else if (useColor) {
    auto &p = target.at<cv::Vec3b>(y, x);  // CV_8UC4 时按 3 字节步长寻址 → 每个像素写错位置
```

- **现象 / 复现路径**：打开任意带 alpha 的 PNG（`IMREAD_UNCHANGED` → `CV_8UC4`，`imagewindow.cpp:851`），选画笔涂一笔 → 涂过的区域出现 RGB 条纹/整块颜色错位（相邻像素数据被互相覆盖）；保存后内容就是这个坏图。
- **根因**：`applyStamp` 只处理"灰度 1 通道"与"其它 = Vec3b 3 通道"两种情况，4 通道图被当成 3 通道访问。灰度分支更把目标颜色硬编码为 255（`BrushEngine.cpp:238,246,261`），忽略 `m_paintColor`。
- **修复建议**：`applyStamp` 入口按 `target.channels()` 分派 1/3/4 三种 `at<>` 访问；或在 Brush 落笔前把目标 `cvtColor` 到 BGRA 统一处理；灰度用 `color.lightness()` 或 `qGray` 目标值替代 255。
- **关联**：`ImageProcessor::matToQImage/qImageToMat`（05 画布）对 4 通道的支持、保存链路。

### 04-08 全局单键快捷键劫持文字输入 (P1)

- **位置**：`src/app/mainwindow.cpp:494-527`
- **证据**：

```cpp
act->setShortcut(QKeySequence(QString::fromLatin1(e.shortcut)));  // "V"/"M"/"L"... 无修饰键
act->setShortcutContext(Qt::ApplicationShortcut);                 // 整个应用范围
connect(act, &QAction::triggered, this, [this, id = e.id]() { onSwitchTool(id); });
```

- **现象 / 复现路径**：文字工具点画布建 item 后输入 "Vistella" → 第一个字符就把工具切成 Move 并丢失输入焦点；任何 `QLineEdit`（如 TextOptionPanel 的"内容"框、搜索框）里输入这些字母也会切工具。
- **根因**：无修饰单键 + `ApplicationShortcut` 优先级高于焦点 widget；`m_globalToolActions` 保存了 QAction 列表却从不 `setEnabled(false)`（grep 只有 2 处引用）。
- **修复建议**：改为挂在 `ImageCanvas` 上的 `QShortcut`（`Qt::WidgetWithChildrenShortcut`），并监听 `QApplication::focusChanged`：焦点进入 `QLineEdit/QTextEdit/QGraphicsTextItem` 编辑态时统一禁用；`m_globalToolActions` 保留作为"可挂 UI 的工具命令"。
- **关联**：`TextOverlayController`/`GraphicsTextItem`（06 文字）、`MainWindow` 菜单（00/03）。

### 04-09 TransformTool 初始化条件写反（空指针解引用 + 变换框尺寸错误）(P1)

- **位置**：`src/media/tools/TransformTool.cpp:31-52`
- **证据**：

```cpp
if (auto* sel = host->selectionModel()) {
    if (!sel && !sel->isEmpty()) {        // sel 非空 → 条件恒 false；sel 为空 → 解引用 null
        initRect = sel->boundingRect();
    }
}
if (initRect.isEmpty()) initRect = QRectF(0, 0, 800, 600);   // 永远走这里
```

- **现象 / 复现路径**：先用矩形选框选一块 200×100 → Ctrl+T：变换框永远是 800×600 居中，而不是选区范围；变换结果与选区无关（`commitTransform` 用 `box->rect()` 做 warp 源）。
- **根因**：`!sel` 是笔误（应为 `sel`）；且 fallback 没有使用图像尺寸（注释自认"P0-6.6 集成后通过 host->imageRect()"）。
- **修复建议**：改条件为 `if (sel && !sel->isEmpty())`；fallback 用 `QRectF(0,0,currentImage().cols,rows)`；无选区时 PS 语义是变换整个图层。
- **关联**：`SelectionModel`（05）、`TransformMath`/`TransformCommand`（transform 模块）。

### 04-10 矢量层坐标契约错误：Shape/Pen 画出的图形不可见 (P1)

- **位置**：`src/media/tools/ShapeTool.cpp:132-146,188-195`；`src/media/tools/PenTool.cpp:48-62`；`src/media/imageworker/layers/LayerStack.cpp:1414-1439`
- **证据**：

```cpp
// 工具：路径坐标 = scene/图像像素
QPainterPath path = buildPath(m_pressScenePos, scenePos);   // 例如 (1200, 800)

// 渲染：假设路径坐标在 0..100
p.scale(sz.width / 100.0, sz.height / 100.0);               // 3000/100=30 倍
p.drawPath(l->vectorPaths[i]);                              // (1200,800) → (36000,24000)
```

- **现象 / 复现路径**：在 3000×2000 图上用形状工具拖一个框/用钢笔点几个点 → 图层面板出现"形状 (vector)"层，但画布上**什么都看不到**（图形被画到画布外几十倍远处）；小图（<100px）反而会被缩小。
- **根因**：`Layer::Vector` 的坐标契约（注释"假设 path 坐标在 0-100 范围"）与工具写入的像素坐标不一致；另外渲染端 `p.setPen(stroke>0 ? QPen(fill, stroke) : NoPen)` 用 fill 颜色当描边色，`vectorStrokeColors`（工具明明写了）被忽略。
- **修复建议**：统一为图像像素坐标（删除 `p.scale`），或在 `addLayer` 前由工具归一化到 canvas 尺寸（需明确契约并加注释/断言）；描边改用 `vectorStrokeColors[i]`。
- **关联**：07 图层（Vector 渲染）、路径编辑（缺失）、Pen/Shape。

### 04-11 HealTool 每帧整图 inpaint → UI 假死 (P1)

- **位置**：`src/media/tools/HealTool.cpp:36-65`
- **证据**：

```cpp
cv::Mat mask = cv::Mat::zeros(img.size(), CV_8UC1);   // 整图尺寸 mask
cv::rectangle(mask, brushRect, cv::Scalar(255), cv::FILLED);
cv::inpaint(img, mask, inpainted, 3, cv::INPAINT_TELEA);   // 成本 ∝ 整图面积
roi.copyTo(img(brushRect));
host->setCurrentImage(img);                               // 再全图 clone ×2 + refreshAll
```

- **现象 / 复现路径**：24MP 图上用修复画笔按住拖动，鼠标每移动一次界面冻结数百毫秒到数秒；连续拖动几乎无法操作。
- **根因**：`cv::inpaint` 对整幅图做距离变换与迭代填充，而真正需要修复的只有 `brushRect`；`setCurrentImage` 又触发全图 `clone` + `renderToView`（见 04-31）。另外 HealTool 复用了 `CloneTool::onMousePress`，按下瞬间还会先做一次纯复制（与"修复"语义不符），且完全不使用 Alt 采样的源点。
- **修复建议**：ROI 裁剪（`brushRect` 外扩 8~16px）+ ROI 尺寸 mask 做 inpaint；`onMousePress` override 掉 base 的克隆复制；补"源点对齐"或明确标注为"污点修复（无源）"语义。
- **关联**：OpenCV 集成（03 构建）、`ImageProcessor`。

### 04-12 仿制图章源点漂移（克隆内容越来越"糊"在同一处）(P1)

- **位置**：`src/media/tools/CloneTool.cpp:127-143,145-162`
- **证据**：

```cpp
// press
m_lastDstPos = scenePos;                                  // 起点
const QPointF offset = m_lastDstPos - scenePos;           // 恒 (0,0)
paintAt(qimg, scenePos, m_samplePoint + offset);          // src = samplePoint

// move
const QPointF offset = scenePos - m_lastDstPos;           // 只是"上一次到这一次"的增量
paintAt(qimg, scenePos, m_samplePoint + offset);          // src 只跟着最后一小步抖
```

- **现象 / 复现路径**：Alt+点击采样额头 → 在脸颊上拖一笔 → 印出来的始终是额头采样点附近那一小块（同一块反复覆盖），而不是"额头到脸颊"的平行复制；拖得越远越明显。
- **根因**：偏移量按"增量"而不是"相对笔画起点"计算；PS 的"对齐"模式要求 `src = sample + (dst - strokeStart)`。
- **修复建议**：press 时存 `m_strokeStart = scenePos`，每帧 `src = m_samplePoint + (scenePos - m_strokeStart)`；增加"对齐/非对齐"复选框（非对齐 = 每个新笔画重置 offset）。
- **关联**：HealTool 继承同一套 press/release；`BrushSharedState` 的 size 应该替代 `m_brushSize`。

### 04-15 所有拖拽工具无橡皮筋预览，且与画布平移冲突 (P1)

- **位置**：`src/media/tools/RectSelect.cpp:42-53`；`src/media/tools/Crop.cpp:105-109`；`src/media/tools/PatchTool.cpp:137-140`；`src/media/tools/ShapeTool.cpp:109-114`；`src/media/imagewindow/ImageCanvas.cpp:37`
- **证据**：

```cpp
// 典型：只记录坐标，不生成任何可视 item
void Crop::onMouseMove(QMouseEvent*, ImageWindow*, const QPointF& scenePos) {
    if (!m_dragging) return;
    m_cropB = scenePos;
}
```

```cpp
setDragMode(QGraphicsView::ScrollHandDrag);   // 画布默认左键拖 = 平移
```

- **现象 / 复现路径**：选裁剪/选框/套索/修补/形状 → 按住左键拖 → 画布上看不到任何框线，而且图像跟着鼠标**平移**（`ScrollHandDrag` 同时生效），松手时坐标系已经变了 → 选区/裁剪框位置错。用户完全无法预判结果。
- **根因**：工具回调返回 `void` 且从不 `accept()` 事件，`eventFilter` 末尾又无条件调用 `QGraphicsView` 默认处理（`return QMainWindow::eventFilter(...)`），view 的 ScrollHandDrag 继续消费左键；工具层也没有任何"临时预览 item"的概念。
- **修复建议**：①`ToolState` 鼠标回调改为返回 `bool`（是否消费），`eventFilter` 按返回值决定是否继续下发；②`ToolContext::setState` 时按工具能力切 `ImageCanvas` 的 `DragMode`（选择/裁剪类 `NoDrag`，Move/Hand 类 `ScrollHandDrag`）；③每个拖拽工具在 press 时创建 `QGraphicsRectItem`/`QGraphicsPathItem`（`ZValue=998`，cosmetic 虚线）并随 move 更新。
- **关联**：05 画布、06 文字手柄拖动（已有独立 hitTest 路径可参考）。

### 04-16 任意工具双击都会创建文字 (P1)

- **位置**：`src/media/imagewindow.cpp:2774-2805`
- **证据**：

```cpp
if (event->type() == QEvent::MouseButtonDblClick) {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::LeftButton
        && !(m_mosaicTool && m_mosaicTool->isEnabled())) {   // 只排除了马赛克
        ...
        } else {
            createTextItem(sp);                              // 任何工具下都会走这里
        }
```

- **现象 / 复现路径**：选画笔 → 在画布上双击（很常见的"重落笔"操作）→ 图像上多出一个空文字 item 并进入编辑；选移动工具双击也一样。若当前是 Text 工具，一次双击还会先建一个（press 走 `Text::onMousePress`）、再建第二个（dblclick），**一次双击两个 item**。
- **根因**：文字创建逻辑写在全局 `eventFilter` 里，没有按当前 `ToolId` 门控；`ToolState` 也没有 `onMouseDoubleClick` 可以接管。
- **修复建议**：dblclick 分支先判 `m_ctx->currentToolId()==mediator::ToolId::Text`；`ToolState` 增加 `onMouseDoubleClick` 并把事件先交给工具；`Text::onMousePress` 里对"空白单击"应改为"单击只准备插入点，真正建 item 在首次输入/DblClick"（PS 语义），避免误点产生空 item。
- **关联**：`TextOverlayController::createTextItem`（06）、04-07（接口缺 dblclick）。

### 04-17 选区工具改选区不可撤销，与菜单选区命令语义冲突 (P1)

- **位置**：`src/media/tools/RectSelect.cpp:55-68`；`Lasso.cpp:58-75`；`MagicWand.cpp:59-76`；`src/app/mainwindow.cpp:2136-2160`
- **证据**：

```cpp
// 工具：直接 setMask，无命令
sel->setMask(mask, mode);

// 菜单：同样的 setMask 语义却 push 了 SelectionCommand
img->undoStack()->push(new selection::SelectionCommand(sel, selection::SelectionCommand::Op::SelectAll));
```

- **现象 / 复现路径**：拖一个选区 → Ctrl+Z → 选区没变化，反而把上一步滤镜/画笔撤销掉（用户视角是"莫名丢了一步图像编辑"）；再 Ctrl+Z 一次，`SelectionCommand` 又恢复到某个历史 mask。
- **根因**：选区变更不入撤销栈（`SelectionModel.h:59` 注释"push to undo stack from caller, NOT here"），但只有菜单入口遵守了，三个工具没遵守；`SelectionCommand` 也只有 SelectAll/Deselect/Invert 三种 Op，没有"SetMask(mask, mode)"形式。
- **修复建议**：`SelectionCommand` 增加 `Op::SetMask`（构造时存 before mask + newMask + mode），三个工具统一 push；`SelectionModel::setMask` 加 `bool` 返回是否变化以决定是否 push（避免空命令）。
- **关联**：撤销栈（00/03）、05 画布、菜单"选择"。

### 04-19 修补工具尺寸不等直接静默失败 (P1)

- **位置**：`src/media/tools/PatchTool.cpp:47-73,129-160`
- **证据**：

```cpp
if (srcClip.width() != dstClip.width() || srcClip.height() != dstClip.height()) {
    LOG_WARN("[PatchTool] src/dst size mismatch ({}x{} vs {}x{}), skipping", ...);
    return;                                   // 无 statusBar 提示
}
```

- **现象 / 复现路径**：第一次拖框选源区域（比如 120×80）→ 第二次拖到目标区域，几乎不可能拖出正好 120×80 → 直接没反应，用户只看到"修补工具坏了"。
- **根因**：P0-9.4 的"简化"用尺寸严格相等做前置条件；两次拖框都是自由手绘，尺寸必然差几像素。且两次拖拽都没有任何可视反馈（04-15），用户也不知道第一次选中的是哪里。
- **修复建议**：以源框尺寸为准，把目标框规范化（`dstRect.setSize(srcRect.size())` 围绕目标中心），或把源 `resize` 到目标尺寸再 `seamlessClone`；失败时 `statusBar()->showMessage()`；两次拖框都画橡皮筋。
- **关联**：04-15、04-11（同一族"修复"工具）。

### 04-20 工具配置持久化 API 全死，选项状态切一次丢一次 (P1)

- **位置**：`src/media/mediators/ToolMediator.h:68-94`；`src/media/mediators/ToolMediator.cpp:19-45`
- **证据**：

```cpp
// ToolMediator 提供了一套完整 per-tool 配置表……
void ToolMediator::applyToolConfig(ToolId id, const QVariantMap& cfg) { m_configs[id] = cfg; emit toolConfigApplied(id, cfg); }
QVariantMap currentConfig(ToolId id) const { return m_configs.value(id); }
// ……但全项目 grep：applyToolConfig / currentConfig / resetToolConfig / toolConfigApplied / toolConfigReset 零调用者
```

- **现象 / 复现路径**：套索设羽化 12 → 切到画笔 → 切回套索：羽化回到 0；魔棒容差、裁剪比例、修补模式、红眼 darken 全部同理；画笔大小/硬度同理（再叠加 04-05 完全不可达）。
- **根因**：设置只存在 `ToolState` 实例成员里，而实例每次切换 `new`；Mediator 的配置表是"设计了但没接"的半成品；选项页也随实例重建（`ImageOptionBar.cpp:283-293` 每次 `optionPage()` 新建再 `deleteLater` 旧页）。
- **修复建议**：定义 `ToolSettings`（`QHash<ToolId, QVariantMap>` 或强类型 struct）挂在 `ImageWindow`：`ToolState::onEnter` 读取、`optionPage` 控件变化写回；`ImageOptionBar` 常驻页（`QHash<ToolId,QWidget*>`）不再重建；Mediator 的 `applyToolConfig` 落地为唯一写入口并 `QSettings` 持久化。
- **关联**：04-39（Factory/页映射）、`BrushSharedState` 已是"共享设置"的正确雏形，应推广到全部工具。

### 04-21 鼠标事件路径上的同步调试日志 (P1)

- **位置**：`src/media/imagewindow.cpp:2619-2643`
- **证据**：

```cpp
static QFile efLog("D:/vistella_events.log");
if (!efOpened) { efLog.open(QIODevice::Append | QIODevice::Text); efOpened = true; }
...
efLog.write(QString("[EVF] %1 pos=(%2,%3) scene=(%4,%5) hit=%6 ...").arg(...).toUtf8());
efLog.flush();
```

- **现象 / 复现路径**：工具的所有输入都经过这个 `eventFilter`；每次鼠标移动都拼字符串 + 写文件 + `flush()`（同步落盘），在高刷新率鼠标/压感笔（125-1000Hz 事件）下直接拖慢整个画布；文件硬编码 `D:/`（无 D 盘机器上 `open` 失败但每帧仍会尝试判断），且文件句柄到进程退出才释放。
- **根因**：早期调试代码遗留在生产路径，没有任何编译开关。
- **修复建议**：删除（或 `#ifdef VISTELLA_TOOL_EVENT_TRACE` + `spdlog` trace 级 + 环形缓冲）。
- **关联**：性能/日志（03 构建）、04-11/04-31 的卡顿叠加。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 工具数量 | ~70+ 工具/子工具 | 15 个扁平按钮，无工具组、无长按弹出兄弟工具 | 缺失 | P1 |
| 移动工具 | 移动图层/选区/参考线，自动选择图层，显示 bbox | 空壳，只有状态栏提示（04-18） | 缺失 | P1 |
| 选框工具组 | 矩形/椭圆/单行/单列，羽化/消除锯齿/样式/宽度高度 | 只有矩形；无羽化无样式；模式下拉未接线（04-23）；无实时框 | 部分 | P1 |
| 套索组 | 套索/多边形套索/磁性套索 | 只有自由套索；羽化按整图算（04-26）；无实时路径 | 部分 | P2 |
| 魔棒/快速选择 | 魔棒（容差/连续/取样全部图层）、快速选择、对象选择 | 魔棒只有容差+连续、只比灰度、慢（04-27）；其余缺失 | 部分 | P2 |
| 选区模式/羽化/反选 | 新/加/减/交 + 羽化 + 反选，选区可撤销 | 模式仅 Lasso/MagicWand 可用且切工具即丢；羽化仅套索；工具选区不可撤销（04-17） | 有但错 | P1 |
| 蚂蚁线 | 沿选区轮廓流动虚线 | 只画 bbox 矩形（04-28） | 有但错 | P2 |
| 裁剪 | 比例/分辨率/拉直/旋转/透视裁剪/删除裁剪像素，Enter 确认 | 无比例、无旋转、无预览、松手即裁、**且完全不生效**（04-04/04-22） | 缺失 | P0 |
| 文字 | 点文字/段落文字/竖排/文字蒙版/字符与段落面板/可再编辑 | 单击建 item + 面板（字体/字号/颜色/粗斜体）；双击任意工具都建文字（04-16）；保存时烧图且不可撤销（见 09-03） | 部分 | P2 |
| 画笔 | 尺寸/硬度/间距/形状动态/散布/传递/纹理/双重笔刷/压感/混合模式 | 面板不可达（04-05）；9 字段仅 4 生效（04-29）；散布未实现；压感无（04-30）；混合模式仅 3 个且按钮失效（04-24） | 有但错 | P0 |
| 橡皮擦组 | 橡皮擦/背景橡皮擦/魔术橡皮擦 | 无橡皮擦工具（画笔 `mode==1` 分支未接 UI） | 缺失 | P1 |
| 渐变/油漆桶 | 5 种渐变 + 油漆桶 | 无 ToolId、无工具 | 缺失 | P1 |
| 加深/减淡/海绵 | 3 个色调工具 | 无 | 缺失 | P1 |
| 模糊/锐化/涂抹 | 3 个 | 无（BrushEngine 里 `mode==2` 注释 "Smudge: future"） | 缺失 | P1 |
| 仿制图章/图案图章 | 对齐/非对齐/样本图层/图案图章 | 仿制有但源点漂移（04-12）；图案图章无 | 有但错 | P1 |
| 修复组 | 污点修复/修复画笔/修补/内容感知移动/红眼 | 4 个都有但：Heal 不看采样源且整图 inpaint 卡死（04-11）、Patch 尺寸不等即失败（04-19）、RedEye pupil 参数无效（04-25）、内容感知移动无 | 有但错 | P1 |
| 历史记录画笔/艺术历史 | 2 个 | 无 | 缺失 | P2 |
| 吸管/颜色取样器/标尺/计数/注释 | 5 个取样与测量工具 | 吸管半成品（04-13）；其余全无 | 部分 | P1 |
| 路径/钢笔组 | 钢笔/自由钢笔/弯度钢笔/添加删除锚点/转换点/路径选择/直接选择 | 只有钢笔，直线折线，无贝塞尔手柄、无锚点编辑、无法闭合，Enter 也不通（04-07/04-09） | 部分 | P1 |
| 形状工具 | 矩形/圆角矩形/椭圆/多边形/直线/自定义形状/填充描边/路径运算 | 5 kind，但 Polygon/Custom 无法完成；矢量渲染缩放错误（04-10）；描边颜色被忽略（渲染端用 fill 当 pen） | 有但错 | P1 |
| 抓手/旋转视图/缩放 | 3 个视图工具 | 全靠 `ScrollHandDrag` + Ctrl+滚轮；无抓手/旋转视图工具 | 缺失 | P2 |
| 切片/框架/注释/计数 | 4 类 | 全无 | 缺失 | P3 |
| 工具选项栏 | 画布顶部横条，随工具切换，含工具预设 | 画布左侧 300px 竖列（`LeftSidePanel`），占用画布宽度；选项页随工具重建、状态不持久（04-20） | 部分 | P2 |
| 工具预设/预设库 | 每个工具的参数预设集合 | 只有画笔有 `BrushPickerDialog`；其余工具无预设 | 缺失 | P2 |
| 快捷键 | V/M/L/W/C/T/B/I/U/P/S/J… 且输入时不冲突 | 12 个字母快捷键注册为全局（04-08），另外 3 工具无快捷键；无 X/D 交换/默认色 | 有但错 | P1 |
| 压感/笔刷倾斜 | 全参数可映射压感/倾斜/旋转 | `ToolState` 无 tablet 入口，压感恒 1.0（04-30） | 缺失 | P1 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 压感笔（pressure/tilt/rotation/eraser） | `QT-NATIVE` | Qt 原生有 `QTabletEvent`（Windows 走 WM_POINTER/WinTab），`QTabletEvent::pressure()/xTilt()/rotation()` 齐全，`QWindow`/`QWidget` 都收得到；缺的只是本项目的转发与接口 | `ToolState` 增加 `onTabletEvent`；`ImageWindow::eventFilter` 转发 `TabletPress/TabletMove/TabletRelease`；`QApplication::setAttribute(Qt::AA_CompressTabletEvents)` 视情况关闭以保采样率 |
| 工具组/长按弹出兄弟工具 | `QT-NATIVE` | `QToolButton::setMenu()` + `InstantPopup`/`DelayedPopup`，PS 风格"长按弹出 + 右下角小三角"只需两步 | 建 `ToolGroup` 数据结构（一组按钮共用 `ToolId` 数组 + 上次选中项），`QToolButton` 加 `DelayedPopup` 菜单 |
| 画笔/工具自定义光标（圆圈随尺寸缩放） | `QT-NATIVE` | 两种做法：`QCursor(QPixmap)`（受最大 32/64px 限制）、或画 `QGraphicsEllipseItem` 跟随鼠标（项目已有 `m_brushCursor` 雏形） | 统一用 canvas 上的 `QGraphicsItem` 作为工具光标，按 `brushSize` 改 radius（已有 `ImageCanvas::setBrushCursorRadius`），并在进入/离开工具时显隐 |
| 光标随工具切换 | `QT-NATIVE` | `QWidget::setCursor(QCursor)` + `ToolContext::cursorChanged` 已存在，只是**没有任何订阅者** | `ImageCanvas` 连接 `ToolContext::cursorChanged` → `viewport()->setCursor(...)` |
| 视图缩放/抓手/旋转画布 | `QT-NATIVE` | `QGraphicsView::scale/rotate/setDragMode(ScrollHandDrag)` 全原生；旋转视图就是 `view->rotate(angle)` | 加 HandTool（临时按 Space 切 ScrollHandDrag）、RotateViewTool（`QGraphicsView::rotate`），缩放工具（点击缩放/框选缩放 `fitInView`） |
| 透视图裁剪（四角单应） | `QT-NATIVE` | `cv::getPerspectiveTransform + warpPerspective` 项目已用（`ImageProcessor::warpPerspective`，TransformTool 的 Distort 模式） | 裁剪工具加 4 角拖动 + 单应 warp；裁剪框用 `QGraphicsItem` 画 |
| 贝塞尔路径/锚点编辑 | `QT-EFFORT` | `QPainterPath`/`QGraphicsPathItem` 提供几何与渲染，但"锚点选择/手柄拖动/转换点/路径运算"要自研交互层（约 5-8 人天） | 建 `PathEditor`（每锚点一个可拖 handle item），先用直线+二次贝塞尔，再补路径运算 |
| 连续笔画性能（24MP 实时） | `QT-EFFORT` | 现状每帧整图 `matToQImage`/`setCurrentImage`/全图 `QImage::setPixelColor`（04-31）不可能流畅；Qt 侧 `QGraphicsView` + 单张 pixmap 也不是为增量绘制设计。需要自研 tile/dirty-rect 合成（项目已有 `ImageCanvas::invalidateRect` 雏形 P7.11） | 笔画期间只对 ROI `viewport()->update(rect)`；`QImage` 直接原地写 + `QPixmap::fromImage` 局部；若要 GPU，用 `QOpenGLWidget`/RHI（项目已有 `gpu/`） |
| 内容识别填充/修补 | `QT-3RD` | 已有 OpenCV 4.12（`cv::inpaint`/`seamlessClone`），但 PS 级"内容识别"需 PatchMatch 类算法 | 先修 04-11/04-19 让 OpenCV 路径可用；再评估 `opencv_contrib::xphoto` 或自研 PatchMatch（约 10-20 人天） |
| 快速选择/对象选择（GrabCut） | `QT-3RD` | OpenCV `cv::grabCut` 可用，交互与迭代收敛需自研；对象检测需 ONNX runtime（重） | `grabCut` 最小版（2-3 人天）；对象选择标注"暂不支持" |
| 磁性套索 | `QT-EFFORT` | 需要边缘代价图 + 最短路径（Live-Wire），OpenCV 可算 Canny/梯度，Dijkstra 自研 | 约 3-5 人天；先做"吸附到边缘"近似版 |
| 图案图章/渐变工具 | `QT-NATIVE` | `QBrush(QPixmap)` + `setBrushOrigin`、`QLinearGradient/QRadialGradient/QConicalGradient`；图案/渐变数据面板已有（`GradientTab/PatternTab`，虽为假数据） | 各 1-2 人天；GradientTool 一次拖拽生成 ramp 写 ROI；PatternStamp 复刻 CloneTool 结构 |
| 加深/减淡/海绵/模糊/锐化/涂抹 | `QT-NATIVE` | 都是"按 stamp 权重做像素函数"，`BrushEngine::applyStamp` 的 `mode` 参数已预留（`mode==1 Erase`、`mode==2 Smudge: future`） | 扩展 `applyStamp` 的 `mode` 枚举 + 亮度/饱和度/卷积核；每个 0.5-1 人天 |
| 历史记录画笔 | `QT-EFFORT` | 需要"历史快照 + 按 stamp 从快照恢复像素"，依赖当前 `QUndoStack`（已有 200 步上限）但需要暴露每步快照 | 用 `QUndoStack::command(i)` 的 image 快照做 source，约 2-3 人天 |
| 颜色替换/背景橡皮擦/魔术橡皮擦 | `QT-EFFORT` | 算法都是"按颜色距离做 alpha"，可复用 MagicWand 的 BFS + Brush stamp | 各 1 人天 |
| 标尺/计数/注释/切片 | `QT-NATIVE` | `QGraphicsItem` + `QPainter` 自绘即可，无平台依赖 | 低优先级，各 0.5-1 人天 |
| 工具预设面板 | `QT-NATIVE` | `QListWidget`/`QListView` + `QSettings`/JSON 持久化即可 | 2 人天（画笔预设已有 picker 可参考） |

## 5. 设计问题

1. **工具状态不在文档模型里**：画笔/修复/红眼直接改 `m_current` 缓存（04-02），只有 MaskBrush 走 `LayerStack`。后果：图层、撤销、保存、导出四条链路对"工具改了什么"认知不一致。建议：定义 `ToolEditTarget`（活动图层 / 蒙版 / 文档），所有工具通过它提交编辑；`ImageEditCommand` 的整图语义限定给"文档级"操作。
2. **三层真源 + 两条切换路径**：`ToolMediator::m_current` / `ToolContext::m_current` / `LeftToolBar` check 状态，`toolConfig*` API 完全没人调用（04-20），`Ctrl+T` 又绕过 Mediator（导致 Ctrl+T 后 Mediator 仍认为旧工具是当前工具，点同按钮会把工具取消掉）。建议：`ToolContext` 是唯一状态机，Mediator 降级为"命令总线"，`setState` 内同步回 `m_toolMed->m_current`；左侧按钮高亮由 `ToolContext::toolChanged` 单向驱动。
3. **`ToolState` 接口设计不足以支撑 PS**：只有 5 个回调（无 double-click/wheel/tablet/context-menu/drag-drop/enter-leave），无返回值（无法 accept 事件）、非 QObject（无法发信号，只能用 lambda 捕获 `this`，选项页因此在工具销毁后仍持野指针 04-40）。建议：接口改为 `QObject` 基类 + `bool` 返回 + `onTabletEvent`/`onDoubleClick`/`onWheel`/`onEnterLeave`；事件转发改为"先给工具，未 accept 再给 view"。
4. **工具实例 = 配置存储**：每次切换 `new ToolState`，所有 per-tool 设置（羽化/容差/比例/画笔大小/修补模式）随之丢失（04-20）；选项页也随实例重建。建议：拆成 `ToolSettings`（持久，Mediator 管）+ `ToolSession`（每次切换重建，管笔画中间态）。
5. **选项页按 `ToolId` 数值当 page index 硬编码**：`.ui` 里 17 个占位页、`idx>=count` 动态分支不可达（04-39）、Brush 页被 Mosaic 抢占（04-05）。建议：`QHash<ToolId, QWidget*>` 显式映射，页创建一次、常驻、切页不重建。
6. **UI 层与工具层互相穿透**：`ImageOptionBar` 用 `dynamic_cast<Crop*>/<Brush*>/<MagicWand*>` 直接读写工具内部状态（`ImageOptionBar.cpp:197/211/258/268/277`、`ModeSelectorBar.cpp:245-327`），工具换成别的实现类就静默失效。建议：抽 `IToolOptions` 接口（`set/get` 参数 + `changed()` 信号），UI 只依赖接口。
7. **调试代码与硬编码布局混在生产路径**：`eventFilter` 每事件写 `D:/vistella_events.log`（04-21）；`LeftToolBar.ui` 用绝对几何画色块（04-37）；`AppBar` 单字母按钮。建议：清理调试块 + 统一 layout/qss。
8. **可测试性**：现有 QTest（`tst_F_N_ToolCursor` 等）只断言 `cursor()` 返回值/`ToolContext` 转发次数，因此"cursor 从未被 set"（无订阅者）、"Crop 不生效"、"Brush 选项不可达"这类问题测试全绿。建议：选项栏/工具栏测试断言"用户可见结果"（page 当前索引、画家输出像素、undo 栈条数），而不是 accessor。

## 6. 修复建议（按优先级排序）

**P0 立即修（小时级）**
- 04-01：`ToolContext::toolChanged` 前/后清 `ImageCanvas::m_box`（`setTransformBox(nullptr)`）——0.5 h，防崩。
- 04-02：笔画写图层而不是缓存（Brush/Heal/Patch/RedEye 统一走 `activeLayer->image` + `LayerCommand`）——1-2 天，风险中（涉及合成器）。
- 04-03：`BrushEngine::applyStamp` 按 channels 分派（4 通道用 `Vec4b`、灰度按颜色亮度）——0.5 天，风险低。

**P1 本轮修（1-3 天）**
- 04-04 裁剪落地 + 尺寸变化通知链——0.5 天。
- 04-05 删除 Mosaic 抢占分支，Brush 页常驻——0.5 h。
- 04-06 删除 LeftToolBar 里的 `setState` 与 15 个 factory 包装——0.5 天。
- 04-07 键盘/双击转发 + `ToolState` 接口补 3 个虚函数——0.5 天（连带修好 Polygon/Pen/Shift）。
- 04-08 工具快捷键改为 canvas 焦点域 / 编辑态禁用——0.5 天。
- 04-09/04-10/04-12/04-13/04-14/04-18/04-19：逐个 0.5-1 天的确定性修复（crop 比例、矢量坐标、克隆偏移、吸管接线、颜色真源、Move 最小版、Patch 尺寸归一）。
- 04-15 橡皮筋预览 + per-tool DragMode——1.5 天。
- 04-20 `ToolSettings` 持久化 + 选项页常驻——1-2 天。
- 04-21 删除事件调试日志——5 min。
- 04-11 Heal ROI inpaint——0.5 天。

**P2 后续（各 0.5-2 天）**
- 04-22～04-42 中裁剪增强、蚂蚁线轮廓、魔棒性能、选区撤销、画笔 9 字段/压感、MaskBrush 撤销、工具栏图标/主题、AppBar i18n、i18n 统一。

**架构级重构（2-3 周）**
- `ToolState` 接口重做（QObject + accept + tablet + 事件优先级）+ `ToolSettings` 持久层 + `IToolOptions` UI 接口；
- 工具写图层/蒙版/文档的 `ToolEditTarget` 抽象（与 07 图层、08 合成器对齐）；
- 画布增量绘制（tile/dirty-rect + 可选 GPU 笔刷路径），这是"接近 PS 的绘画手感"的前提；
- 工具组/预设/快捷键盘（`ToolGroup` + `ToolPreset` 数据结构 + 持久化）。

## 7. 需要 Lead 协调的跨模块问题

- **04-01（TransformBox UAF）** 需要改 `ImageCanvas`（05 画布模块）与 `TransformTool` 的持有关系，单修工具层不够。
- **04-02（笔画不进图层）** 需要 07 图层提供"活动图层像素写入 + 按图层 undo"的 API；`ImageEditCommand`（05）的整图 no-op redo 约定要重新设计。
- **04-10（矢量层坐标）** 需要 07 决定 Vector layer 的坐标契约（图像像素 vs 0..100 归一化），渲染/工具/路径编辑三处统一。
- **04-13/04-14（颜色/吸管真源）** 与 09 报告 09-01（`RightPanelDock::colorDock()` 死链）同源：需要一次"颜色单一真源"改造，涉及 `LeftToolBar`(04) / `ImageWindow`(05) / `ColorTab`(09)。
- **04-07/04-08（键盘路由）** 与 `ImageWindow::keyPressEvent`（05）和 `MainWindow` 的 QAction 快捷键（03/00）冲突，需要定义"焦点优先级：文本编辑 > 对话框 > 画布工具 > 全局工具快捷键"。
- **04-16（双击建文字）** 与 05 的 `eventFilter` 文字分支、06 `TextOverlayController` 交叉；若按 09-03 去掉保存时 `flattenText`，还需要"文字图层化/工程格式"设计（07）。
- **04-27/04-28（魔棒性能/蚂蚁线轮廓）** 需要 selection 模块（05/09）配合：mask→轮廓提取建议做成 `SelectionModel` 的缓存属性，供 canvas 与工具共用。
- **04-39（Transform 不在 Factory）** 若 05/菜单后续统一走 `ToolMediator::switchTool(Transform)`，Factory 必须先支持 `Transform`，否则 `setState(nullptr)` 会静默清空工具。
