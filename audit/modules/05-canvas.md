# 05 · 画布与文档窗口 — 审计报告

> 范围：`src/media/imagewindow.cpp`(实测 3055 行，任务书标 2876) / `imagewindow.h`(实测 644 行，任务书标 559) /
> `imagewindow.ui`(34 行)；`src/media/imagewindow/**` 14 文件 2958 行
> （ImageCanvas 340+179、ImageIOController 277+48、AdjustmentPanel 955+243、MosaicTool 233+98、
> MosaicOptionPanel 178+54、TextOptionPanel 203+61、TextOverlayController 276+123、camera_raw 4 文件 203+57+152+80）。
> 上下文另读了 `imageprocessor.cpp`、`graphicstextitem.cpp`、`LayerStack.cpp`、`LayerCommand.cpp`、
> `FilterDialog.cpp`、`FilterCommand/TransformCommand.cpp`、`ImageOptionBar.cpp`、`RightPanelDock/LayersDock.cpp`、
> `mainwindow.cpp`（open/save/close/tab 链路）。全部逐行通读。
> 审查者：audit-canvas

## 1. 模块概览

`ImageWindow`（QMainWindow，作 QTabWidget 的页）是「一份图像文档」的全部：数据（`m_original`/`m_current`/
`m_layerStack`）、视图（`ImageCanvas` = QGraphicsView + QGraphicsScene + 1 个 pixmap item + 文字 item）、
交互（`eventFilter` 包办滚轮/点击/拖动/双击/右键，再转发给 `ToolContext`、`MosaicTool`、`TextOverlayController`）、
右侧面板（`RightPanelDock` + `AdjustmentPanel` + `LayerPanel`）、IO（`ImageIOController`）、撤销（`QUndoStack`，
命令类型 8 种以上）、异步（`EngineContext` + `m_currentTaskId`）。

调用方向：`MainWindow`（tab/菜单/快捷键）→ `ImageWindow`（公开 API）→ 组件（`MosaicTool`/`TextOverlayController`/
`AdjustmentPanel`/`ImageIOController`）→ 直接改 `ImageWindow` 私有成员（靠 6 个 `friend class`）；
`ImageWindow` → `LayerStack::render()/renderAsync()`、`ImageProcessor`、`ImageCanvas`。反向：组件用裸指针/QPointer
回指 `ImageWindow`，通过 `renderToViewPublic()/currentImage()/undoStack()` 等公开缺口改宿主状态。

核心数据流：`LayerStack::render()`（默认走 P8 compositor：`LayerStackAdapter` 建图 → `RenderGraph` 按 `TileGrid` 逐瓦片 `compute()` → 拼成 `CV_8UC3` canvas，
见 `LayerStack.cpp:1551-1557`、`RenderGraph.cpp:56-78`）→ `m_current`（当作缓存）→ `ImageProcessor::matToQImage` → `QPixmap` →
`pixmapItem()->setPixmap()`；`ImageIOController::onSave` 直接把 `m_current` 写盘。**`m_current` 同时被当作
"渲染缓存"和"就地编辑画布"两种角色，这是本模块一半缺陷的总根源**。

## 2. 缺陷清单

> 编号说明：按发现顺序编号；**05-35 是复核 compositor 链路时新增的 P0**，排版上插在 05-08 之后（为不破坏交叉引用未重排编号）。
> 共 38 条：P0 9 条、P1 18 条、P2 9 条、P3 2 条（每条 P0/P1 下面都有展开）。

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| 05-01 | P0 | ImageIOController.cpp:75,101,128 | data-loss | Ctrl+S / 另存为 / 导出 都会把文字烧成像素且不可撤销；**导出在用户还没选路径（甚至取消）之前就已烧图** | 三个入口开头无条件 `flattenText()`，而 `flattenText` 是 `removeItem+delete` 的破坏性操作、不入撤销栈 | 保存/导出路径禁止调用 `flattenText`；合成文字只在"导出为拍平格式"时做一次性渲染到临时 Mat，不动 scene |
| 05-02 | P0 | TextOverlayController.cpp:196-207 + imagewindow.cpp:1850-1861 | data-loss | 保存一次后文档自身被改坏：多图层被重复合成（下半层像素被算了两次）、`m_original` 变成合成结果、后续撤销基线错 | `flattenText` 用 `setCurrentImage(合成图)`，而 `setCurrentImage` 会 `m_original=img` **并把整张合成图写进 base layer**，其余图层仍在栈上等下一次 render 再叠一遍 | `flattenText` 应走 `ImageEditCommand` 或只更新 base *在不改变 m_original 的前提下*；`setCurrentImage` 不应写 `m_original` |
| 05-03 | P0 | imagewindow.cpp:1894,1964-1971 + ImageIOController.cpp:77,103 | data-loss | 隐藏图层 / 应用智能对象变换 / 开关蒙版后立刻 Ctrl+S，存进文件的是**操作前的旧像素** | `m_currentDirty` 只在 `renderToView()` 里被动重建，`onSave` 不先 `rebuildCurrentCache()`；异步路径下 `m_currentDirty` 在回调回来前一直为 true | `onSave/onSaveAs` 开头 `if (m_currentDirty) rebuildCurrentCache()`（并等异步完成）或直接用 `m_layerStack->render()` 现算 |
| 05-04 | P0 | LayerStack.cpp:1775-1809 + imagewindow.cpp:579-608,1964-1971 | crash | 大图（>1MP）在后台 render 未完成时关闭 tab / 关主窗口 → 在下一个事件循环里回调触碰已析构的 `ImageWindow`/`LayerStack` | `renderAsync` 用 `QCoreApplication::instance()` 当 `invokeMethod` 的 receiver（不是 `ImageWindow`），回调持裸 `this`/`self`；`~ImageWindow` 从不 `setAsyncEnabled(false)`（LayerStack.cpp:1734 注释自己要求这么做，但没人实现） | 用 `QPointer<ImageWindow>` 做 receiver/守卫 + 每次提交带 generation token；`~ImageWindow` 先 `m_currentTaskId.fetch_add(1)` 作废在飞任务，再 reset engine |
| 05-05 | P0 | ImageOptionBar.cpp:157-185 + MosaicOptionPanel.cpp:22-37 + TextOptionPanel.cpp:23-34 | crash | 打开图 A → 切到画笔（创建 Mosaic 选项页）→ 关掉 A 的 tab → 打开图 B → 操作"笔刷大小/类型"或文字面板 → 访问已析构的 A 的 `MosaicTool`/`TextOverlayController` | 选项页 `ensureMosaicPage/ensureTextPage` 只建一次（`m_mosaicPageIdx>=0` 直接 return），页面里存的是**无 QPointer 的裸指针**，`attach()` 换文档时不重建页面 | 面板改存 `QPointer<MosaicTool>`，`attach()` 时 `rebind()` 或按文档销毁旧页重建；`detach()` 里 `->setTool(nullptr)` |
| 05-06 | P0 | TextOverlayController.cpp:199-207 + imagewindow.cpp:2668-2670 | crash | 拖动文字控制点期间按 Ctrl+S → 之后鼠标一动就崩（UAF） | `flattenText` 删掉所有 item 却没清 `m_handleDragItem`；`eventFilter` 每帧都 `m_textCtrl->handleDragItem()->continueHandleDragFromView(sp)` | `flattenText/removeAllFromSceneAndDelete` 都置空 `m_handleDragItem`；`handleDragItem()` 改返回 `QPointer` |
| 05-07 | P0 | imagewindow.cpp:857-861 | data-loss | 打开 16bit PNG/TIFF/HDR，全图惨白（几乎全 255） | `img.convertTo(tmp, CV_8U)` 默认 `alpha=1`，16U 值被 saturate 到 255 而不是缩放到 0-255 | `convertTo(tmp, CV_8U, 255.0/65535.0)`；并保留 16bit 文档按 16bit 流水线处理 |
| 05-08 | P1 | imageprocessor.cpp:126-135 + TextOverlayController.cpp:179 | data-loss | 4 通道图走 legacy 渲染路径（或 compositor 关闭）时显示花屏（通道错位）；`flattenText` 也走这条转换，之后保存把花屏写进文件 | `matToQImage` 对 4 通道先转 RGBA，却用 `QImage::Format_RGB888` + `step=cols*4` 包装，把 A 字节当成 R 读了 | 4 通道用 `Format_RGBA8888`；`flattenText` 先确认通道数再合成 |
| 05-35 | P0 | LayerBitmapNode.cpp:39-43 + RenderGraph.cpp:56-72 + LayerStack.h:327 | data-loss/ui-visual | **打开灰度图（单通道 PNG/JPG）或带 alpha 的图，画布全黑**（用 P8 默认开启的 compositor 渲染路径时） | 根节点 `LayerBitmapNode` 按 `layer->image.type()` 返回 8UC1/8UC4 瓦片；`RenderGraph` 的 canvas 固定 `CV_8UC3`，`tile.type()!=dst.type()` 时**静默跳过**（RenderGraph.cpp:68-72）→ canvas 保持全 0 | RenderGraph/节点统一在入口做通道归一化（1ch→BGR，4ch→BGRA2BGR），或 adapter 给 base 套一个 `ColorConvertNode`；类型不匹配至少要 LOG_WARN |
| 05-09 | P0 | imagewindow.cpp:235-245,564 + MosaicTool.cpp:173 | memory | 24MP 图每涂一笔占 144MB 撤销内存（before+after 全图 clone），100 笔就 ~14GB，长会话必 OOM | `ImageEditCommand` 存两份全图 clone；`QUndoStack::setUndoLimit(200)` 按条数而非字节限制 | 马赛克/画笔类按 ROI 存 patch + 坐标；或按总字节预算裁剪栈（自建 UndoStack 子类估算 `Mat.total*elemSize`） |
| 05-10 | P1 | AdjustmentPanel.cpp:687,702,714,742,758 + imagewindow.cpp:1850-1853 | logic | 拖一次"色阶 Gamma"滑块，图像一次比一次更暗/更灰——**每帧都在已调过的结果上再调一次**；并且之前的画笔/马赛克涂抹被丢掉 | 每次 `applyCurrentTab` 都对 `m_host->originalImage()` 做 LUT，而结果经 `setCurrentImage` 又覆盖了 `m_original` | 引入独立的 `m_baseImage`（真正的原图，只读），LUT 永远作用在它上面；`setCurrentImage` 不再写 `m_original` |
| 05-11 | P1 | AdjustmentPanel.cpp:772-800 + LayerCommand.cpp:757-771 | logic | 大图上调参数后 Ctrl+Z 没反应（这次调整根本没进撤销栈）；能进的也**永远无法 redo** | debounce 200ms 到点时异步结果多半还没回来，`after==m_current` 触发 memcmp 相等 → 跳过 push；`SetAdjustmentLut::redo` 恒 no-op | commit 时不要用 `m_current` 当 after，用"提交给异步的 LUT 作用在 base 上的结果"；redo 恢复 `m_newMat` |
| 05-12 | P1 | imagewindow.cpp:1894,1964-1971 + AdjustmentPanel.cpp:711,739 | state-sync | 大图上调整参数经常"按了没反应"，或者显示的不是当前图层合成结果；CPU 空转 | 两个子系统**共用同一个** `m_currentTaskId` 计数器，互相把对方的结果判为过期丢弃；被丢弃后 `m_currentDirty` 永远保持 true，于是每次 `renderToView` 都重投一次全图 render | 任务 ID 分域（render 一个、adjust 一个），或统一成一个任务队列；被丢弃时也要把 dirty 状态归位 |
| 05-13 | P1 | imagewindow.cpp:979,987,1006,1026,1047,1391,1428,1446,1530,1574,2945 | logic | "刷新智能对象"、"切换嵌入/链接"、"改图层名/锁定/链接"、"应用智能对象变换"等操作后画面完全不更新，要等下一次其它操作才变 | 12+ 处只 `invalidateCurrentCache()` 不 `renderToView()`；全项目有 53 处手写 invalidate/render 组合，漏一处就是静默 bug | 收敛成 `ImageWindow::invalidateAndRender()` 单一入口（或给 `invalidateCurrentCache()` 加"脏即调度一次 `QTimer::singleShot(0)` 重绘"） |
| 05-14 | P1 | imagewindow.cpp:2097-2100,2113-2122 + LayerCommand.cpp:439-443 | state-sync | 撤销"隐藏图层/改不透明度/混合模式"后，图层列表变了但画布还是旧的 | `onStackChanged` 只 emit `dirtyChanged`，不 invalidate 不重绘；`LayerCommand::undo` 直接写 `l->visible=…`，绕开 `LayerStack::setVisible` 的 `layerChanged` 信号 | `onStackChanged` 里统一 `invalidateCurrentCache()+renderToView()`；LayerCommand 改走 setter |
| 05-15 | P1 | MosaicTool.cpp:114-121,146,221-223 | data-loss | 涂抹完，切一下图层可见性或按一次 Ctrl+Z 再 Ctrl+Y，笔迹就消失；"原图"也被涂掉 | 涂抹 in-place 改 `m_current` 和 `m_original`，**从不写回 layerStack 的 base layer**；任何 invalidate→render 都用未涂的图层重算 | 涂抹结束把 ROI patch 应用到 `baseLayer()->image` 并 invalidate；`m_original` 只读 |
| 05-16 | P1 | MosaicTool.cpp:42-73,160-177 | data-loss | 涂到一半点"退出涂抹模式"或切工具：已改的像素留在画面上，但**撤销栈里没有记录**，Ctrl+Z 撤不掉 | `setEnabled(false)` 里 `m_strokeOpen=false; m_backup.release()`，直接丢弃 undo 基线；release 事件又因 `isEnabled()==false` 不再调 `onSceneDragEnd` | 退出前先 `if (m_strokeOpen) onSceneDragEnd()`；`setEnabled(false)` 只做可见性，不动 stroke 状态 |
| 05-17 | P1 | imagewindow.cpp:1905-1910 + imageprocessor.cpp:112-136 + MosaicTool.cpp:224 | perf | 24MP 图上拖一次滑块/画一笔马赛克，UI 卡成幻灯片（每帧数百 ms） | 每次 `renderToView` 都做：render 全图 → `cvtColor` 全图 → `.copy()` 全图 → `QPixmap::fromImage` 全图 → `setSceneRect+resetTransform+scale`；滑块一次 tick ≥8 份全图分配 | 缓存 `QImage`、只对脏矩形更新（已有但没用上的 `ImageCanvas::invalidateRect`）；马赛克/画笔只重传笔刷包围盒 |
| 05-18 | P1 | imagewindow.cpp:2616-2643 | perf | 鼠标移动明显发涩，且 D 盘根目录被写一个 `vistella_events.log` 无限增长；没有 D 盘的机器上每次事件都白做一次失败 open | `eventFilter` 里 `static QFile efLog("D:/vistella_events.log")`，每个 MouseMove 都 `write+flush`（同步 IO） | 删除调试日志；需要跟踪时用 `spdlog` + 运行时开关 |
| 05-19 | P1 | ImageCanvas.cpp:62-89,111-125 + imagewindow.cpp:1908-1910 | logic | Ctrl+滚轮缩放不以鼠标为锚点（`resetTransform` 抹掉 AnchorUnderMouse，`[待确认]` 见展开）；Ctrl+`-` 按几十次后图像缩到看不见、再放大要按同样多次；窗口变大后"适合屏幕"不会跟着适配 | 每次缩放都是 `resetTransform()+scale()`，把 `AnchorUnderMouse` 和原平移一起清掉；`onZoomIn/Out` 完全没有上下限（只有 wheel 有 0.05~20）；`m_zoom` 与 view transform 两份状态 | 保留一个 `QTransform` 直接 `setTransform`（或 `scale()` 累乘不 reset）；上下限抽成 `clampZoom()` 给三个入口共用；`resizeEvent` 里按 fit 模式重算 |
| 05-20 | P1 | ImageIOController.cpp:88-111 + imageprocessor.cpp:32-110 | data-loss | 保存丢 alpha（拍平到 base 后通道数变了）、丢 ICC、丢 EXIF/DPI；另存对话框只给 png/jpg/bmp；存成 `.psd`/`.webp` 之类直接报"保存失败"；重新打开只剩一张拍平位图 | `saveImage` 只按扩展名交给 Qt 写像素；`onSave/onSaveAs` 没有任何元数据参数；项目没有分层容器格式 | 保存链路加 `SaveOptions{icc, exif, dpi, keepAlpha, format}`；读 EXIF 方向；提供 PSD/TIFF(layer) 分层的"项目文件"格式 |
| 05-21 | P1 | imagewindow.cpp:135-216 + TextOverlayController.cpp:147-156 | logic | 打字 → Ctrl+Z（item 被删）→ Ctrl+Y：文字内容**变空**；撤销"删除文字"后重新出现的 item 再编辑/拖动，撤销栈不再记录 | `Add::undo` 删掉 item 只更新自己的 `m_item`；同栈后面 `Change/Move/Rotate` 命令仍持旧 item 的 QPointer（已 null）→ redo 静默 return；`Remove::undo` 重建 item 时漏了 `connectTextItemSignals` 且不恢复字体/颜色 | 文字用稳定 ID（map<id,item*>）而不是指针；Add/Remove 的 undo/redo 负责重建后重新绑定所有相关命令 |
| 05-22 | P1 | TextOverlayController.cpp:187-193 + graphicstextitem.cpp:58-76 | data-loss | 旋转或缩放过的文字，保存后位置/角度/大小全变（平铺在原始位置、无旋转） | `GraphicsTextItem` 用 `setTransform(T(c)·R·S·T(-c))` 实现旋转缩放，所以 `QGraphicsItem::rotation()` 恒 0、`scale()` 恒 1；烧图却用这两个 API | 烧图用 `item->transform()` 或直接 `QGraphicsScene::render` 到该区域；更简单：烧图改用 `item->paint()` 到 QPainter |
| 05-23 | P1 | imagewindow.cpp:848-1810 | data-loss | 在同一个窗口二次载入文件（`ImageWorkspace::loadFile` 路径）会：不提示未保存、直接清撤销栈、旧的文字 item 留在新图上、旧 `LayerPanel` 靠 deleteLater 慢慢死 | `loadFile` 是"就地重置"，但只重置了 layerStack/undo/zoom，没有 load 前的 dirty 确认、没有 `m_textCtrl->removeAllFromSceneAndDelete()` | 拆成 `newDocument()`（唯一入口）与 `attachLoadedImage()`；load 前走统一 `maybeSave()`；`loadFile` 前置清 scene items |
| 05-24 | P1 | ImageIOController.cpp:49-64,227-268 + CameraRawLoader.cpp:59-65,136,164-186 | logic | RAW 文件根本无法通过"打开"进入 Camera Raw（libraw 装了也没用）；即使进入，`m_filePath` 会变成临时 PNG，Ctrl+S 存到 `%TEMP%`，原 RAF 永远不被覆盖且临时文件不删；色温滑块无效 | `onOpen()` 里调的是 `m_host->loadFile()` 而不是 `m_io->loadFile()`（RAW 拦截在后者里），且 `onOpen` **全项目无调用者**（MainWindow 直接 `img->loadFile`）；`wbTempToKelvin()` 定义了从未使用；`output_bps=16` 时按 `CV_8UC3` 解释缓冲 | 把 RAW 判断前移到 `ImageWindow::loadFile`；解码后记住 `originalRawPath` 用于保存对话框默认名；`wbTemp` 真正映射到 `user_mul`/`libraw` 色温；按 `output_bps` 选 `CV_16UC3` |
| 05-25 | P1 | imagewindow.cpp:2745-2768,2774-2805 | ux | 用裁剪/选框/画笔工具时，双击图像会凭空多出一个文字层；右键点一下文字就把它删掉（无确认、可撤销但反直觉） | 双击/右键分支只判断"是否马赛克模式"，不判断当前工具 | 只在 `ToolId::Text` 下允许双击建字；右键改为上下文菜单（删除/复制/字体…） |
| 05-26 | P1 | imagewindow.cpp:579-608 + imagewindow.h:619-641 | design | 析构注释描述的所有防护理由都不成立（详见 05-26 展开）：按文档改代码会改错 | dtor 注释称 `delete ui` 销毁 m_canvas、称 `QPointer<QGraphicsEllipseItem>` 因多级继承失效——两条都是事实错误 | 重写析构：`m_canvas->scene()->clear()` 概念上交给 Qt；用 `QPointer` 只对 QObject；把 dtor 注释改成事实 |
| 05-27 | P2 | imagewindow.cpp:311-322 | memory | 每个 ImageWindow 泄漏一个空 `QGraphicsView`（含 viewport、scene 指针等） | `ui->graphicsView->setParent(nullptr)` 后 hide，再没人 delete；`delete ui` 不删控件 | 直接 `delete ui->graphicsView` 并把 `ui->graphicsView` 置空；或从 .ui 删掉该占位 |
| 05-28 | P2 | imagewindow.cpp:1906 | perf/limit | 超大图（单边 > ~32767，或总像素超 QPixmap 上限）画布全白/空白 | 整图转一张 `QPixmap` 塞进单个 pixmap item | 按瓦片切成 N 个 `QGraphicsPixmapItem`，或 viewport 自绘（衔接 P8 backend） |
| 05-29 | P2 | ImageCanvas.cpp:328-338 | ui-visual | 选区永远显示成一个矩形虚线框，套索/魔棒的真实形状看不出来；没有 PS 的黑白交替蚂蚁线 | `drawForeground` 只 `drawRect(sceneBbox)`，注释也承认是简化 | 从 mask 提轮廓（`cv::findContours` → `QPainterPath`）或用 `QRegion`；黑白双 pen 各画一次 |
| 05-30 | P2 | imagewindow.cpp:1946-1948 | perf/logic | 异步/同步阈值判据用的是 `m_original.size()`；`m_original` 会被 `setCurrentImage`/马赛克改写，阈值跟着漂 | 同 05-02/05-15 的 `m_original` 被污染 | 用文档尺寸成员（`m_docSize`）而不是图像数据推断 |
| 05-31 | P2 | ImageIOController.cpp:270-277；imagewindow.cpp:2293-2382,2843；ImageCanvas.cpp:91-109；ImageCanvas.h:48-54；imagewindow.h:128-173；MosaicTool.cpp:98-101,228-233 | design | 大量空壳/死代码：`ImageIOController::save/saveAs/promptFor*/writeImage`、`onOpen` 无调用者、5 个 no-op slot、`ImageCanvas::hitTest/setBrushCursorRadius/moveBrushCursor/hideBrushCursor`（`m_brushCursor` 从未赋值）、`invalidateRect` 只有测试用、`ImageEditCommand::ParamSet+applyParams` 全链路（`applyParams` 是空函数）、`m_ctrlDown` 只写不读、`MosaicTool::installBrushCursor/ensureBackup` | 从 P0 起一路"先留接口后实装"但从未回收 | 一次性清理：删接口或用 `[[deprecated]]`+TODO 标注；`applyParams` 要么删要么实装 |
| 05-32 | P2 | imagewindow.cpp:1800 | logic/i18n | 属性面板永远显示 72 dpi；缩放/文档尺寸等信息缺失 | `info.dpi = 72;` 硬编码，注释写"后续 P0-8 ICC embed 流程可读 EXIF" | 保存/载入时读写 DPI（PNG pHYs / JPEG JFIF / TIFF Resolution）；无值时不显示 |
| 05-36 | P2 | imagewindow.cpp:1821-1824,2091-2100 | state-sync | `isDirty()` 与实际文件内容不等价：保存时 `flattenText`/`setCurrentImage`/`applySmartObjectTransform` 等改动不进撤销栈，于是"已保存"标记可能盖住未落盘修改；反向地，loadFile 清栈瞬间 `m_savedIndex` 仍是旧值，会闪一下 `*` | `isDirty` 只看 `QUndoStack::index() != m_savedIndex`，而项目的"文档变更"有相当一部分绕过撤销栈 | 给文档加 `m_docRevision`（任何像素/图层/文字变更都 +1），`isDirty = revision != savedRevision`；保存前强制同步缓存并落 revision |
| 05-37 | P2 | imagewindow.cpp:1878-1889,579-608 + FilterDialog.cpp （无 `reject()` 重写） | memory/ux | 预览 Mat 泄漏：`s_previewRegistry` 在 `~ImageWindow` 不清理；且 FilterDialog 按 Esc/标题栏 X 关闭时 `clearPreview()` 不执行 → 画布永久显示预览图（与文档内容不一致），直到用户再开一次滤镜点 Cancel | 预览状态放在文件作用域 static 表 + 裸 `ImageWindow*` 作 key；`FilterDialog` 只在 `onCancelClicked` 里清 | 预览 Mat 改成 `ImageWindow` 成员（或 `QHash<QPointer<...>>`）；`FilterDialog::done()`/析构里统一 `clearPreview()`；与 09-09 合并修 |
| 05-38 | P2 | MosaicTool.cpp:89-94 + imagewindow.h:275-282,631 | crash(risk) | 笔刷光标 `m_brushCursor` 是裸 `QGraphicsEllipseItem*`，`MosaicTool::setSize` 的注释却声称"P2.5 已升级为 QPointer，isNull 检查就够"——只要 scene 先于 ImageWindow 被清/替换，这里就是 UAF | `QGraphicsEllipseItem` 不是 QObject（`QPointer` 根本用不了），实际保护只有 `~ImageWindow` 里一句 `m_brushCursor=nullptr`；`brushCursor()` 公开返回裸指针，任何外部代码（面板/MainWindow）都能在窗口销毁后继续持有它 | 光标 item 改为 `ImageCanvas` 内部成员并随 scene 生命周期管理；`brushCursor()` 返回 `QPointer<QObject>` 不可用，则改成"每次都向 scene 查询"或引入 `std::shared_ptr<BrushCursorToken>` 弱引用 |
| 05-33 | P3 | 全文件 | design | 注释与实现严重脱节：数百行 "P0-x fix/已完成" 注释描述的行为在当前代码里不存在（如 05-26 的 dtor 注释、`m_brushCursor` 的 QPointer 说法、`applyParams` "保留 API 但不做事"）；读代码的人会被误导 | 多轮重构未同步注释 | 注释只写"为什么"，不写"曾经怎样"；删除 changelog 式注释 |
| 05-34 | P3 | imagewindow.ui:18 + imagewindow.cpp:311-322 | build/design | `.ui` 里唯一的中央控件 `graphicsView` 在构造时被抛弃，真实结构完全由代码生成，`.ui` 只剩 34 行假象 | 渐进式重构遗留 | 要么把 canvas 提升为 ui 里的自定义控件，要么删除 .ui 占位 |

### 05-01 保存/导出路径不可逆烧图，且导出在取消前就烧 (P0)

- **位置**：`src/media/imagewindow/ImageIOController.cpp:75`、`:101`、`:128`；`src/media/imagewindow/TextOverlayController.cpp:170-208`
- **证据**：
```cpp
// onExport(): 先烧图, 再弹对话框、再弹文件对话框
if (m_host->textOverlay()) m_host->textOverlay()->flattenText();   // :128
...
dialogs::ExportDialog dlg(m_host);
if (dlg.exec() != QDialog::Accepted) return;                       // :137 取消时文字已经没了
const QString path = QFileDialog::getSaveFileName(...);
if (path.isEmpty()) return;                                        // :144 同上
```
- **现象 / 复现**：文档里有文字层 → 菜单"导出为" → 直接按 Esc → 文字层消失（变成像素，Ctrl+Z 也回不来）。Ctrl+S 保存到只读盘/磁盘满失败时同样先烧图再失败。
- **根因**：`flattenText()` 既做像素合成又做 `delete item`，而三个保存入口把它当"写盘前准备"无条件调用；`flattenText` 不 push 任何 `QUndoCommand`（`TextOverlayController.cpp:170-208`），所以烧图不进撤销栈。文字系统与"保存"的职责被混在一起。
- **修复建议**：删除三个入口里的 `flattenText()`。写盘只需 `currentImage()`（文字本来就不在 `m_current` 里，所以当前"保存出的图不含文字"——这才是需要修的：合成到一份临时 `cv::Mat`（`QPainter` 画到 `matToQImage` 的副本上）后再 `saveImage`，scene 与撤销栈完全不动。
- **关联**：文字系统（TextOverlayController）、导出（09 模块的 `onExport` 结论 09-03 与此重叠，但 09 未覆盖"取消也烧"与"失败也烧"）。

### 05-02 flattenText→setCurrentImage 破坏文档：图层重复合成 + 原图被覆盖 (P0)

- **位置**：`src/media/imagewindow/TextOverlayController.cpp:196-198`；`src/media/imagewindow.cpp:1850-1861`
- **证据**：
```cpp
// TextOverlayController::flattenText
cv::Mat newMat = ImageProcessor::qImageToMat(img);   // img = matToQImage(m_current) 即全栈合成结果
m_host->setCurrentImage(newMat);                    // 整张合成图写回宿主

// ImageWindow::setCurrentImage
m_current = img.clone();
m_original = img.clone();                           // ★ 原图被覆盖
if (m_layerStack && m_layerStack->count() > 0) {
    auto base = m_layerStack->baseLayer();
    if (base) base->image = img.clone();            // ★ 合成图写进 base, 其它图层仍在
}
```
- **现象 / 复现**：两层（背景+半透明红层）→ 双击加文字 → Ctrl+S → 保存出的图正常；但从这一刻起画布/后续保存会越来越"红"（每次 render 都把已在 base 里的红层再叠一次）；用户再按"复位/重新调整"也不对，因为 `m_original` 已经是合成结果。
- **根因**：`m_current` 被设计成"layerStack 的 render 缓存"，但 `setCurrentImage` 把"合成结果"反写回 base；`render()` 又会把栈上其它层再叠到 base 上 → 合成不可逆。`m_original` 本是"原图基线"（Adjustment/滤镜/马赛克都拿它当输入），却在同一个函数里被覆盖。
- **修复建议**：`setCurrentImage` 只写 `m_current`，不写 `m_original`、不写 base layer；需要"把合成结果落到 base"的场景（拼合图像/裁剪画布）显式走 `LayerOp::FlattenVisible` 并先删除其它图层。`m_original` 一旦载入即只读（`const cv::Mat& originalImage() const`），写入路径全部去掉。
- **关联**：AdjustmentPanel（05-10）、马赛克（05-15）、阈值判定（05-30）、图层模块（`LayerStack::setBaseLayer`）。

### 05-03 保存写的是过期缓存（异步/惰性脏）(P0)

- **位置**：`src/media/imagewindow/ImageIOController.cpp:77`、`:103`；`src/media/imagewindow.cpp:1891-1894`、`1964-1971`
- **证据**：
```cpp
// renderToView 是唯一的"重建入口"
if (m_currentDirty) rebuildCurrentCache();     // :1894
...
// rebuildCurrentCache 异步分支: 提交后 m_currentDirty 保持 true, 直到回调
const uint64_t myTaskId = m_currentTaskId.fetch_add(1) + 1;
m_layerStack->renderAsync(m_engine.get(), [this,myTaskId](cv::Mat result){
    if (m_currentTaskId.load() != myTaskId) return;   // ★ 被丢弃时也不清 dirty
    m_current = std::move(result);
    m_currentDirty = false;
    renderToView();
});

// onSave 从不 rebuild, 直接读 m_current
if (!ImageProcessor::saveImage(m_host->currentImage(), fp, &err)) { ... }
```
- **现象 / 复现**：24MP 照片 → 隐藏某个图层 → 立刻 Ctrl+S（<100ms）→ 存出的文件里该图层仍可见。或者在 `applySmartObjectTransform`（`:2945` 只 invalidate 不 render）后保存，变换没进文件。
- **根因**：`m_current` 是"惰性缓存"，所有写盘/导出路径都不做"确保缓存最新"；异步 render 让窗口期从微秒级拉长到几十~几百毫秒。
- **修复建议**：保存前 `refreshCurrentCacheSync()`（`m_current = m_layerStack->render(); m_currentDirty=false;`）作为唯一真源；异步只服务显示，不服务写盘。
- **关联**：所有 `invalidateCurrentCache()` 调用点（53 处）。

### 05-04 异步 render 回调在 ImageWindow/LayerStack 析构后执行 (P0)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1775-1809`；`src/media/imagewindow.cpp:1964-1971`、`579-608`
- **证据**：
```cpp
// LayerStack::renderAsync —— receiver 是 QCoreApplication, 不是 ImageWindow
const LayerStack *self = this;                       // 裸指针进闭包
auto *ctx = QCoreApplication::instance();
engine->background().submit_fn([self, cb, ctx](TaskContext&){
    cv::Mat result = self->render();                 // ★ self 可能已析构
    QMetaObject::invokeMethod(ctx, [cb, result](){ cb(result); }, Qt::QueuedConnection);
}, ...);

// ImageWindow::rebuildCurrentCache 的回调
[this, myTaskId](cv::Mat result) {                   // ★ this 可能已析构
    if (m_currentTaskId.load() != myTaskId) return;  // 第一行就解引用 this
```
- **现象 / 复现**：打开一张 6000×4000 的图（>1024² 必走异步）→ 做任意触发 render 的操作 → 1 秒内关 tab 或关主窗口 → 下一个事件循环崩溃（Release 下表现为随机闪退/无提示退出）。`LayerStack.cpp:1730-1735` 的注释自己写了"调用方须在 ImageWindow 析构时 setAsyncEnabled(false) 或类似保护"，但全项目 `setAsyncEnabled` **无任何调用点**（grep 仅定义处）。
- **根因**：Qt 的 `QMetaObject::invokeMethod(ctx, functor, QueuedConnection)` 把 functor 投到 `QCoreApplication` 的事件队列，`QCoreApplication` 活到最后，所以队列里的 lambda 一定会被执行；闭包里的 `ImageWindow*`/`LayerStack*` 没有任何 `QPointer`/token 保护。`~ImageWindow` 里 `m_engine.reset()` 只保证 worker 线程退出，**不清事件队列**。
- **修复建议**：① `renderAsync` 增加 `QPointer<QObject> receiver` 参数，用 receiver 调 `invokeMethod`（Qt 会在 receiver 析构时丢弃该对象的 posted event）；② 闭包捕获 `QPointer<ImageWindow>` 并在回调第一步判空；③ `~ImageWindow` 首行 `m_currentTaskId.fetch_add(1)` 作废在飞任务 + `m_asyncEnabled=false`；④ 给 `LayerStack` 加 `abortRender()`/generation。
- **补充（`asyncEnabled` 关闭路径）**：`setAsyncEnabled(false)` 只影响 `rebuildCurrentCache`（`imagewindow.cpp:1936-1941`），`AdjustmentPanel::applyCurrentTab` 只判断 `engineContext()` 是否存在、**完全忽略该开关**（`AdjustmentPanel.cpp:696-708`），所以即使按注释"关异步"也不能消除本 UAF 面；而全项目没有任何地方调用它，等于这条"安全阀"从未生效。
- **关联**：LayerStack（图层模块）、EngineContext（ThreadPool 模块）。此条与 05-12 同根（共用 counter）。

### 05-05 选项面板跨文档裸指针 → 关 tab 后操作即崩 (P0)

- **位置**：`src/media/tools/ImageOptionBar.cpp:157-185`；`src/media/imagewindow/MosaicOptionPanel.cpp:22-37`；`src/media/imagewindow/TextOptionPanel.cpp:23-34`
- **证据**：
```cpp
// ImageOptionBar::ensureMosaicPage —— 只建一次, 之后永远复用
if (m_mosaicPageIdx >= 0) return;                    // :160-163
auto* page = new MosaicOptionPanel(m_mosaic, this);  // m_mosaic 是"当前"文档的工具

// MosaicOptionPanel
MosaicOptionPanel::MosaicOptionPanel(MosaicTool* tool, QWidget* parent)
    : QWidget(parent), m_tool(tool)                  // ★ 裸指针, 无 QPointer
...
void MosaicOptionPanel::onSizeSliderChanged(int v) { if (m_tool) m_tool->setSize(v); }
```
- **现象 / 复现**：图 A 用画笔工具（Mosaic 选项页被创建）→ 关 A 的 tab（`ImageWindow` 被 `deleteLater`，`MosaicTool` 随之析构）→ 打开图 B → 再切画笔 → 该页仍是 A 的页面 → 拖"笔刷大小"，`m_tool` 指向已释放内存 → UAF（`setSize` 里 `m_host.isNull()` 恰好为假/真随机）。文字页同理（`TextOptionPanel::onFontComboChanged → m_ctrl->setTextFont`）。
- **根因**：左侧 dock 在 P7.6.x 被搬到 MainWindow 全局唯一，但选项页的"每文档绑定"没有跟着做：`attach()` 只更新 bar 自己的 `m_mosaic/m_textCtrl`，不更新已创建页面的成员；页面持有跨文档生命周期对象用裸指针。
- **修复建议**：`MosaicOptionPanel::rebind(MosaicTool*)` + 成员 `QPointer<MosaicTool>`；`ImageOptionBar::attach/detach` 里对所有已建页面调用 `rebind`/`rebind(nullptr)`；或干脆按文档销毁并重建页面（`removeWidget+deleteLater`）。
- **关联**：tools/ImageOptionBar（P7.6.x 迁移未完成，与 09 模块 09-01 同类问题）。

### 05-06 flattenText 未清 m_handleDragItem → UAF (P0)

- **位置**：`src/media/imagewindow/TextOverlayController.cpp:199-207`；`src/media/imagewindow.cpp:2668-2670`
- **证据**：
```cpp
// flattenText 删除所有 item
for (auto* item : m_items) { m_host->sceneRef().removeItem(item); delete item; }
m_items.clear();
setCurrent(nullptr);                     // ★ 只清了 current, 没清 m_handleDragItem

// eventFilter(MouseMove) 每帧:
if (m_textCtrl && m_textCtrl->handleDragItem()) {
    m_textCtrl->handleDragItem()->continueHandleDragFromView(sp);   // UAF
```
- **现象 / 复现**：拖着文字右下角控制点不松手，按 Ctrl+S（或任何触发 `flattenText` 的路径）→ 松开/移动鼠标 → 崩溃。
- **根因**：`m_handleDragItem`（TextOverlayController.h:115）是裸 `GraphicsTextItem*`，`flattenText` 只重置 `m_current`；`removeAllFromSceneAndDelete` 记得清（:78），`flattenText` 漏了。
- **修复建议**：`flattenText` 开头 `setHandleDragItem(nullptr)`；并把 `m_handleDragItem` 改成 `QPointer<GraphicsTextItem>`；`handleDragItem()` 返回 `QPointer` 或每次使用前判空。
- **关联**：文字系统、保存链路。

### 05-07 16bit/浮点图载入不做位深缩放 (P0)

- **位置**：`src/media/imagewindow.cpp:856-861`
- **证据**：
```cpp
if (img.depth() != CV_8U) {
    cv::Mat tmp;
    img.convertTo(tmp, CV_8U);   // alpha=1, beta=0 → saturate_cast
    img = tmp;
}
```
- **现象**：打开 16bit PNG（常见于截图/摄影导出）后整图接近纯白；32F HDR 更糟。
- **根因**：`convertTo` 的默认 `alpha=1` 只做饱和截断，不是量程映射；16U 的 1000 被 clamp 到 255。
- **修复建议**：`double s = 255.0 / ((1<<(img.elemSize1()*8))-1); img.convertTo(tmp, CV_8U, s);`；更正确：保留 16bit 到 `LayerStack`（`CV_16UC3`），显示时降位。
- **关联**：`ImageProcessor` 全链路按 8U 设计（`saveImage` 直接拒绝非 8U，`imageprocessor.cpp:39-42`）。

### 05-08 4 通道图像显示/保存花屏 (P1，legacy 路径)

- **位置**：`src/media/imageprocessor.cpp:112-136`；调用点 `src/media/imagewindow.cpp:1905`、`src/media/imagewindow/TextOverlayController.cpp:179`
- **证据**：
```cpp
case 4:
    cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGBA);
    break;
...
return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
              QImage::Format_RGB888).copy();   // ★ RGBA 数据 + RGB888 格式 + step=cols*4
```
- **现象**：compositor 关闭（`setCompositorEnabled(false)`）或走 `LayerStack::render()` legacy 分支时，4 通道 base 直接成为 canvas → 花屏（每像素错位一字节，颜色整体偏）；此时若加文字并保存，`flattenText` 走同一函数 → 花屏写进文件（数据损坏）。注：compositor 默认开启时 4 通道其实是"全黑"（见 05-35），两条路都是错的。
- **根因**：格式枚举与数据布局不匹配；`QImage` 会接受 `bytesPerLine>cols*3`，所以不会 assert，只是把 A 当成下一像素的 R 读。
- **修复建议**：4 通道返回 `QImage::Format_RGBA8888`；或统一在显示层 `cvtColor(BGRA2RGB)` 丢掉 alpha 并用棋盘格表示透明（见 §3）。
- **关联**：ImageProcessor（照片模块）、任何 `matToQImage` 使用方、05-35。

### 05-35 灰度/带 alpha 的图打开后画布全黑 (P0)

- **位置**：`src/media/compositor/nodes/LayerBitmapNode.cpp:39-43`；`src/media/compositor/RenderGraph.cpp:56-72`；`src/media/imageworker/layers/LayerStack.h:327`（compositor 默认 `true`）
- **证据**：
```cpp
// LayerBitmapNode::compute —— 瓦片类型跟随图层图, 可能是 CV_8UC1 / CV_8UC4
cv::Mat tile(r.height, r.width, m_layer->image.type());     // :43
srcROI.copyTo(dstROI);                                      // 不转换通道
...
// RenderGraph::execute —— canvas 固定 3 通道
cv::Mat canvas(m_grid->imageHeight(), m_grid->imageWidth(), CV_8UC3, cv::Scalar(0,0,0));  // :56
...
if (tile.size() == dst.size() && tile.type() == dst.type()) { tile.copyTo(dst); }        // :68
// Mismatched size / type = silently skip (caller should ensure ...)                    // :71
```
- **现象 / 复现**：File > 打开一张 8bit 灰度 PNG（或任意单通道 TIFF）→ 画布**纯黑**；带 alpha 的 PNG 同样纯黑。状态栏/属性面板尺寸正确，暗示"图加载成功但显示不出来"。因为 `RenderGraph` 对类型不匹配是静默跳过，没有任何日志。
- **根因**：`LayerStack` 的 base layer 保留载入时的通道数（`loadFile` 只在 16U→8U 转换，不改通道数：`imagewindow.cpp:851-861`），而 P8 compositor 的 canvas 硬编码 `CV_8UC3`；`makeRoot()`（LayerStackAdapter.cpp:142-153）直接把 base 包成 `LayerBitmapNode`，没有归一化节点。legacy `render()` 分支反而是对的（`canvas = base->image.clone()`，随后 `applyBlend` 内部有 1ch/4ch→BGR 转换，LayerBlendNode.cpp:32-39）。另注意 `LayerStack.cpp:1750` 注释写"Default is OFF"，与 `LayerStack.h:327` 的 `= true` 矛盾，生产路径（全项目无人调 `setCompositorEnabled`）恒走 compositor。
- **修复建议**：① 在 adapter 的根节点前插一个 `ChannelNormalizeNode`（1ch→BGR，4ch→BGRA2BGR，保留 alpha 作单独通道）；② `RenderGraph` 类型不匹配时 `LOG_WARN` 并做一次 `cvtColor` 兜底，禁止静默；③ `loadFile` 统一把 base 归一化为 BGRA/BGR（并记录 hasAlpha 用于棋盘格显示）。
- **关联**：compositor 模块（P8.1/P8.2）、图层模块；与 05-08 是同一"通道数没定义清楚"问题的两条分支。另一个衍生 bug：`LayerBitmapNode.cpp:43` 的 `cv::Mat tile(...)` **不初始化**，但第 53-56 行注释写"Zero-init already happened via cv::Mat ctor"（错），边缘瓦片会读到未初始化内存 → 图像边缘出现随机噪点。

### 05-09 撤销栈全图快照导致 GB 级内存 (P0)

- **位置**：`src/media/imagewindow.cpp:235-245`、`:564`；`src/media/imagewindow/MosaicTool.cpp:173`
- **证据**：
```cpp
ImageEditCommand::ImageEditCommand(ImageWindow *w, const cv::Mat &imageBefore,
                                   const cv::Mat &imageAfter, const QString &text, ...)
    : ..., m_imgBefore(imageBefore.clone()), m_imgAfter(imageAfter.clone()), m_useImage(true) {}

m_undoStack->setUndoLimit(200);   // 按"条数"限制
```
- **现象**：24MP 图连续涂 20 笔 ≈ 2.9GB 常驻内存，任务管理器直线上升；32MP 手机照片 10 笔就能让 32 位进程 OOM。马赛克、画笔、克隆图章、修复工具、红眼、裁剪全走这个命令。
- **根因**：一条命令两份全图；`QUndoStack::setUndoLimit` 不感知字节；且 push 时 `ImageEditCommand::redo` 是 no-op（`imagewindow.cpp:257-277`），这两份 `Mat` 只用于 undo。
- **修复建议**：① 编辑类命令存 ROI + 矩形（`cv::Mat patch = before(rect).clone()`），undo 只 patch 回去；② `UndoStack` 子类在 `push` 前按 `Mat.total()*elemSize()` 估算，超预算先裁旧命令；③ 马赛克类命令合并同一次 stroke（现在是每 stroke 一条，正确）。
- **关联**：`ImageEditCommand`（本文件）+ Brush/CloneTool/PatchTool/RedEye/Crop（tools 模块）+ MainWindow 的裁剪/画布大小动作。

### 05-10 AdjustmentPanel：每帧都在已调整结果上再调整 (P1)

- **位置**：`src/media/imagewindow/AdjustmentPanel.cpp:687-694`、`:702`、`:714`、`:742`、`:758`；`src/media/imagewindow.cpp:1850-1853`
- **证据**：
```cpp
if (!m_host) return;
if (m_host->originalImage().empty()) return;
if (m_pendingBefore.empty() && m_host->undoStack())
    m_pendingBefore = m_host->currentImage().clone();
...
cv::Mat snapshot = m_host->originalImage().clone();   // :714 输入永远是 m_original
...
host->setCurrentImage(out);                           // :742 → 覆盖 m_original
```
- **现象 / 复现**：色阶 tab 拖 gamma 滑块，图像越来越暗（每 tick 叠一次 gamma），根本没法"实时预览"。若先画笔涂抹再调调整，涂抹立刻消失（因为 LUT 作用在没涂抹的 `m_original` 上，然后整图替换）。
- **根因**：`m_original` 名义上是"原图基线"，实际被 `setCurrentImage` 每次覆盖；调整面板把它当不可变输入 → 累积。同时 `m_current` 里的非 base 图层内容也被一起丢掉。
- **修复建议**：新增 `m_baseSnapshot`（载入时的只读原图，或当前图层合成快照）专供"参数化调整"用；调整面板统一在 `m_baseSnapshot` 上算，结果只更新显示/预览层；`setCurrentImage` 不再写 `m_original`（见 05-02）。
- **补充（tab 参数持久）**：5 个 tab 的参数缓存本身是好的——`QTabWidget` 子页不销毁、`m_curves/m_levels/m_hsl/m_bw/m_cm` 跨 tab 保留（`AdjustmentPanel.cpp:281-294` 的 `switchToTab` 只切索引）；坏的是"应用基数"（每个 tab 的 LUT 都乘在已被上一次结果覆盖的 `m_original` 上），所以切 tab 回到旧 tab 再拖一次，会得到"旧 tab 参数 × 新 tab 参数"的混合结果。
- **关联**：`ImageProcessor::applyLut`、`setCurrentImage`、滤镜（`FilterCommand` 的 undo/redo 同样走 `replaceCurrentImage` → 写 base + 覆盖 `m_original`，与 05-02 同病）、`ImageEditCommand`（马赛克/画笔类不做 LUT，但同样只改缓存）。

### 05-11 调整的撤销/重做基本失效 (P1)

- **位置**：`src/media/imagewindow/AdjustmentPanel.cpp:772-800`；`src/media/imageworker/layers/LayerCommand.cpp:757-771`
- **证据**：
```cpp
void AdjustmentPanel::commitUndoDebounced() {
    if (m_pendingBefore.empty()) return;
    cv::Mat after = m_host->currentImage().clone();       // 异步还没回来时 after==旧图
    if (m_pendingBefore.size == after.size && type 相同) {
        if (std::memcmp(m_pendingBefore.data, after.data, bytes) == 0) return;  // 静默丢弃
    }
    auto* cmd = layers::LayerCommand::makeSetAdjustmentLut(stack, index, m_pendingBefore, after, m_host);
    m_host->undoStack()->push(cmd);
}
// redo: "redo is a no-op for both host and legacy paths"  (LayerCommand.cpp:757-771)
```
- **现象**：大图拖滑块后 Ctrl+Z 无反应（这次改动没有撤销项）；即使进了栈，Ctrl+Y 也永远回不来。
- **根因**：debounce 200ms 赶不上后台 LUT；"after"取的是 UI 线程此刻的 `m_current`（尚未被异步回调更新）→ 与 before 相同 → 放弃 push。redo 被前几轮"简化"成 no-op（注释承认 trade-off）。
- **修复建议**：commit 不要读 `m_current`，改为"把最终 LUT 作用在 base 快照上"同步算一次（或保存异步任务的 result 句柄）；redo 实现为 `replaceCurrentImage(m_newMat)`（`m_newMat` 已经有了）。
- **关联**：`LayerCommand`（图层模块）、`FilterCommand`（同样 redo 有实现但 undo 靠 replace）、`TransformCommand`（redo 正常）。

### 05-12 共用任务 ID + dirty 不归位 → 显示错乱/空转 (P1)

- **位置**：`src/media/imagewindow.cpp:1964-1971`、`:1894`；`src/media/imagewindow/AdjustmentPanel.cpp:711`、`:739`
- **证据**：
```cpp
// ImageWindow::rebuildCurrentCache
const uint64_t myTaskId = m_currentTaskId.fetch_add(1) + 1;
m_layerStack->renderAsync(m_engine.get(), [this,myTaskId](cv::Mat result){
    if (m_currentTaskId.load() != myTaskId) return;   // 被谁取消了? 可能是 AdjustmentPanel
    m_current = std::move(result); m_currentDirty = false; renderToView();
});
// AdjustmentPanel::applyCurrentTab
const uint64_t myTaskId = m_host->currentTaskId().fetch_add(1) + 1;   // 同一个计数器
```
- **现象**：大图上先拖调整滑块再马上缩放/切图层，调整结果被丢弃（"按了没反应"）或 render 结果被丢弃（画面停在调整后的中间态）。被丢弃的 render 让 `m_currentDirty` 永远 true → 之后每次 `renderToView` 都重新投一次全图 render，CPU 打满但画面不变。
- **根因**：一个 token 承担两个语义（"最新 render"与"最新 adjustment"）；回调早退时没有把 dirty 状态回滚。
- **修复建议**：`m_renderTaskId` / `m_adjustTaskId` 分开；render 回调被丢弃时也应再触发一次（或把 `m_currentDirty` 保持 true 且用 `QTimer::singleShot(0)` 串行重投，而不是每个 paint 重投）。
- **关联**：05-04（同一套异步机制）。

### 05-13 / 05-14 刷新入口散落、undo 后不重绘 (P1)

- **位置**：`src/media/imagewindow.cpp:979`（smart object refresh）、`:987`（toggle embed）、`:1006`、`:1026`、`:1047`、`:1391`（rename）、`:1428`（lock）、`:1446`（link）、`:1530`、`:1574`、`:2945`；`src/media/imagewindow.cpp:2097-2100`、`2113-2122`；`src/media/imageworker/layers/LayerCommand.cpp:439-443`
- **证据**：
```cpp
connect(m_layerPanel, &layers::LayerPanel::refreshSmartObjectRequested, this, [this](int idx) {
    if (!m_layerStack) return;
    m_layerStack->refreshSmartObject(idx);
    invalidateCurrentCache();                 // :979 到此为止, 没有 renderToView()
    statusBar()->showMessage(tr("智能对象已刷新"), 3000);
});
...
void ImageWindow::onStackChanged() { emit dirtyChanged(isDirty()); }   // :2097 不 invalidate
```
- **现象**：点"刷新智能对象"→ 状态栏报成功但画面不变；改图层名/锁定/链接/应用智能对象变换同样；Ctrl+Z 撤销"隐藏图层"后图层树更新了、画布不更新（`LayerCommand::undo` 直接 `l->visible = m_boolVal;` 不发信号）。
- **根因**：刷新协议是"约定俗成"的（谁记得谁调），不是不变量；`renderToView`/`invalidateCurrentCache` 在 .cpp 里有 53 个调用点，任何一处漏掉都静默失败。undo 路径完全没有"重绘"协议。
- **修复建议**：把 `invalidateCurrentCache()` 改成"脏标记 + `QTimer::singleShot(0)` 调度一次 `rebuild+render`（可合并）"，所有 53 处只保留 invalidate；`onStackChanged` 里统一 invalidate；`LayerCommand` 改调 setter 以复用 `layerChanged`。
- **关联**：图层模块、智能对象。

### 05-15 / 05-16 马赛克 in-place 编辑：不同步图层、切换即丢撤销 (P1)

- **位置**：`src/media/imagewindow/MosaicTool.cpp:111-121`、`:136-148`、`:160-177`、`:194-225`；`src/media/imagewindow/MosaicTool.cpp:42-73`
- **证据**：
```cpp
void MosaicTool::onSceneClicked(const QPointF& scenePos) {
    cv::Mat& current = m_host->currentImage();
    m_backup = current.clone();      // 只有一个"全图 before"
    m_strokeOpen = true;
    applyMosaicAtInitial(scenePos);  // 直接 in-place 改 m_current 和 m_original
}
...
void MosaicTool::setEnabled(bool on) {
    ...
    m_strokeOpen = false;
    if (!on) m_backup.release();     // ★ 丢掉 undo 基线, 已改像素无法撤销
}
```
- **现象**：① 涂抹后切换图层可见性/改名（触发 invalidate+render）→ 笔迹消失；② 涂到一半点"退出涂抹模式"/按下 Esc 切工具 → 笔迹留在图上但 Ctrl+Z 撤不掉；③ 大图上异步 render 回填时笔迹被覆盖。
- **根因**：涂抹只改 `m_current` 这个"缓存"，没有写回 `m_layerStack` 的 base layer；`m_original` 也被同一次 `applyMosaic(original)` 涂掉（注释明确写"同时改 m_original，否则后续 slider 变化会重置 m_current"，说明作者知道这是补丁）；`setEnabled(false)` 把 stroke 状态和 undo 基线一起清掉。
- **修复建议**：① stroke 结束把 patch 写入 `m_layerStack->baseLayer()->image` 后 `invalidateCurrentCache()`；② `m_original` 只读（05-02）；③ `setEnabled` 里先 `if (m_strokeOpen) onSceneDragEnd()` 再改标志；④ 大图时涂抹走"patch/瓦片"而不是全图 clone（05-09）。
- **关联**：画笔/克隆/修复工具是同一套 `ImageEditCommand`，同样问题（tools 模块需同步修）。

### 05-17 刷新一次 ≥8 份全图拷贝 (P1)

- **位置**：`src/media/imagewindow.cpp:1905-1910`；`src/media/imageprocessor.cpp:112-136`；`src/media/imagewindow/MosaicTool.cpp:221-224`
- **证据**：
```cpp
const QImage qimg = ImageProcessor::matToQImage(*display);   // cvtColor 全图 + QImage.copy() 全图
m_canvas->pixmapItem()->setPixmap(QPixmap::fromImage(qimg)); // 全图再分配一次(native)
m_canvas->scene()->setSceneRect(m_canvas->pixmapItem()->pixmap().rect());
m_canvas->resetTransform();
m_canvas->scale(m_canvas->zoom(), m_canvas->zoom());
```
- **量化**（24MP/72MB 一次全图）：渲染一帧 = `render()` 内部 base clone(1) + 混合临时(1~2) → `matToQImage` 的 `cvtColor`(1) + `.copy()`(1) → `QPixmap::fromImage`(1) → `setPixmap` 隐式共享 + `sceneRect` → **≥5~7 份全图内存流量**；调整滑块一 tick 再加 `originalImage().clone()`(1)、`applyLut` 输出(1)、`setCurrentImage` 的两份 clone(2) = 8~10 份。60Hz 拖动的量级是每帧 ~500MB 内存流量。
- **根因**：`QImage`/`QPixmap` 不做缓存，每次重建；`ImageCanvas::invalidateRect`（脏矩形 API，P7.11 已写）**没有任何生产调用点**（只有测试）；`matToQImage` 只为显示服务，却做了 cvtColor+copy。
- **修复建议**：① `ImageCanvas` 持有 `QPixmap` 复用，`renderToView` 只在尺寸/内容版本变化时重建；② 缓存一份 RGB888 `QImage` 供显示，`saveImage` 另走 BGR；③ 马赛克/画笔按笔刷包围盒调 `invalidateRect`；④ 大图切瓦片。
- **关联**：ImageProcessor、ImageCanvas（08/画布渲染模块可能重复结论）。

### 05-18 eventFilter 内同步写调试日志 (P1)

- **位置**：`src/media/imagewindow.cpp:2616-2643`
- **证据**：
```cpp
static QFile efLog("D:/vistella_events.log");           // 硬编码绝对路径
if (!efOpened) { efLog.open(QIODevice::Append | QIODevice::Text); efOpened = true; }
...
efLog.write(QString("[EVF] ...").toUtf8());
efLog.flush();                                          // 每个 MouseMove 同步 flush
```
- **现象**：鼠标在画布上移动就产生磁盘写；没有 D 盘的机器每事件都尝试一次失败 open；日志无上限（长时间使用几十 GB）。UI 发涩。
- **根因**：调试代码未清理，`static` 局部对象使它在整个进程生命周期存在（跨窗口共享，追加写）。
- **修复建议**：删除；若需保留，用 `spdlog` 的 debug 级 + 编译期 `#ifdef MULTIDOC_EVENT_TRACE`，输出到 `QStandardPaths::TempLocation`。

### 05-19 缩放：无边界、锚点失效、fit 状态不记 (P1)

- **位置**：`src/media/imagewindow/ImageCanvas.cpp:62-89`、`:111-125`；`src/media/imagewindow.cpp:1908-1910`
- **证据**：
```cpp
void ImageCanvas::applyTransform() { resetTransform(); scale(m_zoom, m_zoom); }   // :62-68
void ImageCanvas::setZoom(double z) {
    if (qFuzzyCompare(z, m_zoom)) return;   // 无范围检查
    m_zoom = z; applyTransform(); emit zoomChanged(m_zoom);
}
void ImageCanvas::onZoomIn()  { setZoom(m_zoom * 1.25); }
void ImageCanvas::onZoomOut() { setZoom(m_zoom / 1.25); }   // ★ 无下限
void ImageCanvas::onFitWindow() { fitInView(m_item, Qt::KeepAspectRatio); m_zoom = transform().m11(); }
// wheelEvent 有 clamp 0.05~20, 按钮路径没有 → 两条路径不一致
```
- **现象 / 机制**：
  - Ctrl+`-` 按 ~35 次后 `m_zoom≈0.0004`，`scale()` 生成接近奇异的矩阵，`mapToScene` 精度崩坏；再放大回来要按同样多次（无"100%/适合屏幕"一键，只有 Ctrl+0）。
  - 滚轮缩放的锚点：`setTransformationAnchor(AnchorUnderMouse)`（ImageCanvas.cpp:38）只在变换时生效，而这里每步都先 `resetTransform()` 再 `scale()`；**重置那一步把视口对齐/滚动位置一起重算**，缩放的视觉锚点退化为视口中心而非鼠标位置（[待确认]：具体退化程度取决于 Qt 6.11 内部 `resetTransform` 是否重算 scrollbar；如需精确验证建议写最小 QTest 对比 `setTransform` 与 `resetTransform+scale` 的 `mapToScene(mousePos)`）。
  - "适合屏幕"后改变窗口大小：`ImageCanvas` 没有 `resizeEvent` 处理，`m_zoom` 与 transform 都不变 → 窗口变大/变小后图像既不适配也不重算；也没有 PS 那种"适合屏幕"模式状态。
  - 缩放百分比从不显示：`zoomChanged` 信号无任何 connect（grep 仅 emit 处）；PS 状态栏/导航器有实时百分比输入框。
- **修复建议**：抽取 `setZoom(z, anchor=NoAnchor)`，内部 `z = qBound(0.05, z, 32.0)`；用 `setTransform`/`scale()` 累乘（如需 anchor 就先 `mapToScene(mouse)`→变换→`centerOn` 校正）；`resizeEvent` 里若处于 fit 模式则重算；把 `zoomChanged` 接到状态栏百分比控件。
- **关联**：MainWindow 的 4 个缩放 action（`:420-421`、`:481-487`、`:2243-2256`）。

### 05-20 保存内容/元数据丢失 + 无格式校验 (P1)

- **位置**：`src/media/imagewindow/ImageIOController.cpp:88-111`；`src/media/imageprocessor.cpp:32-110`
- **证据**：
```cpp
const QString path = QFileDialog::getSaveFileName(m_host, tr("另存为"), suggest,
    tr("图像 (*.png *.jpg *.bmp);; 全部文件 (*)"));     // 只有 3 种过滤器
...
if (!ImageProcessor::saveImage(m_host->currentImage(), path, &err)) { ... }
// saveImage 内部: qimg.save(path) —— 格式完全由扩展名推断, 无 profile/exif/dpi 参数
```
- **现象**：① 保存后重新打开文件：图层/文字/调整参数全部消失（只剩拍平位图），且保存时没有任何提示；② PNG 的 alpha 在"烧纸/拼合"后被丢（`m_current` 变 3 通道）；③ ICC/EXIF/GPS/DPI 全丢；④ 原文件是 `.webp/.tif/.gif` 时另存对话框默认过滤器给 `.png`，用户手输 `.psd` 则直接失败；⑤ 相对 `onSave`（覆盖）没有任何"文件已被外部修改"检查。
- **根因**：项目没有"文档容器格式"的概念；`SaveOptions` 只有 quality/compression；`ImageProcessor::saveImage` 用 `QImage::save`（扩展名推断）。
- **修复建议**：① 增加 `.vistella`（或其扩展）分层工程文件（JSON 头 + 各层 PNG，或直接写 PSD，见 §4）；② `SaveOptions` 增 `iccData/exif/dpi/format/keepAlpha`；③ 保存前用 `QImageWriter::supportedImageFormats()` 动态过滤，格式与扩展名不一致时提示或强制后缀；④ 保存前 record mtime，覆盖时检测外部修改。
- **补充（覆盖确认 / 中文路径 / 失败路径）**：① 另存覆盖由 `QFileDialog` 原生确认，Ctrl+S 直接覆盖无备份、不检测文件是否被外部改过；② 载入用 `cv::imread(path.toLocal8Bit())`（`imagewindow.cpp:851`），路径里出现本地代码页无法表示的字符（日文、Emoji、部分符号）时打开失败，而保存走 `QImageWriter`（UTF-16 路径）是正常的 → 同一路径"存得进、打不开"；③ 只读目录/磁盘满只在 `saveImage` 返回 false 后弹 `QMessageBox`，但此时文字已被 05-01 烧掉。
- **关联**：icc 模块（09 的 09-11）、ExportDialog（09-10）、05-01/05-02。

### 05-21 文字 undo 指针分叉 + Remove undo 不重建信号 (P1)

- **位置**：`src/media/imagewindow.cpp:135-216`；`src/media/imagewindow/TextOverlayController.cpp:147-156`
- **证据**：
```cpp
// TextItemCommand::undo (Add): 删除 item, 只清自己的 m_item
win->unregisterTextItem(m_item.data());
win->m_canvas->scene()->removeItem(m_item.data());
delete m_item.data();  m_item = nullptr;
// TextItemCommand::redo (Add, 非首次): 建新 item, 只更新自己的 m_item
item = new GraphicsTextItem(); ... win->registerTextItem(item); win->connectTextItemSignals(item);
m_item = item;
// 而栈里后续的 Change/Move/Rotate 命令的 m_item 仍指向被删的那个 QPointer → null → redo 静默 return

// Remove::undo —— 重建 item, 但没有 connectTextItemSignals / 没有恢复字体颜色
auto *item = new GraphicsTextItem();
item->setPosition(m_oldPos); item->setRotationDeg(m_oldRot); item->restoreFromText(m_oldText);
win->m_canvas->scene()->addItem(item); win->registerTextItem(item); m_item = item;
```
- **现象**：打字 → Ctrl+Z → Ctrl+Y，文字变空；删掉一段文字 → Ctrl+Z 恢复 → 再编辑/拖动这段文字，撤销栈不再记录（编辑"丢失撤销"）、字体颜色变默认。
- **根因**：命令用 QPointer 直指 scene item（对象身份不稳定），item 一重建命令之间就断链；Remove undo 只恢复文本，不恢复 style、不重连信号。
- **修复建议**：文字模型独立于 scene item（`TextDocumentModel{id,text,font,color,pos,rot,scale}`，scene item 只是视图），命令操作模型 + 重建视图；或至少在 Add/Remove 的 undo/redo 里广播"item 重建"让栈内相关命令更新 `m_item`。
- **关联**：`GraphicsTextItem`（graphicstextitem.cpp）、PropertiesDock（`currentChanged` 联动）。

### 05-22 烧图用错 API：旋转/缩放文字保存后错位 (P1)

- **位置**：`src/media/imagewindow/TextOverlayController.cpp:185-194`；`src/media/graphicstextitem.cpp:58-76`
- **证据**：
```cpp
// 绘制侧: 用 setTransform 实现 T(c)·R·S·T(-c)
void GraphicsTextItem::applyTransform() { ... setTransform(t); }

// 烧图侧: 却用 QGraphicsItem 的 rotation()/scale()
const QPointF sp = item->scenePos();
p.translate(sp);
p.rotate(item->rotation());          // setTransform 不改 rotation 属性 → 恒 0
p.scale(item->scale(), item->scale());// setTransform 不改 scale 属性 → 恒 1
item->document()->drawContents(&p);
```
- **现象**：把文字旋转 30° / 拉大 2 倍 → 画布上正常 → 保存后文字变回 0°/原始大小（位置也可能偏，因为旋转中心不同）。
- **根因**：`GraphicsTextItem` 完全绕开 `setRotation/setScale`，而烧图代码假设它们在用那两个 API。`QGraphicsItem::rotation()` 返回的是 `setRotation()` 设置的属性值，与 `setTransform()` 无关。
- **修复建议**：烧图时 `p.setTransform(item->transform(), true)` 或直接 `item->paint(&p, option, nullptr)`；更稳妥用 `scene->render(&p, targetRect, itemRect)` 让 Qt 处理。
- **关联**：保存链路（05-01）、文字样式。

### 05-23 loadFile 就地重置：不确认 dirty、不清 scene item、面板重建 (P1)

- **位置**：`src/media/imagewindow.cpp:848-1810`（关键 `:862-872`、`:876`、`:950`、`:1808`）
- **证据**：
```cpp
m_original = img.clone();
m_filePath = path;
m_canvas->setZoom(1.0);
m_undoStack->clear();                 // :871 直接清, 无任何确认
m_savedIndex = 0;
m_layerStack = std::make_unique<layers::LayerStack>(this);   // :876 旧 stack 销毁
...
m_layerPanel = new layers::LayerPanel(m_layerStack.get());   // :950 旧 panel 走 deleteLater
...
renderToView();                       // :1808 只有这里刷新
```
- **现象**：同一窗口二次 `loadFile`（`ImageWorkspace::loadFile`，`src/media/imageworker/ImageWorkspace.cpp:92` 调用）→ 旧文档未保存内容直接消失（无提示）、旧文字 item 还叠在新图上、旧 `LayerPanel` 在 deleteLater 生效前仍可能读已销毁的旧 `LayerStack`（[待确认] 可达性：主窗口目前每次新建 `ImageWindow`，此路径主要被 ImageWorker/测试使用）。
- **根因**：`loadFile` 是"构造函数后半段"式的实现（假定对象刚建），被当成可重复调用的 loader。
- **修复建议**：拆 `loadFile()` = `closeDocument() + openDocument()`；`closeDocument` 负责 dirty 确认（`QMessageBox` / 主窗口统一 `maybeSave()`）、`m_textCtrl->removeAllFromSceneAndDelete()`、`m_layerPanel->deleteLater()` 同步化、断开旧 stack 全部 connect。

### 05-24 CameraRaw 全链路死代码 + 保存到临时文件 (P1)

- **位置**：`src/media/imagewindow/ImageIOController.cpp:49-64`（`onOpen`）、`:227-268`（RAW 拦截）；`src/app/mainwindow.cpp:3167`（真实打开路径）；`camera_raw/CameraRawLoader.cpp:59-65`、`:136`、`:164-186`
- **证据**：
```cpp
// ImageIOController::onOpen —— 全项目无调用者 (grep 仅定义)
const QString path = QFileDialog::getOpenFileName(...);
if (!m_host->loadFile(path, &err)) { ... }            // 绕过自己的 RAW 拦截

// MainWindow 实际打开:
ImageWindow* img = new ImageWindow; ...
if (img->loadFile(path, &err)) { addImageTab(img); }  // 直接走 ImageWindow::loadFile → cv::imread

// RAW 分支里的临时文件 + 路径污染
QString tempPath = QDir::tempPath() + "/multidoc_raw_" + ... + ".png";
cv::imwrite(tempPath.toLocal8Bit().toStdString(), res.image);
return m_host->loadFile(tempPath, err);               // ★ m_filePath 变成 temp
```
- **现象**：① 打开 `.RAF/.CR2` 只会报 `cv::imread failed`（libraw 支持与否都一样）；② 假设修通拦截：Ctrl+S 会把 PNG 写到 `%TEMP%`，原 RAW 永远不会被覆盖，关掉程序后临时文件残留；③ 色温滑块（`wbTemp`）完全无效——`wbTempToKelvin()` 定义后从未调用（CameraRawLoader.cpp:59-65，grep 无第二处），`wbTemp != 50` 分支里也只是 `use_camera_wb=1`；④ `bitsPerChannel=16` 时 `p.output_bps=16`，但下面固定按 `CV_8UC3` + `step=w*3` 解释 `img->data` → 花屏/半幅；⑤ `colors==4` 被接受（RGBG）但按 3 通道读 → 错位。
- **修复建议**：① 把 RAW 判断移进 `ImageWindow::loadFile`（或让 MainWindow 统一走 `m_io->loadFile`）；② decode 后保留 `m_sourceRawPath`，保存对话框默认该路径 + 提示"Camera Raw 结果需另存"；③ `wbTemp` 走 `rawProcessor.imgdata.params.user_mul` + `use_camera_wb=0`（或 `libraw_set_output_params`）；④ 16bit 输出建 `CV_16UC3`；`colors==4` 转 `BGRA/BGR`；⑤ 临时文件用 `QTemporaryFile` 并显式删除。
- **关联**：CameraRaw（本范围）+ MainWindow 文件打开（`onHomeOpenPathRequested`）+ 最近文件。

### 05-25 任意工具下双击建字 / 右键删字 (P1)

- **位置**：`src/media/imagewindow.cpp:2745-2768`（右键）、`:2774-2805`（双击）
- **证据**：
```cpp
if (event->type() == QEvent::MouseButtonDblClick) {
    if (me->button() == Qt::LeftButton
        && !(m_mosaicTool && m_mosaicTool->isEnabled())) {   // 只排除马赛克
        ...
        } else { createTextItem(sp); }                       // 其它任何工具都会创建文字
```
- **现象**：用裁剪工具双击画面 → 多出文字框；右键点画面上的文字 → 直接删除（无确认）。PS：只有文字工具才能双击建字，右键永远出上下文菜单。
- **根因**：文字工具被"废除"（注释 "文字模式已经废除"）后，事件路由没有改按 `ToolContext` 当前工具分派。
- **修复建议**：`if (m_ctx && m_ctx->currentToolId() != ToolId::Text) return ...`；右键改为 `QMenu`（删除/复制/图层化/字体…）。

### 05-26 析构注释与真实机制不符（会误导修复）(P1)

- **位置**：`src/media/imagewindow.cpp:579-608`；`src/media/imagewindow.h:619-641`
- **证据**：
```cpp
// imagewindow.cpp:581-587
// P2.5 bug fix: reset m_brushCursor FIRST, before delete ui.
//   delete ui destroys m_canvas (a child widget), whose scene owns the ellipse ...
m_brushCursor = nullptr;
...
delete ui;
// imagewindow.h:625-628
//   Qt 6.11's QPointer<T> doesn't work for multi-level inheritance like
//   QObject->QGraphicsItem->QGraphicsEllipseItem (static_cast<T*>(QObject*) fails ...)
```
- **事实**：① `delete ui` 只释放 `Ui::ImageWindow` 这个 POD 结构体，**不销毁任何控件**（`ui_*.h` 的析构函数是默认的）；`m_canvas` 的 parent 是 `ui->centralWidget`，真正删除发生在 `~QWidget`→`~QObject`，即在 `~ImageWindow` 函数体**之后**。② `QGraphicsEllipseItem` **根本不是 QObject**（`QGraphicsEllipseItem → QAbstractGraphicsShapeItem → QGraphicsItem`），所以 `QPointer<QGraphicsEllipseItem>` 与"多级继承"无关，它压根编译不过/无法工作。③ 因为控件比成员变量活得久，`m_selection.reset()`（:606）之后 `ImageCanvas::m_sel` 成为悬垂指针（canvas 还有 `MarchingAnts` 定时器可能 `viewport()->update()`），`m_layerStack` 析构后 dock 里的 `LayerPanel` 仍持有其裸指针到 `~QObject` 才消失。
- **现象**：目前靠"关闭 tab 时先 `setParent(nullptr)` 隐藏窗口，不触发 paint"侥幸不崩；但任何人按注释去"修"（例如把 reset 挪到 delete ui 之后、或改用 QPointer）都会引入新 bug。
- **修复建议**：`~ImageWindow` 显式顺序化：`m_canvas->setSelectionModel(nullptr)` → `m_textCtrl->removeAllFromSceneAndDelete()` → `m_canvas->scene()->clear()` → `m_undoStack->clear()` → 逐个 `reset()` UI 无关组件 → `delete ui`（或直接让 Qt 释放）；把 `.ui` 的占位 `graphicsView` 删除；`m_brushCursor` 改用"由 ImageCanvas 持有 + `QPointer` 不可用所以用 `QGraphicsScene::addEllipse` 返回值 + scene 生命周期"的文档说明。
- **关联**：ImageCanvas、SelectionModel、LayerPanel。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 缩放百分比显示/输入 | 状态栏左下角可输入 33.3%、100% 等，导航器同步 | 无任何显示；`zoomChanged` 无接收者 | 缺失 | P1 |
| 适合屏幕 / 实际像素 / 填充屏幕 | 三个独立命令，Ctrl+0 / Ctrl+1 / Ctrl+Alt+0 | 只有"适合屏幕"(Ctrl+0) 与 Ctrl+`+/-`，无"实际像素"快捷键 | 部分 | P2 |
| 抓手/空格临时平移 | H 工具 + 按住空格任意工具临时平移 | `ScrollHandDrag` 常开 → 任何工具左键拖动都会平移，与绘画/选框冲突（`ImageCanvas.cpp:37`） | 有但错 | P1 |
| 透明画布棋盘格 | 透明像素显示灰白棋盘 | 无 `drawBackground` 重写、scene 无 backgroundBrush → 透明区显示 viewport 底色 | 缺失 | P2 |
| 画布外 pasteboard | 图像外有灰底区域，可超出画布拖动 | `sceneRect = pixmap rect`（`imagewindow.cpp:1907`），场景=图像，画布外不可滚动 | 缺失 | P2 |
| 标尺 / 参考线 / 网格 | Ctrl+R 标尺、Ctrl+; 参考线、Ctrl+' 网格 | 全无 | 缺失 | P2 |
| 旋转视图 (R) | 旋转画布而不旋转像素 | 无 | 缺失 | P3 |
| 多视图同步（同一文档新窗口） | Window > Arrange > New Window | 无（一个文档一个 QGraphicsView） | 缺失 | P3 |
| 导航器面板 | 缩略图 + 红框 + 缩放滑块 | 无 | 缺失 | P3 |
| 像素网格（>500%） | 显示像素边界 | 无 | 缺失 | P3 |
| 选区显示 | 黑白交替蚂蚁线 + 真实轮廓 + 快速蒙版 | 只有 bbox 矩形虚线（`ImageCanvas.cpp:328-338`）；有 mask overlay 开关 | 有但错 | P1 |
| 文字层保存后可编辑 | PSD 内保留文字层 | 保存即烧图且不可撤销（05-01/05-22） | 有但错 | P0 |
| 文字工具交互 | 只在文字工具下创建；双击已有文字进编辑 | 任何工具双击都建字；右键直接删字（05-25） | 有但错 | P1 |
| 调整层/智能滤镜非破坏 | 参数可反复修改 | 调整面板直接改像素且累乘（05-10）；无调整层实数（Layer::Adjustment 有 LUT 但面板不写它） | 有但错 | P1 |
| 撤销/重做完整性 | 所有操作可 redo | 大量命令 redo 为 no-op（LayerCommand 注释承认）；文字/调整 redo 断链（05-11/05-21） | 有但错 | P1 |
| 分层保存格式 | PSD/PSB/TIFF(layers) | 只能写拍平位图（png/jpg/bmp/tif/webp） | 缺失 | P1 |
| ICC 载入/转换/嵌入 | 打开读 profile、转换到工作空间、保存嵌入 | 仅导出时嵌 ICC；打开不读、显示不转、保存不嵌 | 部分 | P2 |
| EXIF 方向/元数据保留 | 读 EXIF 方向自动旋转、保存保留 | 无 EXIF 读写，竖拍照片横躺 | 缺失 | P2 |
| DPI/物理尺寸 | 图像大小对话框可改 | 硬编码 72 dpi（`imagewindow.cpp:1800`） | 缺失 | P2 |
| 32bit/16bit 文档 | 16/32 位通道 | 载入强制转 8U 且不做位深缩放 | 有但错 | P1 |
| Camera Raw | ACR 全套参数、非破坏 | 死代码（05-24），即使跑通也只是 exposure/sharpness 三个有效参数 | 有但错 | P2 |
| 画布大小/裁剪画布 | Image > Canvas Size / Crop | 只有"调整图片大小"(ImageEditCommand) 与裁剪选区，无画布扩展（pasteboard 尺寸） | 部分 | P2 |
| 文档内存/暂存盘信息 | 状态栏显示文档大小 | 无 | 缺失 | P3 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 透明棋盘格背景 | QT-NATIVE | `QGraphicsView::drawBackground` 或 `scene->setBackgroundBrush(QBrush(checkerPixmap))` 一行搞定；当前没重写 | 生成 16×16 两色 `QPixmap` 作 backgroundBrush，跟随 zoom 用 cosmetic 绘制（约 0.5 人天） |
| 标尺 / 参考线 / 网格 | QT-NATIVE | `drawBackground`（网格）+ 视图边缘自绘（标尺）+ scene 内 1px item（参考线，可拖）；无需第三方 | 参考线用 `QGraphicsLineItem` 集合 + 菜单开关；标尺用 `QWidget` overlay（2~3 人天） |
| 旋转视图 | QT-NATIVE | `QGraphicsView::rotate()` 即可；注意事件坐标要反变换 | 加 `m_viewRotation`，`mapToScene` 已自动处理；UI 加 R 键（0.5 人天） |
| 多视图同步 | QT-NATIVE | 一个 `QGraphicsScene` 可挂多个 `QGraphicsView`，天然同步 | Window > New Window 时再 new 一个 `ImageCanvas` 共享 scene；需把 `m_canvas` 从单实例改成列表（1~2 人天） |
| 导航器/鸟瞰 | QT-NATIVE | 第二个 view + `fitInView` + 画视口矩形 | 3 天 |
| HiDPI 清晰显示 | QT-NATIVE | `QPixmap::setDevicePixelRatio(qApp->devicePixelRatio())`，或按 dpr 生成 2x pixmap；当前 `fromImage` 默认 dpr=1 → 150% 缩放下 100% 视图是"1 图像像素 = 1 逻辑像素"（视觉放大 1.5 倍且发虚）[待确认：Qt6 Windows 默认 per-monitor DPI v2] | `renderToView` 里给 pixmap 设 dpr，并用 dpr 修正 `m_zoom` 的语义（1 人天） |
| 超大图（单边 > 32767 / 纹理上限） | QT-LIMIT | `QPixmap` 受平台/后端尺寸限制（raster 设备相关，D3D/OpenGL 纹理通常 16384），QImage 无此限制但 QPixmap 有；单 item 整图必然失败 | 近似方案：瓦片化 `QGraphicsPixmapItem`（每片 ≤4096²）或 viewport 自绘；无法做到"无限画布"完全无感 |
| 大图瓦片/增量渲染 | QT-EFFORT | `ImageCanvas::invalidateRect` 骨架已有但没用；真正瓦片要改 render/dist 缓存（P8 Compositor 已有 RenderGraph/TileGrid） | 5~8 人天（与 P8 增量合成合并做） |
| 16bit/32bit 文档 | QT-EFFORT | Qt6 `QImage` 有 `RGBA64`，但 `QPainter` 对 16bit 混合支持有限；OpenCV 全链路需 16U；显示要降位 | 8~12 人天（先做 16bit 存储 + 8bit 显示，混合仍 8bit） |
| ICC 载入 + 显示转换 | QT-3RD | lcms2 已集成（`media/icc`，MIT 风格许可），缺 PNG iCCP / JPEG APP2 / TIFF tag 的**读取**与显示转换 | 补 reader（PNG 需解析 chunk、JPEG APP2、TIFF tag 34675），1~2 人天；体积 <1MB |
| EXIF 读写（方向/日期/GPS） | QT-3RD | Qt 的 `QImageReader` 不暴露 EXIF；需 Exiv2（GPLv2/商业双许可，注意合规）或 libexif（LGPL，仅读）或 tinyexif（MIT，仅读） | 只读用 tinyexif（MIT，~1000 行）2 天；读写用 Exiv2（注意 GPL 传染，需评估） |
| PSD 分层保存 | QT-3RD | `psd_sdk`（MIT，header-only + 少量 cpp）可写图层；或 `libpsd`（BSD）。需把 `LayerStack` 映射为 PSD layer records | 3~5 人天 + 图层模型对齐；体积 <1MB |
| RAW 解码 | QT-3RD | LibRaw（LGPL-2.1 / CDDL，vcpkg 可装）已在 CMake 条件编译里，但调用链是死代码 | 修 05-24（2 人天）；包体 +3~5MB |
| 文字高级排版（沿路径/变形） | QT-NATIVE | `QTextLayout`/`QTextDocument` + `QPainterPath` 已足够；当前问题只是烧图 API 用错 | 先修 05-22（0.5 人天） |
| Qt 6.11 杂散 mouse release assert | QT-LIMIT（已 workaround） | `QGraphicsView::mouseReleaseEvent` 无条件读未 engage 的 `QEventStorage`，Qt 6.11 debug 会 ASSERT（`ImageCanvas.h:124-144` 有详细说明） | 现有 workaround 可接受；升级 Qt 后复查 |
| 无限撤销（字节预算） | QT-NATIVE | `QUndoStack` 支持自定义 `undoLimit`，但无字节预算；需子类化 | 自己包装 `QUndoStack`（1 人天，配合 05-09） |

## 5. 设计问题

1. **`m_current` 一个变量承担两种矛盾角色**（渲染缓存 / 就地编辑目标）。它是本模块 05-02/03/10/12/15/30 的共同根因。→ 拆成 `m_renderCache`（只读、可丢弃）+ `m_editLayer`（编辑目标）。
2. **上帝类 + 53 处手写刷新**：ImageWindow 3055 行，`friend class` 6 个（`imagewindow.h:633-641`），组件直接读改 `m_current`/`m_original`/`ui`/`m_undoStack`。任何新操作都要作者"记得"invalidate+render，漏一处就是静默 bug（05-13）。→ 定义 `Document`（数据 + 版本号 + `changed` 信号），View 订阅；禁止 friend。
3. **两种所有权模型混用**：`unique_ptr` + Qt parent 双拥有（`m_layerStack(this)`、`m_mosaicTool(this)`、`m_textCtrl(this)`、`m_io(this)`、`m_dialogMed(this)`、`m_toolMed(this)`、`m_ctx(this)`、`m_selection(this)`、`m_smartWatcher(this)`），另有裸指针 + Qt 拥有（`m_canvas`、`m_rightDock`、`m_adjustmentPanel`、`m_layerPanel`）。**成员先于 QObject 子树析构**造成短暂悬垂窗口（05-26）。头文件 `imagewindow.h:508-516` 自己写了策略，但只有一半代码遵守。
4. **异步任务没有"取消 + 生命周期"协议**：共享 `m_currentTaskId`、receiver 用 `QCoreApplication`、回调持裸 `this`、`m_asyncEnabled` 从不关闭（05-04/05-12）。→ 统一的 `AsyncJob` 封装：`QPointer` receiver + 每文档 generation + `cancelAll()`。
5. **保存/写盘路径与显示路径耦合**：`onSave` 读 `m_current`（可能过期、可能带预览覆盖）、`onExport` 先烧文字、`onSave` 不写元数据（05-01/03/20）。→ 引入 `Document::exportComposite()`（保证最新、不含 UI 临时态）作为唯一写盘入口。
6. **命令模式被"简化"掏空**：`ImageEditCommand::redo` no-op、`LayerCommand` 十几个 op 的 redo no-op、调整 redo no-op（注释承认 trade-off）。→ 用户心智里"撤销/重做"是双向的；要么实现，要么在 UI 上隐藏 redo 按钮（当前不隐藏）。
7. **坐标转换没有单一封装**：`eventFilter` 里到处 `mapToScene` + `int()` 截断（`imagewindow.cpp:2650-2656`），没有 clamp/取整策略；`hitTest` 在 ImageCanvas 是空 stub，真实 hitTest 写在 ImageWindow（重复两套几何判断）；文字 item、选区、变换框各自的坐标空间没有文档说明。→ `ImageCanvas::viewToImage(QPoint) const` 单一入口 + `QRect` 内 clip。
8. **调试设施混进生产路径**：`D:/vistella_events.log`（05-18）、`LOG_DEBUG` 每帧、`statusBar` 里塞调试文案。
9. **死代码/空壳占比高**（05-31、05-27、05-34）：`ImageIOController::onOpen` 整段死、RAW 整链死、`ImageCanvas::hitTest` 空、`applyParams` 空、`invalidateRect` 只有测试用。读者无法判断哪些是"真的架构"。
10. **迁移半途**：`imagewindow.ui` 只剩占位 `graphicsView`；右侧 dock 从 ImageWindow 搬到 MainWindow（P7.6.x）只搬了左侧，导致选项面板跨文档裸指针（05-05）；`.bak31` 文件（`AdjustmentPanel.cpp.bak31`、`AdjustmentPanel.h.bak31`，720+190 行）直接躺在 src 树里。
11. **可测试性**：所有状态在 ImageWindow 私有成员里，只能靠 friend 或公开缺口访问；`renderToView` 直接碰 QPixmap（无注入点），测试只能开真窗口。→ 抽 `IDocument`/`IRenderer` 接口。
12. **命名/注释**：`refreshAll()` 只是 `renderToView()`（不 invalidate）；`invalidateCurrentCache()` 不触发重绘；`setCurrentImage` 语义是"替换文档"还是"改显示"没人说得清（同一函数被 8 个模块用）。
13. **像素格式没有契约**：`Layer::image` 允许 1/3/4 通道 + 任意位深，`LayerStack` legacy / compositor / `matToQImage` / `saveImage` / 各种工具各自假设 8UC3。05-07（16bit 白屏）、05-08（4 通道花屏）、05-35（灰度/alpha 全黑）、以及 `LayerStack` 内部多处 `cvtColor` 兜底，都是这条缺失的症状。→ 在 `Layer` 层强制 `canonicalize()`（BGRA8 或 BGR8 + 独立 alpha），入口一次转换，内部只用一种格式。

## 6. 修复建议（按优先级排序）

**P0 立即修（约 4~6 人天，风险中）**
1. 05-01/05-20：移除三处保存路径里的 `flattenText()`；写盘用"合成到临时 Mat"；`onExport` 的顺序改为"先选参数/路径 → 再合成 → 写盘"。（0.5 天）
2. 05-02/05-10/05-15/05-30：把 `m_original` 改成载入后只读的 `m_baseImage`，`setCurrentImage` 不再写它；新增 `Document` 层明确 `baseImage / renderCache` 两个数据的写权限。（1~2 天，改动面广但机械）
3. 05-04/05-12：`renderAsync` 加 `QPointer` receiver + generation；`~ImageWindow` 先作废 token；render/adjust job id 分域；丢弃结果时归位 dirty。（1 天）
4. 05-05：`MosaicOptionPanel/TextOptionPanel` 改 `QPointer` + `rebind()`，`ImageOptionBar::attach/detach` 同步。（0.5 天，跨模块）
5. 05-06：`flattenText` 清 `m_handleDragItem`；`handleDragItem` 改 QPointer。（0.2 天）
6. 05-07/05-08/05-35：16bit 缩放映射；`matToQImage` 4 通道用 `Format_RGBA8888`；compositor 根节点做通道归一化（灰度/带 alpha 图全黑）。（1 天，跨 compositor 模块）
7. 05-03：`onSave/onSaveAs` 前置 `refreshCurrentCacheSync()`。（0.3 天）

**P1 本轮修（约 6~8 人天，风险中低）**
8. 05-09：`ImageEditCommand` 改 ROI patch + 字节预算撤销栈。（2 天，涉及 tools 模块）
9. 05-13/05-14：`invalidateCurrentCache` 内部调度一次合并重绘，删掉 50+ 处手写 `renderToView`；`onStackChanged` 统一 invalidate。（1 天）
10. 05-17：ImageCanvas 持有可复用 QPixmap/QImage；马赛克/画笔走 `invalidateRect`。（2 天）
11. 05-19：`clampZoom` + `setTransform` 累乘 + `resizeEvent` fit 模式 + 状态栏百分比。（1 天）
12. 05-11/05-21/05-22：调整 redo 实现、文字改模型化、烧图用 `item->paint`。（2 天）
13. 05-18/05-25：删调试日志；双击/右键按当前工具分派。（0.3 天）
14. 05-23/05-24：拆 `loadFile`；修 RAW 链路（含保留原路径、删临时文件）。（1 天，跨模块）

**P2 后续**
15. 05-26/05-27/05-34：重写析构顺序、删 `.ui` 占位控件、清死代码/`.bak31`。
16. 05-29：选区真实轮廓 + 黑白蚂蚁线。
17. 05-28：瓦片化大图（与 P8 增量渲染合并）。
18. 05-20/05-32：SaveOptions 扩展 + DPI/EXIF 读写。
19. 05-36/05-37/05-38：`isDirty` 改文档 revision、预览状态归宿主并在 `done()` 清理、笔刷光标生命周期改由 `ImageCanvas` 持有。
20. §3 缺失功能：缩放百分比、棋盘格、抓手/空格、画布外区域。

**架构级重构**
21. 把 `ImageWindow` 拆成 `Document`（数据 + undo + IO + 版本号）+ `DocumentView`（canvas + 坐标 + 工具路由）+ `DocumentPanels`，禁止 friend；组件只依赖 Document 的公开接口。
22. 引入分层容器（PSD 或自定义 `.vistella`），把"保存"从"导出显示结果"变成"序列化文档"。
23. 定义全项目的**像素格式契约**（单通道/3 通道/4 通道 + 位深），所有层、compositor、显示、保存都按同一契约归一化（当前 05-07/05-08/05-35 都是这条缺失的症状）。
24. 统一异步任务框架（取消/生命周期/进度/背压），替换现有散落的 `submit_fn + invokeMethod`。

## 7. 需要 Lead 协调的跨模块问题

1. **`ImageProcessor::matToQImage` 4 通道 bug（05-08）** 属于照片/图像处理模块，但所有画布显示都经过它；修法（`Format_RGBA8888`）会影响 `ImageProcessor` 的单测与其它调用方，需与 `imageprocessor` 审查员对齐。
2. **compositor 通道归一化（05-35）** 在 P8 compositor 模块（`nodes/LayerBitmapNode.cpp` + `RenderGraph.cpp`）：`canvas` 固定 `CV_8UC3`、瓦片类型不匹配静默跳过，直接导致灰度/带 alpha 图全黑；同一处还有 `cv::Mat tile(...)` 未初始化的边缘脏数据。修法必须在 compositor 侧加归一化节点或 fallback，本模块只能记录 hasAlpha。
3. **`LayerStack::renderAsync` 的 receiver/取消（05-04）** 在图层模块；本模块只能加防御，根治需改 `renderAsync` 签名（增加 `QPointer<QObject> receiver`），涉及图层模块测试。
4. **`LayerCommand` 的 redo no-op 与"undo 不 emit 信号"（05-11/05-14）** 属图层模块；与"调整面板撤销"、"图层菜单撤销"两条链路冲突，建议由 Lead 统一决策"命令是否必须可 redo"。
5. **`ImageOptionBar` 选项页跨文档裸指针（05-05）** 在 tools 模块（P7.6.x 迁移未完成），与 09 模块 09-01（左侧工具栏取色死链）同源；需要 tools 审查员配合改 `attach/detach/rebind`。
6. **`FilterDialog` 未重写 `reject()/closeEvent`**（Esc/X 关闭时 `clearPreview` 不执行 → 画面永久停留在预览、registry 泄漏）属 filters 模块；本模块的 `s_previewRegistry` 也不在 `~ImageWindow` 清理（与 09-09 重叠）。需要统一：预览状态归 `ImageWindow` 管理，`FilterDialog` 只在 `done()` 里清。
7. **保存/关闭的 dirty 流程**：`MainWindow::onTabCloseRequested`（`mainwindow.cpp:1848-1852`）用 `img->isDirty()` 判断保存是否成功，但 `savePublic()` 不返回 bool，且 `flattenText`/`setCurrentImage` 不进撤销栈 → `isDirty` 与"文件是否最新"不等价。需与 app 模块一起定义 `bool ImageIOController::save()` 返回值语义。
8. **`ImageWorkspace::loadFile/save/saveAs`（`src/media/imageworker/ImageWorkspace.cpp:72-98`）** 是对 `ImageWindow` 的旧包装（`save()` 恒 true、`saveAs(path)` 忽略 path），与主窗口新链路并存；是否删除需与 media/imageworker 审查员确认。
9. **RAW/LibRaw 与 CMake**：`MULTIDOC_HAVE_LIBRAW` 条件编译已存在但调用链死；若要实装需 vcpkg 依赖 + 包体增加，属构建级改动。
10. **PSD 分层保存**需引入第三方库（psd_sdk MIT 或 libpsd BSD），涉及 CMake + 图层模型，属于产品级决策，非单模块可定。
11. **与 09-panels.md 的重叠**：09-03（保存烧图）、09-09（preview registry 泄漏）、09-11（保存不写 ICC）与本报告 05-01/05-20 结论一致；建议最终报告合并成一条并保留本模块的"机制层根因（setCurrentImage 写 m_original / 写 base layer）"以免重复。
