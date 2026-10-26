# MultiDoc / Vistella — 全项目代码审计报告

> **审计日期**：2026-10-26
> **审计范围**：`src/` 全部生产代码（350 文件 / 53,033 行），**不含** `tests/`（用户要求排除）
> **审计方式**：9 个模块分工 + 逐文件通读 + 交叉验证 + Lead 汇总。每个模块的完整缺陷清单（含 `文件:行号` 与代码证据）见 `audit/modules/`。
> **代码快照**：审计期间（14:09–14:30）工作区存在**并发修改**（`ImageCanvas.cpp/.h` 被另一个进程按 P8.3.3 改动，另有 `_probe.png`/`.bak33` 产生）。本报告结论基于该时间窗内读到的代码；与最新工作树可能有个别行号偏移。

---

## 0. 阅读指南

### 0.1 交付物

| 文件 | 内容 |
|---|---|
| **本报告** | 全局结论、P0/P1 总榜、PS 功能对照、Qt 能力边界、架构问题、修复路线图 |
| `audit/RUBRIC.md` | 审计规范（严重度/缺陷类型/输出格式定义） |
| `audit/modules/01-app-shell.md` | 外壳层：main / MainWindow / 菜单 / tab / 窗口状态机 / QSS / 启动（48 条） |
| `audit/modules/03-core-infra.md` | core/线程池/日志/Session/Recent/用户/网络/AI（38 条） |
| `audit/modules/04-tools.md` | 15 个工具 + 工具栏/选项栏 + 工具状态机（42 条） |
| `audit/modules/05-canvas.md` | ImageWindow/ImageCanvas/IO/调整面板/文字/马赛克/CameraRaw（35 条） |
| `audit/modules/06-pixel-ops.md` | ImageProcessor/选区/蒙版/变换/画笔引擎/文字图元（71 条） |
| `audit/modules/07-layers.md` | LayerStack/LayerPanel/LayerCommand/BlendMode/SmartObject（51 条） |
| `audit/modules/08-render.md` | Compositor DAG/GPU/滤镜/液化（49 条） |
| `audit/modules/09-panels.md` | docks/dialogs/mediators/ICC（18 条，Lead 亲审） |
| `audit/modules/02-ui-layer.md` | ui 层：HomePage/文件树/设置/登录/主题画廊 + 自研可拖拽 dock（42 条） |

### 0.2 缺陷编号前缀

`APP-` 外壳 · `CI-` 基础设施 · `04-` 工具 · `05-` 画布 · `06-` 像素算法 · `LYR-` 图层 · `CP-` 渲染/滤镜 · `09-` 面板/对话框

### 0.3 严重度定义

| 级别 | 判定 |
|---|---|
| **P0** | 崩溃、数据丢失/损坏、内存破坏/UAF、竞态崩溃、保存内容错误 |
| **P1** | 用户可见的功能错误、交互逻辑断裂、撤销栈错乱、状态不同步、>500ms 阻塞、大图必现的泄漏 |
| **P2** | 边缘情况错误、体验缺陷、UI 不一致、小量泄漏、DPI/i18n |
| **P3** | 代码质量、命名、注释、重复代码、可维护性 |

---

## 1. 执行摘要

### 1.1 一句话结论

**这个项目的"设计意图"和"实际可用性"之间存在系统性落差**：架构骨架（分层、Mediator、ToolState、Compositor DAG、瓦片缓存、GPU 后端抽象）方向正确、注释密度罕见地高，但**核心用户路径几乎每一条都有 P0/P1**——保存会毁文档、退出不保存、图层撤销/重做失效、默认渲染路径对非 3 通道图全黑、画笔笔迹不进图层、并发渲染回调 UAF、线程池任务永不释放。UI 与 PS 的差距主要不是"控件长得不像"，而是**交互语义不完整 + 大量功能是"声明完成、实际不工作" + 状态在多处各存一份**。

### 1.2 总体数据

| 指标 | 数值 |
|---|---|
| 生产代码 | 350 文件 / 53,033 行（不含 tests 与 third_party） |
| 单元测试 | 79 个 QTest 文件 / 82 个测试可执行文件（本次不审查） |
| 审查模块 | 9 个 |
| 缺陷总数 | **394 条**（按各模块报告自报口径汇总） |
| **P0** | **34 条** |
| P1 | 162 条 |
| P2 | 161 条 |
| P3 | 37 条 |
| **模块健康度** | **全线 3–4 / 10**（图层 3/10 最低） |
| 结论 | 不是"修几个 bug"的量级；建议按下文 §9 的路线图分批推进，先保数据安全，再修核心链路，最后补 PS 语义 |

各模块缺陷分布（按各报告自报口径）：

| 模块 | 文件/行数 | P0 | P1 | P2 | P3 | 健康度 |
|---|---|---|---|---|---|---|
| 01 外壳层 `src/app` | 7 / 5,059 | 2 | 15 | 20 | 11 | 4/10 |
| 02 UI 层 `src/ui`+`ui/dock` | 37 / 5,036 | 1 | 14 | 18 | 9 | 4/10 |
| 03 基础设施 core/logger/user/network/ai | 52 / 6,600 | 3 | 10 | 21 | 4 | 4/10 |
| 04 工具系统 `src/media/tools` | 57 / 6,837 | 3 | 18 | 21 | 0 | 4/10 |
| 05 画布/文档 `imagewindow.*` | 17 / 6,400 | 9 | 18 | 6 | 2 | 3/10 |
| 06 像素算法/选区/变换/画笔 | 40 / 8,700 | 1 | 34 | 30 | 6 | 4/10 |
| 07 图层系统 `imageworker` | 22 / 5,837 | 12 | 19 | 20 | 0 | 3/10 |
| 08 渲染/滤镜/液化 | 62 / 6,121 | 2 | 30 | 17 | 0 | 4/10 |
| 09 面板/对话框/ICC | 57 / 5,840 | 1 | 4 | 8 | 5 | 4/10 |

### 1.3 最致命的 10 个问题（跨模块）

按"用户损失 × 发生概率"排序：

1. **保存会破坏正在编辑的文档（05-01/05-02/09-03）**：Ctrl+S 先把场景里的文字 item 删除烧成像素（不可撤销），再把合成图写回 base 层 → 多图层被重复合成、`m_original` 被污染、后续撤销基线错误。保存一次，文档就"坏"一次。
2. **退出时点"保存"其实不保存（APP-01）**：`closeEvent` 只处理了 `DocWindow`，`ImageWindow` 完全没有保存分支，弹窗点"保存"后直接退出；随后还把 `lastExitClean=true` 写盘，连崩溃恢复提示都没有。
3. **默认渲染路径让灰度/带 alpha 图全黑（CP-01 / 05-35）**：合成 canvas 固定 `CV_8UC3`，1ch/4ch 瓦片被静默丢弃；而 `m_compositorEnabled` 默认 `true` 且**没有任何 UI 开关可回退**。
4. **图层撤销/重做整体失效（LYR-07/08）**：undo 只改字段、不 emit、不 invalidate、不重绘（视觉无变化）；除 Merge 外 30+ 个命令的 `redo()` 全是 no-op（重做永远无效）；合并/拼合的 undo 不恢复上层图层。
5. **图层身份系统不可靠（LYR-06/12/05）**：`Remove::redo` 用 `cv::Mat::data` 指针反查图层 → 删错层；`m_groups` 以对象地址为 key → 地址复用串组；"编组"按钮把整栈打包导致 `canvasSize()` 退化为 800×600。
6. **异步渲染回调 UAF（CI-03 / 05-04 / LYR-10）**：`QMetaObject::invokeMethod(qApp, ...)` 用 `qApp` 作 context、lambda 捕获裸 `this`，关闭 tab 后事件仍会派发 → 触碰已析构的 `ImageWindow`/`LayerStack`。
7. **线程池任务永不释放（CI-01）**：`run_task` 不 `delete Task`，而 `AdjustmentPanel` 每次滑块提交都捕获整图 clone → 拖一次滑块泄漏数 GB，直至 OOM；`work_stealing_queue` 的 Chase-Lev `pop()` 还有 off-by-one（任务重复执行/丢失），目前只是恰好没走那条路。
8. **画笔笔迹根本不属于图层（04-02/04-03）**：Brush/Heal/Patch/RedEye 只写"合成缓存"`m_current`，任何图层操作都会让已画内容消失；带 alpha 的图上一画就整块像素写花。
9. **工具核心动作是假的（04-04/04-05/04-13）**：裁剪工具只 push 一条 redo 恒 no-op 的命令、从不真正裁剪；画笔选项面板被 Mosaic 页抢占，永远不可达（画笔锁死 13px 黑笔）；吸管采色无消费者；MoveTool 22 行空壳。
10. **变换方向错误（06-01/06-07）**：`qTransformToAffine` 把 QTransform 的 `m12/m21` 转置写入 cv 2×3 矩阵 → 旋转/斜切结果与包围盒方向相反，保存出来是错的。

### 1.4 对你的主观感受的对应解释

| 你的感受 | 根本原因（报告依据） |
|---|---|
| "UI 做得天差地别" | 顶级菜单顺序错乱（帮助夹在中间）、无工具组/长按弹出、15 工具 vs PS 70+、面板大量是静态假数据、暗色主题 QSS 自相矛盾导致 tab 白底白字、全项目 80+ 处 inline stylesheet 不随主题刷新 |
| "功能不完整" | 没有工程文件格式（保存即扁平，图层/蒙版/智能对象/文字全部丢失）、没有自动保存、没有调整子菜单、没有剪切/复制/粘贴、没有渐变/图案/加深减淡等整类工具、没有选区修改/存储选区、没有路径/通道真实数据 |
| "还有很多 bug" | 33 个 P0 集中在：保存链路、图层撤销、并发渲染、工具写入、变换矩阵、16bit 转换、颜色同步死链、预览缓存泄漏 |
| "交互逻辑要完整" | 工具键盘事件链断裂（Enter/双击收不到 → 多边形永远闭合不了）、12 个单键快捷键抢走文字输入、颜色三份副本互不同步、选项面板绑定错误、dock attach 不 detach 导致重复连接 |
| "不求和 PS 一模一样" | 完全可以做到"大体一致"——绝大多数差距不是 Qt 限制，而是**没有实现完**或**实现错**（见 §6） |

---

## 2. 项目全貌

### 2.1 目录与规模

```
src/
├── app/            外壳：main.cpp(317) / mainwindow.cpp(3518) / 菜单 / QSS / QML splash
├── ui/             HomePage / 文件树 / 设置 / 登录 / 主题画廊 / 自研可拖拽 dock(1,490)
├── core/           主题/会话/最近文件/语言/扩展名路由 + ThreadPool(2,072) + Async/Result/Throttle
├── logger/         独立 SHARED 库（spdlog 1.15 + SizeDailySink + 异步）
├── models/         RecentListModel
├── user/ network/ ai/   SQLite 用户 / OAuth+HTTP / AI 模型注册（多为 stub）
└── media/          图像处理全部（约 38k 行）
    ├── imagewindow.*            文档窗口（3055+644 行，"上帝类"）
    ├── imagewindow/             ImageCanvas / IO / 调整面板 / 文字 / 马赛克 / CameraRaw
    ├── imageworker/             IModule 包装 + 图层系统（LayerStack 1678 / LayerPanel 1095 / LayerCommand 740）
    ├── compositor/              DAG + 瓦片缓存 + 15 个节点（P8 新渲染路径）
    ├── gpu/                     CPU/GPU 后端抽象（Qt OpenGL）
    ├── tools/                   15 工具 + 工具栏/选项栏/工具状态机（6,837）
    ├── brushes/                 画笔引擎/预设/ABR/压感/平滑（2,087）
    ├── filters/                 48 个滤镜枚举 + 对话框 + 液化（liquify）
    ├── docks/ dialogs/ mediators/  右侧面板 / 5 个调整对话框 / 3 个 Mediator
    ├── selection/ masks/ transform/  选区 / 颜色范围+边缘 / 自由变换
    ├── icc/                     lcms2 封装 + JPEG/PNG/TIFF ICC 嵌入
    └── imageprocessor.*         唯一 cv::Mat↔QImage 桥 + 算法库（999 行）
```

### 2.2 真实架构（与文档宣称的差异）

```
MainWindow (QMainWindow, 3518行, "上帝对象")
 ├─ 自绘标题栏 + 12 个顶级菜单 + QTabWidget + HomePage/Workspace 切换 + 左侧4 dock + 状态栏
 └─ tab 内是 ImageWindow (QMainWindow!, 3055行)
      ├─ ImageCanvas (QGraphicsView: 1 pixmap item + N text item)
      ├─ m_original / m_current (cv::Mat) ← "渲染缓存"与"就地编辑画布"双重角色
      ├─ LayerStack ── render() ─┬─ legacy applyBlend 路径（15 种混合）
      │                          └─ CompositorGraph 路径（默认开启，P8）
      ├─ QUndoStack（8+ 种命令风格混用）
      ├─ EngineContext（每个文档自建 5 个线程池）
      └─ RightPanelDock (3 组 VDraggableTabWidget → 9 个 V*Page 适配器)
```

关键设计事实（也是问题的来源）：

1. **`ImageWindow` 是 `QMainWindow`**，却被 `setWindowFlags(Qt::Widget)` 塞进 tab → 每个 tab 自带状态栏/空菜单栏，屏幕模式失效（APP-15）。
2. **`vistella_media` 是一个约 200 文件的巨型 STATIC 库**：tools/docks/filters/compositor/layers/icc/selection 全在一个 target 里；`PUBLIC` 链接 OpenCV 使 `opencv2/core.hpp` 通过 `imagewindow.h` 泄漏到 `vistella_app` 的公共头。
3. **层间存在循环**：`vistella_media` 的 `RightPanelDock.cpp` include `ui/dock/VTabPageBase.h`，而 `vistella_ui_dock` 的 `VPages.h` include `media/docks/*` 且 link `vistella_media` → ui-dock ↔ media 互相依赖，靠静态库链接顺序兜住。
4. **双渲染路径**：legacy `LayerStack::render()` 与 `CompositorGraph`（`LayerStackAdapter` 刻意"镜像" legacy 行为），默认走新路径，**没有回退开关**，两条路径在 alpha/隐藏 base/尺寸不一致/anchor 语义上已经漂移。
5. **三套线程设施并存**：`ThreadPool`（自研 Chase-Lev，有 bug）、`SerialExecutor`、`RealtimeThread`；`parallel_for`/`task_graph`/取消树/`SpscRing` 均为零调用死代码。

### 2.3 依赖与构建

| 项 | 现状 | 问题 |
|---|---|---|
| Qt | 6.11.1 Widgets + Quick(仅 splash) + Sql + LinguistTools + OpenGL | 版本很新，能力足够 |
| OpenCV | 4.12 **world** 构建（127MB debug DLL） | world 模式无子模块 target；debug/release DLL 混用曾导致堆破坏 |
| LibRaw | 可选（找不到则 CameraRawLoader stub） | 当前是否找到需看配置；stub 时 UI 仍暴露 RAW 入口 |
| lcms2 | 2.18（源码 vendor） | 仅用于"导出时嵌 ICC"，未用于色彩管理 |
| spdlog | 1.15 header-only | 异步 + 自定义 SizeDailySink |
| ONNX 模型 | `third_party/landmark/*.onnx` | 路径写死 `D:/Collide/...`（FaceDetector.cpp:52） |
| 构建 | CMake 3.19 + MSVC2022 + `/utf-8` + `/INCREMENTAL:NO` | 巨型 target 导致 .ilk 100MB+；曾产出损坏 PE |
| 部署 | windeployqt + 手工删/拷 DLL + 手工拷 OpenCV/LibRaw DLL | 非常脆弱，注释里记录了多轮"启动即崩"的排查 |
| CI | **无**（无 `.github/`、无流水线配置） | 79 个测试靠手工跑，`Testing/` 与 `ctest_*.log` 满天飞 |

### 2.4 UI 层与自研 dock 系统（02）

`src/ui` + `src/ui/dock`（37 文件 / 5,036 行，含 607 行的自研 `VDraggableTabWidget`）由 UI 审查员完成，**42 条缺陷（P0 1 / P1 14 / P2 18 / P3 9），健康度 4/10**。核心结论：

- **自研面板拖拽系统实际 0% 可用**：`drop/dragEnter/dragMove` 把 `e->pos()`（局部坐标）当全局坐标传给 `mapFromGlobal` → `insertIdx` 恒 -1，drop 被静默 accept 但不搬页（UI-02）；浮动 wrapper 用 `parent=this` 创建 → `isFloating()==isWindow()` 恒 false，浮不起来（UI-05）；`m_pages`（含 floating）与 `QTabBar`（仅 docked）双索引混用 → 切页/改名/删除错页（UI-04）。
- **P0 隐患**：跨 `ImageWindow` 移动面板页后，源窗口 `RightPanelDock`/`m_adjustmentPanel` 与 `AdjustmentPanel::m_host` 仍是裸指针 → 任一窗口销毁即 UAF（UI-01，目前被 UI-02 掩盖，drop 修好后必现）。
- **文件树**：同步双全扫 + 逐项 `stat` 卡 UI；空/无权限目录 `hasChildren` 恒 true 且失败仍标记 lazyLoaded；无 watcher/刷新（UI-06/07）。
- **设置/主题/语言/GPU 四条链全部未闭环**：主题与"自动保存"控件零连接；`.ts` 839 条未翻译 + 双 translator + 无 retranslate；GPU 页启动从不 probe → 强制 GPU 永远落 CPU（UI-08~11）。
- **打开路径无路由**：`.mp3/.mp4` 走 `cv::imread` 失败后当文本整块读入（乱码/OOM）；`DocWindow` 只 UTF-8 + `Truncate` 保存 → GBK 文本保存即损坏、非原子写。
- Qt 结论：文件树应用 `QFileSystemModel`、面板应用 `QMainWindow + QDockWidget + saveState`（覆盖约 80% 的 PS 面板行为），自研方案在重造轮子；仅网络盘可靠递归监听属 `QT-LIMIT`。

---

## 3. P0 缺陷总榜（34 条）

> 修复优先级：全部建议在"任何新功能之前"处理。每条的证据代码与完整分析见对应模块报告。

### 3.1 数据安全与持久化（10 条）

| ID | 位置 | 现象 | 修复方向 |
|---|---|---|---|
| APP-01 | mainwindow.cpp:264-294 | 退出时图片文档点"保存"实际不保存 | 补 `ImageWindow` 保存分支或统一走 `onTabCloseRequested` |
| 05-01 | ImageIOController.cpp:75,101,128 | 保存/另存/导出都先 `flattenText()`：文字 item 被删、不可撤销；导出**在用户选路径前**就已烧图 | 删除这三处 `flattenText`；导出用临时合成副本 |
| 05-02 | TextOverlayController.cpp:196-207 + imagewindow.cpp:1850-1861 | 保存一次后文档被改坏：多图层重复合成、`m_original` 被污染 | `setCurrentImage` 不写 `m_original`；`flattenText` 走命令 |
| 05-03 | imagewindow.cpp:1894,1964-1971 | 隐藏图层/改蒙版/应用智能变换后立刻 Ctrl+S → 存的是旧像素 | 保存前 `if (m_currentDirty) rebuildCurrentCache()` 并等待异步完成 |
| 09-03 | 同 05-01 | 面板/IO 视角同一问题（保存毁文字） | 同 05-01 |
| CP-11 | FilterCommand.cpp:62 + imagewindow.cpp:835 | 滤镜/液化作用于合成图再写回 base 层 → 上方图层被叠加两次、undo 不可回原状 | 滤镜必须作用于"活动图层"，命令保存该层 before/after |
| 04-02 | Brush.cpp:131/190/251 + imagewindow.cpp:879-884 | 画笔笔画只写合成缓存，任何图层操作都会擦掉已画内容 | 笔画写活动图层（或临时 stroke layer），按图层出 undo |
| 04-03 | BrushEngine.cpp:215-263 | 4 通道图上 `Vec3b` 寻址 → 像素写花；灰度图恒画白色 | `applyStamp` 按 channels 分支 / 统一 BGRA |
| LYR-03 | LayerCommand.cpp:464-474 | 合并 undo 明确"不重建上层" → 上层永久丢失 | 命令保存上层完整 Layer 并按原下标重建 |
| LYR-04 | imagewindow.cpp:745-767,1299-1320 | 拼合图像不可撤销，面板入口连撤销记录都没有 | 引入 `LayerSetCommand` 整栈快照/事务 |

### 3.2 崩溃 / UAF / 并发（9 条）

| ID | 位置 | 现象 | 修复方向 |
|---|---|---|---|
| APP-02 | mainwindow.cpp:1872-1884,3371-3418 | 关当前图后点左侧工具按钮 → UAF（`blockSignals` 压掉 `currentChanged`，dock 未重绑） | `removeTab` 后显式 `onTabChanged`；`attach` 先 `detach`；存 `QPointer` |
| 04-01 | imagewindow.cpp:2956-2959 + ImageCanvas.h:160 | Ctrl+T 后切工具 → `ImageCanvas::m_box` 悬垂，重绘即崩 | `toolChanged` 时 `setTransformBox(nullptr)` 或改 `QPointer` |
| CI-03/05-04/LYR-10 | thread/render 回调 | `invokeMethod(qApp,...)` + 裸 `this`，关 tab 后 UAF（三份报告独立命中同一根因） | 用 `QPointer<ImageWindow>` 作 receiver/守卫 + generation token |
| 05-05 | ImageOptionBar.cpp:157-185 | Mosaic/Text 选项页存裸指针且只建一次，换文档后操作即访问已析构对象 | 面板存 `QPointer`，`attach` 时 rebind/重建 |
| 05-06 | TextOverlayController.cpp:199-207 | 拖文字控制点时 Ctrl+S → `m_handleDragItem` 悬垂，鼠标一动就崩 | flatten/removeAll 时置空 drag item |
| LYR-02 | LayerCommand.cpp:75-92 | 对文字/组/异尺寸层"向下合并"→ `cv::addWeighted` 空 Mat 抛 cv::Exception（未捕获） | 统一走 rasterize+applyBlend，空 Mat 早退 |
| LYR-09 | LayerStack.cpp:1775-1809 | 后台渲染无锁读 `m_layers`，UI 线程同时改栈 → 数据竞争 | 提交前不可变快照 / 加锁 + 队列化 |
| LYR-01 | LayerStack.cpp:1389-1440 | 文字/矢量层 rasterize 丢 alpha（`BGRA2BGR`）→ 黑底覆盖整幅画布 | 4 通道 BGRA 合成或独立 alpha 掩码 |
| LYR-12 | LayerStack.cpp:1688-1696 | `clear()/flatten` 不清 `m_groups`（地址 key）→ 泄漏 + 串组 | `m_groups.clear()`；地址键改稳定 id |
| UI-01 | VDraggableTabWidget.cpp:377 | 跨 ImageWindow 移动面板页后，源窗口 `RightPanelDock`/`m_adjustmentPanel`/`AdjustmentPanel::m_host` 悬垂 → 任一窗口销毁即 UAF（当前被 drop bug 掩盖，修好 drop 后必现） | 禁止跨顶层窗口移页，或移页时 rebind host + 宿主侧改 `QPointer` 并订阅 page 迁移信号 |

### 3.3 渲染 / 变换 / 保存内容错误（7 条）

| ID | 位置 | 现象 | 修复方向 |
|---|---|---|---|
| CP-01 / 05-35 | RenderGraph.cpp:56-72 | 灰度/带 alpha 文档在默认 compositor 路径下全黑（非 3ch 瓦片被静默丢弃） | 节点入口/出口统一通道归一；类型不匹配至少 `LOG_WARN` |
| 06-01 | imageprocessor.cpp:375-388 | `QTransform→cv` 2×3 矩阵转置 → 旋转/斜切方向相反，保存错误 | `M=[m11 m21 dx; m12 m22 dy]` |
| 05-07 | imagewindow.cpp:857-861 | 16bit PNG/TIFF/HDR 打开后全白（`convertTo(CV_8U)` 默认 alpha=1 饱和而非缩放） | `alpha=255.0/65535.0`，并保留 16bit 管线 |
| LYR-05 | LayerPanel.cpp:856-861 | 点"编组"把整栈打包 → base 进组、`canvasSize()` 退化 800×600 → 画面变白底 | 按选区编组；画布尺寸独立于图层 |
| LYR-06 | LayerCommand.cpp:677-687 | `Remove::redo` 用像素指针反查 → 删除后 redo 删错图层 | 命令持稳定 `LayerId` |
| LYR-07 | LayerCommand.cpp:403-659 | undo 只改模型不发信号/不重绘 → Ctrl+Z 视觉无效 | undo/redo 末尾统一 emit+invalidate+render |
| LYR-08 | LayerCommand.cpp:661-782 | 30+ Op 的 `redo()` 是 no-op → 重做永远无效 | redo 携带 new 值重新应用 |

### 3.4 资源 / 内存 / 线程基础设施（4 条）

| ID | 位置 | 现象 | 修复方向 |
|---|---|---|---|
| CI-01 | thread_pool.cpp:198-235 | 每个执行完的 Task 都不 `delete`；配合全图 clone → 拖一次滑块泄漏数 GB | `delete t`（含早退分支）或全程 `TaskPtr` |
| CI-02 | work_stealing_queue.hpp:41-69 | 自研 Chase-Lev `pop()` off-by-one：同一任务反复执行、末元素必失败 | 按标准实现修正；建议直接换 oneTBB/Taskflow |
| 05-09 | imagewindow.cpp:235-245,564 | 24MP 每笔撤销存 before+after 全图（144MB），100 笔≈14GB | 按 ROI 存 patch；`QUndoStack` 加字节预算 |
| LYR-11 | LayerStack.cpp:928,1465 | `cv::imread/imwrite(path.toStdString())` 在中文用户名下失败（智能对象 MISSING） | 用 QImage/QSaveFile 或 `toLocal8Bit` |

> 另有 CP-02（TileCache 无上限，4000×3000×10 层单次渲染峰值约 780MB）、CP-04（并行合成提交失败/异常 → 忙等死循环）虽标 P1，但在"接线后立即变 P0"，一并列入优先修复。

---

## 4. P1 缺陷精选（按主题）

完整 162 条见模块报告；这里按"修起来收益最大"排序，每组只列关键项。

### 4.1 状态同步与交互逻辑

- **颜色三份副本、链路断裂**：`LeftToolBar::m_fgColor` / `ColorTab::m_foreground` / 画笔 `m_paintColor` 各存一份；`mainwindow.cpp:3407` 连到 `RightPanelDock::colorDock()`（返回 `nullptr` 的 deprecated shim）→ 工具栏取色完全无效（09-01/04-14）。修复：单一真源 + 广播。
- **dock attach 不 detach**：切 tab 反复 `attach()`，每次多一组 connect → 点一次工具触发 N 次切换（APP-05）。
- **12 个单键快捷键是 `Qt::ApplicationShortcut`**：文字工具里打 V/M/L/W/C/T/B/I 会切工具而不是输入文字（04-08）。
- **工具键盘链路断裂**：`ToolContext::onKeyPress` 零调用者、`ImageCanvas::keyPressed` 无订阅者 → 多边形/钢笔收不到 Enter，Shift 等比失效（04-07）。
- **调整对话框无实时预览**：`AdjustDialogBase::preview` 信号零消费者（09-02）。
- **曲线/色阶分通道无效**：切通道只改字符串，控制点不分通道；色阶 channel 被忽略（09-04/06-04）。
- **菜单无 enable 状态管理**：主页/空栈时 Undo/Save/图层/滤镜全可点（APP-07）。
- **`m_mode` 是 `windowState()` 的影子**：系统级最大化/还原后状态失联，最大化记忆永远不生效（APP-12/APP-24）。

### 4.2 工具与绘画

- 裁剪工具不裁剪（`applyCrop` 不 `setCurrentImage`，推送的命令 redo 恒 no-op）（04-04）。
- 画笔选项面板不可达（Brush 被 Mosaic 分支抢走），画笔锁死 13px 黑笔；9 个共享参数只有 4 个进落笔路径（04-05）。
- 吸管只写状态栏，`sampledColor()` 零消费者；非 3 通道图 `Vec3b` 越界（04-13）。
- 修复画笔每次 mousemove 对整图 `cv::inpaint` → 24MP 假死（04-11）。
- 仿制图章源点用增量算偏移 → 反复印同一小块（04-12）。
- 选区工具改动不可撤销；蚂蚁线只画 bbox 矩形；有选区时绘画不裁剪到选区（06-11/13，04-16）。
- 13 种 BrushKind 只是标签，引擎无 kind 分支；压感恒 1.0；Multiply/Screen 三通道全用蓝色分量；无 per-stroke 累积缓冲（06 报告 §1.1）。
- `TransformTool` 的 `if (!sel && !sel->isEmpty())` 条件恒假 → Ctrl+T 初始框永远 800×600（04-09）。
- Shape/Pen 的矢量层被按 `sz/100` 缩放 → 图形画到画布外看不见（04-10）。

### 4.3 渲染与滤镜

- 增量合成完全不增量：每次 render 重建 DAG + 新缓存，命中率 0，比 legacy 慢 2–3 倍（CP-03）。
- GPU 加速是装饰品：三态开关只改状态栏文本，`RenderGraph::setBackend` 零调用；`BackendCapabilities` provider 恒 false；readback 上下翻转（08 报告）。
- 滤镜预览注册表是文件作用域 static，`~ImageWindow` 不清理 → 关文档后永久泄漏 + 地址复用串预览（09-09/CP-14）。
- 渐变映射用未初始化 LUT → 随机噪点（CP-13）。
- 液化：6 个工具只有 1 个可达、面板参数被 `Q_UNUSED`、ONNX 路径写死、结果红蓝互换（CP-12/08 报告）。
- 20+ 个滤镜是 alias/简化（`Twirl/Polar/Ripple/...` 仅注册 enum；`Mezzotint/Fragment` 简化；`GradientMap` 未初始化）（08 §3）。

### 4.4 选区 / 蒙版 / 变换

- `grow/shrink/smooth` 的 `memcpy` 忽略 Alpha8 stride → 宽度非 4 倍数必错位（06-06）。
- 羽化后重新二值化 → 边缘仍是硬边；O(W·H·r) 三重循环大半径卡死（06-07）。
- 魔棒只用亮度容差（容差 0 会选中所有同亮度异色像素）；逐像素 `QImage::pixel()` 极慢（06-08/09）。
- 套索羽化的积分图 `std::vector<int>` 在 >8.4MP 溢出（06-10，APP-17 同源）。
- ColorRange 每像素一次 1×1 `cvtColor` → 24MP 冻结数分钟（06-15）。
- RefineEdge 的 Shift 在二值蒙版上等于"全选/蒙版损坏"（06-16）。
- 变换框不认 Distort 角点、拖 Center 抹掉 4 角、矩阵不复合（06-17/18/19）。
- 蒙版全链路失效：UI/蒙版画笔写新 `LayerMask`，渲染只读旧 `layerMask/maskEnabled`，`LayerMaskRenderer` 零调用（LYR-15/CP-18）。

### 4.5 图层面板与模型

- LayerPanel 无任何拖放（无 `setDragDropMode`/drop 事件）→ 不能跨组/入组/出组；组子项只读（LYR-25/31）。
- 图层不透明度/显隐可改但 undo 后不刷新；隐藏 base 仍当画布（LYR-13/14）。
- `m_layers` 用 `LayerPtr` 地址/下标做身份，复制/删除/重排后各种错位（LYR-06/12/16）。
- 智能对象：缓存路径 `toStdString` 非 ASCII 失败；外部修改监视、重链接可用但无序列化（LYR-11/23）。

### 4.6 外壳 / 会话 / 生命周期

- 启动先 `setLastExitClean(false)` 再读 → 每次启动都误报"异常退出"；点"否"还会清空会话（APP-03）。
- 崩溃恢复选"是"后主窗口永不显示（APP-04）。
- 会话只存路径、只在干净退出时存、无 autosave → 崩溃后未保存修图全丢（APP-16/CI-07）。
- 无单实例（APP-31/CI-07）：多开互相覆盖 session/settings。
- `onResetLayout` 读一个从不存在的 `workspace_default.ini`（APP-19）。
- splash 已打进 qrc 却从磁盘读；3s 定时 vs 5s 进度条不一致（APP-28）。
- `qDebug` 同步写死 `D:/vistella_qdebug.log` 并 flush（APP-32）。
- `SetUnhandledExceptionFilter` 返回非法值 `EXCEPTION_UNWIND`，等于放弃 crash dump（APP-30）。
- 无边框窗口不能拖边 resize、无贴靠/系统菜单（APP-13）。

### 4.7 基础设施 / 数据层

- 日志：block 溢出会卡 UI、中文路径直接无日志、切分重复行（03 报告）。
- `Result.h` 的 `Q_TRY` 在 MSVC 上无法编译；`Async.h`/`Result.h` 基本无人使用（03）。
- `SerialExecutor::run_sync` 在 submit 被拒/任务异常时无限自旋；`EngineContext::gpu()/stats_gpu()` 在禁用 GPU 线程时解引用空 unique_ptr（CI-04/05/09）。
- UserDatabase 明文存 access/refresh token；`UserManager` 密码参数被 `Q_UNUSED`；AuthClient 对任意输入返回 mock 成功（03）。
- `src/network` 连 `Qt6::Network` 都没链接（03）。

---

## 5. 功能完整度 vs Photoshop

> 判定：`无`（已实现且行为接近 PS） / `有但错`（有实现但行为不对） / `部分`（子集可用） / `缺失`
> 结论：**"像 PS"的最大障碍不是缺功能，而是"已声明的功能里约一半行为不对"**。下表加粗项为对"大体像 PS"影响最大的。

### 5.1 界面与外壳

| 功能项 | PS 行为 | 现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 顶级菜单 | 文件/编辑/图像/图层/文字/选择/滤镜/3D/视图/窗口/帮助 | 文件/编辑/视图/设置/增效工具/窗口/**帮助**/图像/图层/文字/选择/滤镜（12 个，顺序错） | 有但错 | P1 |
| 编辑菜单 | 还原/重做/切换/剪切/复制/粘贴/填充/描边/自由变换/变换/定义画笔/首选项 | 只有 还原/重做/自由变换/剪切/复制/粘贴/全选/查找/替换；图像文档剪贴板无效 | 部分 | P1 |
| 图像菜单 | 模式/调整/自动色调/图像大小/画布大小/旋转/裁剪/裁切/显示全部 | 有大小/格式/裁剪/翻转/旋转/模式(RGB/灰度)/画布/裁切/显示全部，**无"调整"子菜单** | 部分 | P1 |
| 图层菜单 | 新建/复制/删除/图层样式/新建调整层/蒙版/编组/排列/合并/拼合/修边 | 新建/复制/删除/上下移/合并可见(实为向下)/拼合/蒙版 8 项/智能对象 8 项；**无图层样式/调整层/编组菜单/盖印/修边** | 部分 | P1 |
| 选择菜单 | 全部/取消/反选/色彩范围/羽化/修改(边界/平滑/扩展/收缩)/存储选区/载入选区 | 全部/取消/反选/羽化/色彩范围；**无修改/存储/载入(有内部保存但只存矩形)** | 部分 | P1 |
| 滤镜菜单 | 13 类 100+ 滤镜 + 滤镜库 + 智能滤镜 | 48 个 enum（多数 alias/简化/占位）+ 液化 + CameraRaw 占位 + 智能滤镜；无滤镜库 | 部分 | P2 |
| 视图菜单 | 标尺/参考线/网格/对齐/屏幕模式/放大缩小/适合屏幕/实际像素/多视图 | 放大/缩小/重置布局/适合屏幕；无标尺/参考线/网格/屏幕模式(不可用)/多视图 | 部分 | P1 |
| 窗口菜单 | 排列/工作区/所有面板开关 | 文档列表/工作区 4 种/平铺堆叠(禁用)/全部最小化 | 部分 | P2 |
| 工具箱 | 70+ 工具，按组收纳，长按弹出兄弟工具 | 15 个扁平按钮，**无工具组** | 缺失 | P1 |
| 选项栏 | 随工具切换完整参数 | 有 ImageOptionBar，但画笔页不可达、多数参数装饰 | 部分 | P1 |
| 面板停靠 | 自由停靠/浮动/分组/折叠/保存工作区 | 自研 VDraggableTabWidget，**拖拽 0% 可用**、浮动不浮、无布局持久化 | 有但错 | P1 |
| 状态栏 | 缩放/尺寸/颜色/内存/进度 | 有 GPU/CPU 按钮 + 消息区；无缩放百分比/文档尺寸 | 部分 | P2 |
| 多文档 | 独立窗口 + 标签组 + 拖出成新窗口 | QTabWidget（pin/关闭/右键）；tab 不可拖出 | 部分 | P2 |
| 快捷键 | 完整 + 可自定义 | 部分；12 个单键全局快捷键抢文字输入；Ctrl+A 冲突失效 | 有但错 | P1 |
| 工作区/主页 | 主页 + 工作区切换 | 有 HomePage + 4 工作区（Mediator 定义），主页 4 模块按钮是空壳 | 部分 | P2 |
| 暗色主题 | 全局一致 | QSS 有明暗主题，但 inline stylesheet 80+ 处不刷新、选中 tab 白底白字 | 有但错 | P1 |

### 5.2 文件、格式与持久化

| 功能项 | PS 行为 | 现状 | 差距 | 优先级 |
|---|---|---|---|---|
| **工程/分层格式** | PSD/PSB 完整往返（图层/蒙版/智能对象/文字/调整层） | **完全没有**。保存只写拍平位图；重开丢失一切非破坏数据 | 缺失 | **P0** |
| 打开格式 | PSD/PSB/PDF/AI/EPS/RAW/HEIF/JXL/… | PNG/JPEG/BMP/WebP/TIFF + RAW(LibRaw 可选)；无 PSD/HEIF/JXL/PDF | 部分 | P2 |
| 保存格式 | PSD/PSB/PSD 大文档/PDF/TIFF/PNG/JPEG/… | PNG/JPEG/BMP（另存）；导出 PNG/JPEG/TIFF/WebP/BMP/GIF(可能失败) | 部分 | P1 |
| 自动保存/恢复 | 自动恢复 + 崩溃恢复 | 无 autosave；session 只存路径且只在干净退出时存 | 缺失 | **P0** |
| 最近文件 | 列表 + pin + 缩略图 | RecentManager + model；打开已 pin 的文件会掉 pin | 有但错 | P2 |
| 导出 | 单窗口、预览、元数据、ICC、批量 | ExportDialog + 二次 QFileDialog；无预览/元数据；ICC 仅部分格式 | 部分 | P2 |
| 打印 | 完整打印/打印预览/色彩管理 | 无 | 缺失 | P3 |
| Camera Raw | ACR 完整 | LibRaw 封装 + 对话框；**onOpen 无调用者 → 链路死代码** | 有但错 | P2 |
| 批处理 | 动作/批处理 | 无 | 缺失 | P3 |

### 5.3 工具（16 vs PS 70+）

已有：移动(V)、矩形选框(M)、套索(L)、魔棒(W)、裁剪(C)、文字(T)、画笔(B)、吸管(I)、形状(U)、钢笔(P)、仿制图章(S)、修复画笔(J)、修补、红眼、蒙版画笔。
其中**真正可用**的不到一半（裁剪不裁剪、移动空壳、吸管不吸色、画笔参数锁死、形状画到画布外、多边形收不到 Enter、选区不可撤销）。

完全缺失（按优先级）：
1. **橡皮擦组**（橡皮擦/背景橡皮擦/魔术橡皮擦）
2. **渐变 + 油漆桶**
3. **加深/减淡/海绵**
4. **模糊/锐化/涂抹**
5. **图案图章**
6. **历史记录画笔 / 艺术历史记录画笔**
7. **抓手 / 旋转视图 / 缩放工具**（现在 ScrollHandDrag 常开，任何工具左键都平移画布）
8. 透视裁剪、内容识别移动、颜色替换
9. 标尺/计数/注释/切片/框架
10. **工具组（长按弹出）——整类交互缺失**

### 5.4 选区与蒙版

| 功能项 | PS | 现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 选区形状 | 矩形/椭圆/单行单列/套索/多边形/磁性/快速选择/对象选择 | 矩形/套索/魔棒 | 部分 | P1 |
| 选区运算 | 新/加/减/交（工具级+选项栏） | 4 模式在 model 层实现正确，但 RectSelect 模式下拉未 connect，切工具即丢 | 有但错 | P1 |
| 羽化/抗锯齿 | 8-bit 软蒙版 | 二进制 0/255 蒙版；羽化后重新二值化（硬边） | 缺失 | P1 |
| 修改 | 边界/平滑/扩展/收缩/羽化 | API 存在，UI 入口缺失 | 部分 | P2 |
| 蚂蚁线 | 沿轮廓 | 只画 bbox 矩形 | 有但错 | P1 |
| 存储/载入选区 | 通道 | 内部存 mask，但只存矩形；通道面板是静态假数据 | 有但错 | P1 |
| 选区裁剪绘画 | 绘画受选区限制 | 不裁剪 | 缺失 | P1 |
| 颜色范围/边缘调整 | 完整 | 有算法，ColorRange 极慢、RefineEdge 会毁蒙版 | 有但错 | P1 |
| 蒙版（像素/矢量/密度/羽化/反相） | 全链路 | 新 LayerMask 体系**完全不参与渲染**，只读旧字段（蒙版乘黑近似） | 有但错 | **P0** |

### 5.5 图层

| 功能项 | PS | 现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 图层类型 | 像素/文字/形状/智能对象/调整层/填充层/组 | 5 种 enum，文字/矢量/调整层渲染有 P0 缺陷 | 有但错 | P0 |
| 混合模式 | 27 + Pass Through | 15 种（12 基础 + 4 HSL），缺 Dissolve/Linear Burn/Darker Color/Linear Dodge/Lighter Color/Vivid Light/Linear Light/Pin Light/Hard Mix/Subtract/Divide/Pass Through | 部分 | P1 |
| 不透明度/填充 | 双参数 | 不透明度可改但 undo 不刷新 | 有但错 | P1 |
| 图层组/嵌套 | 无限嵌套 | 有 GroupNode，但编组按钮会把 base 吞进去导致画布退化 | 有但错 | P0 |
| 图层蒙版 | 像素+矢量+密度+羽化+反相 | 数据有、渲染不认 | 有但错 | P0 |
| 调整层 | 非破坏 15+ 种 | 无（调整直接改像素） | 缺失 | P1 |
| 图层样式 | 投影/内阴影/外发光/描边/斜面浮雕… | 无 | 缺失 | P2 |
| 混合/盖印/合并/拼合 | 全部可撤销 | 合并会崩/undo 丢层；拼合不可撤销；无盖印 | 有但错 | P0 |
| 图层锁定/颜色标记/搜索/多选 | 有 | 无 | 缺失 | P2 |
| 智能对象 | 完整 + 智能滤镜 | 有转换/栅格化/重链接/非破坏变换/智能滤镜；但中文路径失败、无序列化、GPU 路径死 | 部分 | P2 |
| 图层树拖放 | 跨组/入组/出组/排序 | **完全没有拖放** | 缺失 | P1 |
| Alpha/透明 | 每层 alpha | 全链路 8UC3，无 alpha | 缺失 | P0 |

### 5.6 文字 / 变换 / 调整 / 滤镜 / 色彩

| 功能项 | PS | 现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 文字 | 点/段/路径文字、字符/段落面板、变形、样式 | 场景 item + 8 手柄 + 字体/字号/颜色/粗斜体；**旋转绕错圆心会漂移**；保存即烧图 | 部分 | P1 |
| 自由变换 | 缩放/旋转/斜切/透视/变形/变形网格 + 选区参与 | 8 手柄 + Distort；矩阵转置错、box 不更新、无实时预览、选区不参与 | 有但错 | P0/P1 |
| 图像调整 | 20+ 种 | 亮度/对比度/曝光/饱和度/色相/灰度/反相/阈值/锐化/模糊 + 曲线/色阶/HSL/B&W/通道混合器；**HSL/B&W/通道混合器 LUT 退化** | 部分 | P1 |
| 调整层/非破坏 | 全部非破坏 | 无；对话框无实时预览；曲线不分通道 | 缺失 | P1 |
| 滤镜 | 100+ | 48 enum（约 1/3 alias/简化/占位）+ 液化 + 液化 6 工具仅 1 个可达 | 部分 | P2 |
| 色彩管理 | 指定/转换/软打样/溢色警告/CMYK/16bit | 仅导出时嵌 ICC；打开不读 profile、显示不转换、保存不嵌；无软打样 | 缺失 | P2 |
| 16/32bit | 完整 | 16bit 打开全白（量程 bug）；全链路 8U | 有但错 | P1 |
| 历史记录 | 快照/非线性/可拖动 | QUndoStack 镜像列表（高度限 200px），无快照 | 部分 | P2 |
| 动作/脚本/插件 | 动作录制、脚本、插件市场 | 全无（QJSEngine/QPluginLoader 可做） | 缺失 | P3 |
| 3D/视频时间轴 | 3D 图层、视频 | 明确移除（合理取舍） | 不适用 | — |

### 5.7 缺失功能对"像 PS"的影响排序

1. **分层保存格式 + 自动保存** —— 没有它，"非破坏修图软件"这个定位不成立。
2. **alpha/软蒙版全链路** —— 没有它，图层/蒙版/文字/羽化全都是错的。
3. **图层撤销体系** —— 没有它，任何编辑都不敢做。
4. **工具组 + 缺失的 8 类工具** —— 决定"工具箱看起来像不像 PS"。
5. **面板拖拽/浮动/工作区** —— 决定"工作区像不像 PS"。
6. **调整子菜单 + 调整层 + 实时预览** —— 决定"调色流程像不像 PS"。
7. 选区体系（椭圆/多边形/磁性/快速选择/修改/存储） —— 决定"选区用起来像不像 PS"。
8. 文字排版/图层样式/动作 —— 专业用户才会感知。

---

## 6. Qt 能力边界分析

> **方法**：对每个"做不了/很难/现在很别扭"的点，先问三个问题——① Qt 是否已有对应能力（QPainter composition、QPainterPath、QTextLayout、QGraphicsView、QOpenGLContext/QRhi、QPrinter、QFileSystemModel、QColorSpace、QTabletEvent、QSaveFile、QLockFile…）？② 能否用自研算法绕过？③ 能否引入成熟第三方库？只有三者都不行才判 `QT-LIMIT`。
> **结论先说：不存在"必须换框架"的理由。** 真正的 Qt/平台硬限制只有 8–10 项，且都不阻碍"大体像 PS"；被判定为 `QT-NATIVE` 的项目才是重点——**Qt 本来就支持，本项目却自己写了错的**。

### 6.1 `QT-LIMIT` —— Qt/平台真的做不到或只能近似（10 项）

| # | 需求点 | 为什么受限 | 可接受的替代 | 影响 |
|---|---|---|---|---|
| 1 | 全局热键（软件在后台也响应） | Qt 只有应用级 `QShortcut`/`QAction`，无全局热键 API | Win32 `RegisterHotKey`（约 100 行封装） | 小 |
| 2 | 网络盘/UNC 的可靠递归文件监听 | Windows `ReadDirectoryChangesW` 对网络盘不可靠、缓冲区会溢出；Qt 的 `QFileSystemWatcher` 只是薄封装 | 只监听"已展开目录"+ 定时轮询 mtime + 手动刷新按钮；UI 明示 | 小（仅文件树） |
| 3 | Win11 贴靠布局悬停面板（Snap Layouts） | 需要 `WM_NCHITTEST` 返回 `HTMAXBUTTON` 并自绘悬停，Qt 无公开 API | `QWindow::startSystemMove/startSystemResize` 可拿到原生拖动/贴边；悬停面板只能近似 | 小 |
| 4 | 无边框窗口的完整原生行为（DWM 阴影/圆角/系统菜单/多屏 DPI 切换瞬间） | `FramelessWindowHint` 丢掉 `WS_THICKFRAME`/`WS_CAPTION`，这些行为由系统窗口管理器提供 | `nativeEvent` + DWM API 补阴影/圆角/命中测试；或用 `Qt::ExpandedClientAreaHint`（6.5+） | 中（当前项目完全没做，连 resize 都不行） |
| 5 | TWAIN/WIA 扫描仪/相机导入 | Qt 无扫描仪 API | 平台 SDK（WIA/TWAIN）或第三方 | 小 |
| 6 | 单张 QPixmap 承载超大画布（>平台纹理/图像尺寸上限） | 平台/GPU 纹理尺寸有上限，`QGraphicsView` 单 pixmap 模型不提供瓦片 | 自研瓦片画布（项目已有 TileGrid/TileCache 骨架） | 中（大图必需） |
| 7 | 硬实时/低延迟笔输入（不被 UI 线程阻塞） | 笔事件仍进 Qt 主事件循环；主线程一忙就丢/合并事件 | 平台 raw input + 独立采样线程；或把重活全部移出主线程 | 中（专业绘画场景） |
| 8 | 抢占式取消正在执行的 `QFuture`/QtConcurrent 任务 | Qt 无法安全杀死线程 | 代次 token 丢弃结果（项目已用 `m_currentTaskId` 模式，是对的） | 小 |
| 9 | PS 级文字排版（OpenType 全特性/避头尾/文字变形/路径文字交互编辑） | `QTextLayout` 不是完整排版引擎；`QFont::setFeature`（6.7+）只覆盖部分特性 | 接受 90% 效果；复杂变形用 `QPainterPath` 轮廓近似 | 中（专业排版场景） |
| 10 | PSD/PSB 完整往返（图层/智能对象/调整层/文字引擎私有数据） | 格式封闭且高度复杂，Qt 无支持，开源库只能读部分 | `psd_sdk` 只读 + 自研工程格式（推荐）；不承诺 PSD 写 | 中 |

> 另注：`QGraphicsView` 在 Qt 6.11 存在杂散 mouse-release assert（项目 `ImageCanvas.h:124-144` 已有 workaround），属版本行为差异，不算硬限制。

### 6.2 `QT-NATIVE` —— Qt 原生支持，本项目没用对/没用（**最该修的一类，22 项**）

| # | 需求点 | Qt 现成能力 | 项目现状 | 收益 |
|---|---|---|---|---|
| 1 | 透明/Alpha 合成 | `QImage::Format_ARGB32_Premultiplied` + `QPainter` 天然逐像素 alpha；OpenCV `CV_8UC4` 同样支持 | 全链路 CV_8UC3，蒙版降级为"乘黑" | 修掉一半渲染错误 |
| 2 | 混合模式 | `QPainter::CompositionMode` 自带 12 种（含 Multiply/Screen/Overlay/SoftLight/ColorDodge/ColorBurn/HardLight/Difference/Exclusion…） | 自研 15 种（部分正确），且 3 种退化 | 快速补齐基础混合 |
| 3 | 图层树拖放 | `QTreeWidget::setDragDropMode(InternalMove)` / `QAbstractItemModel::moveRows` + `dropMimeData` | **完全没有拖放** | 图层交互立刻可用 |
| 4 | dock 停靠/浮动/分组/布局持久化 | `QMainWindow` + `QDockWidget`：`addDockWidget`/`splitDockWidget`/`tabifyDockWidget`/`setFloating`/`saveState`/`restoreState` | 自研 VDraggableTabWidget，拖拽失效、浮动不浮、无持久化 | 删掉约 1,500 行自研代码，换来 PS 80% 面板行为 |
| 5 | 文件树 | `QFileSystemModel`（后台扫描 + fetchMore 增量 + watcher + 图标） | 自研同步 model：双全扫 + 逐项 stat 卡 UI | 大目录不再卡 |
| 6 | 无边框拖动/resize | `QWindow::startSystemMove()` / `startSystemResize(Qt::Edges)`（6.0+）；`nativeEvent` + `WM_NCHITTEST` | 自绘拖动能行，**resize 完全没做** | 窗口可用性 |
| 7 | 单实例 | `QLockFile` + `QLocalServer`/`QLocalSocket` | 无（多开互相覆盖 session） | 修数据丢失 |
| 8 | 原子写文件 | `QSaveFile` | `QFile` 直写（DocWindow 还 `Truncate`，崩溃即损坏） | 防损坏 |
| 9 | ICC 解析/转换 | `QColorSpace`（Qt 5.14+）：`fromIccProfile`、`convertToColorSpace`、`QImage::setColorSpace` | 引入 lcms2 只做"导出嵌 profile" | 色彩管理起步 |
| 10 | 压感/倾斜/旋转 | `QTabletEvent`（pressure/tangentialPressure/rotation/tilt；Windows 走 WM_POINTER/WinTab） | 未转发，pressure 恒 1.0 | 画笔手感 |
| 11 | 蚂蚁线轮廓 | `QRegion(QBitmap)` 或 `QPainterPath`（marching squares） | 只画 bbox 矩形 | 选区可视化 |
| 12 | 通道格式 | `QImage::Format_Grayscale8`/`RGBA8888`/`RGBA64`/`RGBA_F16`；OpenCV 全类型 | 4 通道配 `Format_RGB888`（错）；无通道契约 | 灰度/alpha/16bit |
| 13 | 撤销栈状态/上限 | `QUndoStack::canUndoChanged/canRedoChanged/undoLimit/cleanChanged` | 未接 enable 状态；按条数限制导致 GB 级内存 | 菜单与内存 |
| 14 | 撤销/重做菜单联动 | `QAction::setEnabled` + 上述信号 | 无动态 enable | UI 正确性 |
| 15 | 语言切换 | `QTranslator` + `QEvent::LanguageChange` + `uic` 的 `retranslateUi` | 双 translator、`retranslateUiTexts()` 空实现、.ts 839 条未翻译 | i18n 真正可用 |
| 16 | 打印/PDF | `QPrinter`/`QPrintDialog`/`QPdfWriter` | 无 | 输出能力 |
| 17 | 脚本 | `QJSEngine` | 无（PS 用 ExtendScript） | 动作/脚本 |
| 18 | 插件 | `QPluginLoader` + 接口 | 无（菜单是灰的） | 扩展性 |
| 19 | 屏幕取色/截图 | `QScreen::grabWindow` | AppBar 截图已用；取色未接 | 工具补齐 |
| 20 | 拖放打开 | `dragEnterEvent` + `QMimeData::urls()` | 无 | 易用性 |
| 21 | 动画 | `QVariantAnimation`/`QPropertyAnimation` | 无 | 观感 |
| 22 | OpenType 特性/可变字体 | `QFont::setFeature`/`setVariableAxis`（6.7+） | 未用 | 排版 |

### 6.3 `QT-3RD` —— 需要第三方库（含许可证与代价）

| 需求 | 推荐库 | 许可证 | 集成代价 | 优先级 |
|---|---|---|---|---|
| 线程池/DAG/并行 | **oneTBB** 或 **Taskflow** | Apache-2.0 / MIT | 低（替换自研 Chase-Lev） | P0（线程池有 bug） |
| PSD 只读 | **psd_sdk** | MIT | 中（图层/蒙版/文字解析） | P2 |
| HEIF/AVIF | **libheif**（+libde265/aom） | LGPL | 中（插件或直接解码） | P2 |
| JPEG XL | **libjxl** | BSD-3 | 中 | P3 |
| GIF 写（动画） | **giflib** + **libimagequant** | MIT / GPL(后者需评估) | 低 | P2 |
| EXIF/XMP/IPTC | **exiv2**（GPL/商用双许可，注意传染）或 **tinyexif**（MIT，只读） | 见左 | 中 | P2 |
| 神经滤镜/人脸关键点 | **onnxruntime** | MIT | 中（模型需另行授权） | P3 |
| 高级图像算法（超分/去噪/边缘） | **opencv_contrib (ximgproc)** | Apache-2.0 | 低（已用 OpenCV） | P2 |
| 2D 渲染加速 | **Skia** / **Blend2D** | BSD / Zlib | 高（替换 QPainter 管线） | P3 |
| 颜色拾取控件 | **QtColorWidgets** | MIT | 低 | P2 |
| 画笔引擎参考 | **libmypaint**（GPL，传染）/ Krita 的 KisBrush（LGPL 部分） | 注意许可 | 高（许可风险） | P3（建议自研） |
| 崩溃转储 | **Crashpad** | BSD | 中 | P2 |
| 色彩管理 | **lcms2**（已引入） | MIT | 已有 | P1（补全读写/软打样） |

### 6.4 `QT-EFFORT` —— Qt 能做，但需要自研算法/大量工作

| 需求 | 工作量估计 | 说明 |
|---|---|---|
| 分层工程文件格式（自定义 .vst / PSD 式） | 5–8 人天 | 设计文档模型 + 序列化 + 版本迁移；比"接 PSD 写"现实得多 |
| alpha/软蒙版全链路改造 | 5–10 人天 | `CV_8UC3` → `CV_8UC4`/独立 alpha + 所有合成/滤镜/画笔/显示适配 |
| 瓦片增量渲染（只重绘脏区） | 10–15 人天 | TileGrid/TileCache 骨架已有，需真接线 + 缓存 key 正确 + 内存上限 |
| 画笔引擎重写（kind/dynamics/累积缓冲/压感/纹理） | 10–15 人天 | 13 kind 需要真实分支；per-stroke buffer；GPU 可选 |
| 调整层（非破坏 LUT 图层） | 5–8 人天 | 依赖 alpha 与合成器 |
| 图层样式栈（投影/发光/描边/浮雕） | 15–25 人天 | 每种样式一套参数 + 渲染 |
| 液化实时预览 | 5–8 人天 | 网格变形 + 增量重绘 |
| 内容识别填充（PatchMatch/inpaint） | 10–15 人天 | OpenCV inpaint 可做 v1 |
| 16bit/32F 全链路 | 10–15 人天 | 通道契约 + OpenCV 类型 + 显示转换 |
| 色彩管理全链路（工作空间/软打样/CMYK） | 10–15 人天 | lcms2/QColorSpace + 文档色彩空间 |
| 高级文字排版/变形/路径文字 | 10–20 人天 | 与 PS 仍有差距 |
| 动作录制 + 脚本 API | 10–15 人天 | QJSEngine + 命令总线 |
| 视频时间轴/编码 | 20+ 人天 | Qt Multimedia/FFmpeg |

### 6.5 边界结论（直接回答你的问题）

1. **"Qt 本身局限性无法完成"的**（保守判定，共 10 项）：全局热键、网络盘可靠递归监听、Win11 贴靠悬停面板、无边框窗口的全部原生行为、扫描仪导入、超大画布单 pixmap、硬实时笔输入、QFuture 抢占式取消、PS 级文字排版、PSD 完整往返。**这些都不阻碍"大体像 PS"**，且多数有工程替代方案。
2. **"Qt 本身就能实现、只是没做对"的**：Alpha 合成、12 种混合模式、图层树拖放、QDockWidget 面板体系、QFileSystemModel 文件树、无边框 resize、单实例、原子写、QColorSpace、QTabletEvent 压感、蚂蚁线轮廓、正确的通道格式、撤销栈联动、i18n、打印/PDF、脚本/插件、拖放打开……**共 22 项，是本项目最大的"自废武功"**。
3. **"引入第三方库就能完美实现"的**：线程池（oneTBB）、PSD 读（psd_sdk）、HEIF/JXL、GIF 写、EXIF、ONNX 推理、高级图像算法（opencv_contrib）、崩溃转储。**许可证以 MIT/BSD/Apache 为主，均可商用**（exiv2/ImageMagick 类 GPL 库需避开或商用授权）。
4. **"能做但费工"的**：工程格式、alpha 全链路、瓦片增量渲染、画笔引擎、图层样式、液化、内容识别、16bit、色彩管理、高级排版、脚本。**这些才是真正决定"像不像 PS"的部分**，合计约 3–5 人月（不含视频/3D）。

---

---

## 7. 架构与设计问题（根因层）

> 这些不是零散 bug，而是"为什么会持续产出 bug"的结构原因。按影响排序。

### 7.1 `m_current` 一变量两角色（数据损坏总根源）

`ImageWindow::m_current`（`cv::Mat`）同时是"图层合成缓存"和"可原地编辑的画布"：
- 画笔/马赛克/克隆/滤镜/调整直接写它（视为画布）；
- `LayerStack::render()` 会在任何 `layerChanged` 后覆盖它（视为缓存）；
- 保存时直接写它（视为唯一真相）。

结果：04-02（笔画被图层操作擦掉）、CP-11（滤镜写回 base 层导致重复合成）、05-02（保存把合成图写回 base 层）、05-03（保存写过期缓存）全部由此而来。
**建议**：确立"`LayerStack` 是唯一像素真相，`m_current` 只是只读渲染产物"；所有编辑走"命令 → 修改目标图层 → 触发重合成"；显示层永远不参与写。

### 7.2 没有稳定的图层/文档身份系统

现状混用四种身份：`LayerPtr`（对象地址）、`int` 下标、`cv::Mat::data` 像素指针、`SmartFilter` 链的序号。任何复制/删除/重排/合并后都可能指向错误对象（LYR-06/12/16、UI-01）。
**建议**：引入 `LayerId`（单调递增 64 位）+ `DocumentId`；命令只存 id；`QAbstractItemModel` 用 id 做 `internalPointer`；`m_groups` 用 id 做 key。

### 7.3 撤销架构四分五裂

现存 4 种命令风格：`LayerCommand`（多 Op 混一个类，redo 多为 no-op）、`ImageEditCommand`（整图快照，redo 对图像路径 no-op）、`TransformCommand`、`SelectionCommand`；外加 `LiquifyCommand`、`MaskBrushCommand`、`TextItemCommand`。
后果：LYR-07/08（撤销重做失效）、05-09（24MP 每笔 144MB）、04-16（选区不可撤销）、06-04（调整 redo 断链）。
**建议**：统一 `ICommand` 规范——① 命令只持 id + before/after 最小差异；② redo **必须**幂等重放；③ 统一通过 `Document::notifyChanged()` 收尾（emit + invalidate + repaint）；④ `QUndoStack` 子类按字节估算裁剪（大图快照按 ROI 存）。

### 7.4 双渲染路径 + 无回退开关

legacy `applyBlend` 与 P8 `CompositorGraph` 并存；`LayerStackAdapter` 刻意"镜像" legacy 的 bug（隐藏 base 仍当画布、base 之前图层丢弃、尺寸不一致处理不同）；`m_compositorEnabled` 默认 `true` 且**全项目无人调用 `setCompositorEnabled`**，注释却写"Default is OFF"。
**建议（P0 级）**：① 立刻加回退开关（设置页/环境变量/命令行）并默认保守；② 修 `RenderGraph` 通道归一化；③ 用 parity 测试锁定两路径行为；④ 长期只保留一条路径。

### 7.5 线程模型失控

- `main.cpp` 创建一个全局 `EngineContext`（5 池），但 `current()` 零调用 → 空转；
- 每个 `ImageWindow` 又创建自己的 `EngineContext`（`Profile::ImageEditor`，5 池）→ 8 核机器启动 40+ 线程；
- 三套线程设施（ThreadPool/SerialExecutor/RealtimeThread）+ 大量未接线设施（parallel_for/task_graph/取消树/SpscRing）；
- 取消协议靠 `atomic taskId`，但**结果回调的 receiver 用 `qApp` + 裸 this**（CI-03/05-04）→ UAF。
**建议**：单全局 `EngineContext`；文档只提交任务并持 generation token；所有回调 `invokeMethod(document, ...)`；删除未接线设施（或明确标注 `QT-UNUSED`）。

### 7.6 状态副本遍地（同步靠 80+ 个手工 connect）

| 状态 | 副本数 | 现状 |
|---|---|---|
| 前景/背景色 | 3（LeftToolBar / ColorTab / 画笔） | 链路断裂（09-01） |
| 工具配置 | 3（ToolMediator config / ToolState / 面板控件） | 切工具即丢（04-20） |
| 面板显隐 | 2（WorkspaceMediator / QDockWidget） | 易失步 |
| Quick Mask | 3（AppBar / ChannelPathPanel / ImageWindow） | 三向同步靠信号 |
| 选中的图层 | 2（LayerStack::selection / LayerPanel::currentRow） | 面板与模型双向同步手工维护 |
| 主题 | 2（ThemeManager / QSS + inline stylesheet 80+） | 暗色主题半黑半白（APP-14） |
| 窗口模式 | 2（m_mode / windowState()） | 系统操作后失联（APP-12） |
**建议**：每个状态找唯一 owner（通常是 Document / ApplicationState），其余全部是 view；用"属性变化 → 广播 → view 重读"的单向流，禁止 view 之间点对点 connect。

### 7.7 上帝类与职责膨胀

| 类 | 行数 | 症状 |
|---|---|---|
| `MainWindow` | 3,518 | 外壳 + 菜单 + tab + session + 直接操作 `cv::Mat`/`LayerStack`/滤镜/液化；单函数 850 行；30 处重复 `activeImageWindow()` 模板 |
| `ImageWindow` | 3,055 | 文档 + 视图 + 交互 + 面板 + IO + 撤销 + 异步；6 个 `friend class` 直接改私有状态 |
| `LayerStack` | 1,678 | 模型 + 渲染 + 序列化 + SmartObject 缓存 |
| `LayerPanel` | 1,095 | 面板 + 缩略图 + 拖拽 + 上下文菜单 |
| `AdjustmentPanel` | 955 | 5 个 tab + LUT + 异步提交 + 撤销 debounce |
**建议**：`MainWindow` 拆 6 个类（TitleBar/AppMenu/TabManager/SessionController/CommandRouter/DockHost，报告 01 给了行数估算）；`ImageWindow` 拆 `ImageDocument`（数据+命令）/`CanvasView`/`DockHost`/`ImageIO`；`LayerStack` 拆模型/渲染器/序列化。

### 7.8 依赖方向被破坏

- `vistella_media`（约 200 文件一个 target）内部没有子模块边界；
- `vistella_media` ↔ `vistella_ui_dock` **循环 include 且互相 link**；
- `vistella_media` `PUBLIC` 链接 OpenCV → `opencv2/core.hpp` 经 `imagewindow.h` 泄漏到 app 公共头；
- `imageworker`（图层）反向依赖 `media`（ImageWindow），而 `LayerMask*` 却在 `media` 里（层级颠倒）；
- `src/network` 未 link `Qt6::Network`；`vistella_ui` 反向依赖 media 的 GPU 符号但 CMake 未声明。
**建议**：按"core → media/base → media/layers → media/render → media/tools → ui → app"拆 target；公共头不 include OpenCV（用 pimpl/前向声明 + `cv::Mat` 封装类型）；CI 加 `#include` 方向检查。

### 7.9 迁移未完成的残留物（"假完成"）

- `RightPanelDock` 的旧 accessor 全部 `return nullptr` + warn，但调用方还活着（09-01）；返回 nullptr 的兼容层比崩溃更难发现；
- `ColorDock`（4-tab 容器）已是死代码；`texteditorframe.cpp` 不在任何 CMake target；
- `ImageIOController::saveAs()/save()/writeImage()` 等 6 个接口返回空；`Result.h`/`Async.h` 基本无人使用且 `Q_TRY` 在 MSVC 编不过；
- `parallel_for`/`task_graph`/`RealtimeThread`/`SpscRing`/`AIManager`/`AuthClient` 零生产调用；
- 源码树有 4 个 `.bak31/.bak32/.bak33` 备份文件。
**建议**：一次性"清理周"——删除死代码或加 `[[deprecated]]` + 调用点迁移；禁止"返回 nullptr 的兼容层"；CI 加死代码/未使用 API 扫描。

### 7.10 错误处理与契约缺失

- 大量 `bool` 返回值被忽略（`savePublic()` 无返回值、`LayerStack::remove()` 等）；
- `cv::Exception` 会穿透 Qt 槽函数（LYR-02）→ 直接崩溃；
- `QDialogButtonBox` 的 Apply/Cancel 语义在预览场景没有回滚协议（09-02）；
- `Result<T>` 抽象存在但废弃。
**建议**：所有 Qt 槽入口 `try/catch(cv::Exception)`；统一 `Result<T>` 或 `expected`；文件/渲染/命令 API 全部返回可判定结果。

### 7.11 可测试性差 → 79 个测试全绿却漏掉所有 P0

现有测试多为"断言 cursor()/转发次数/结构存在"（如 `tst_VDraggableTabWidget` 不测 drop 坐标、`tst_FileTreeModel` 不测空目录、`tst_P8_1_1` 把 `m_compositorEnabled=true` 钉死）。
**建议**：测试分层——① 纯模型/算法单测（已有基础）；② 属性/不变量测试（撤销后状态、合成前后不变量）；③ 端到端场景（保存→重开→比对）；④ sanitizer/ASAN + 线程池压力测试；⑤ 视觉回归（截图比对）。

### 7.12 注释债与"注释与实现相反"

代表性例子：`~ImageWindow` 注释说重置 `m_brushCursor` 是防 UAF（正确），但同一文件注释称 `m_compositorEnabled` "Default is OFF"（实际 true）；`LayerStack` 注释称瓦片已清零（实际未初始化）；多份报告遇到"注释宣称完整实装，实际零调用"。
**建议**：注释描述"为什么"，不描述"已完成"；把"计划/未接线"统一用 `TODO(P8.x): not wired` 标记并纳入 CI 检查。

### 7.13 国际化/主题/样式碎片化

- `tr()` 使用不一致；`Vistella.qss` 里选中 tab 写死 `#ffffff`（暗色不可读）；80+ 处 inline `setStyleSheet` 不随主题刷新；
- `.ts` 839 条未翻译，且 `MainWindow` 重复安装 translator，无 `retranslate()`。
**建议**：只保留一个 `ThemeManager` 输出 palette/QSS；禁止 widget 级 inline 颜色；统一 `retranslateUi` + `LanguageChange` 分发。

### 7.14 构建与部署脆弱

巨型 target + `/INCREMENTAL:NO` workaround（.ilk 100MB+ 曾产出损坏 PE）、手工 DLL 管理、硬编码 `D:/Collide/opencv`、`D:/libraw`、`D:/vistella_qdebug.log`、`D:/Collide/.../landmark.onnx`；每模块 20+ 个 build 日志。
**建议**：拆 target 缩短编译；用 `CMakePresets.json` + vcpkg manifest 管理依赖；资源/模型走 qrc；删除绝对路径。

### 7.15 无 CI / 无静态分析 / 无 sanitizer

项目有 79 个测试、有 `ctest`，但没有 CI 配置；按代码里大量"手工验证"注释看，回归靠人肉。这是"改了 A 坏了 B"的温床（P8 引入 compositor 后立刻回归 CP-01 就是例证）。
**建议**：GitHub Actions（Windows + MSVC + Qt）跑 `cmake --build` + `ctest` + `cppcheck`/`clang-tidy`；PR 必须绿。

---

## 8. 工程卫生现状

| 项 | 现状 | 影响 |
|---|---|---|
| 根目录 | 179 个日志/脚本/临时文件，合计 56.8MB（`build_p*.log`、`ctest_*.log`、`fix_*.py`、`_*.ps1`、`_probe.png`、`_repro_*.png`） | 找不到东西，新人无从下手 |
| Git 仓库 | `.git` 287MB（打包对象），历史里有 160MB / 106MB 的超大 blob；当前工作树仅 42MB（其中 third_party 38MB） | clone 慢；建议 `git filter-repo` 清历史 |
| 文档 | `docs/PROJECT_SUMMARY_2026-09-03.md`、`ARCHITECTURE_P0_UI_2026-09-08.md` 停留在 P0 阶段（当前代码到 P8.3.x）；**无 README** | 文档与现实脱节 |
| 备份文件 | `*.bak31/.bak32/.bak33` 散落源码树 | 与真实代码混淆 |
| 测试 | 79 文件 / 82 可执行；历史 ctest 日志有失败记录；无 CI 基线 | 不知道当前是否全绿 |
| 依赖管理 | OpenCV/LibRaw 手工路径 fallback 链；Qt 靠 windeployqt + 手工删拷 | 换机器就崩 |
| 日志 | 根目录 100+ 构建日志 + `D:/vistella_qdebug.log` 双日志 | 磁盘/排查混乱 |

**建议**：一次性整理（半天）——把日志/脚本移到 `tools/` 或删除并加 `.gitignore` 规则；`.bak*` 全部删除（git 已有历史）；补 `README.md`（5 分钟能跑起来）；更新 `docs/ARCHITECTURE.md` 到当前架构；`CMakePresets.json` + vcpkg manifest。

---

## 9. 修复路线图

> 原则：**先保数据安全 → 再修核心链路 → 再补 PS 语义 → 最后做性能/新能力**。每阶段都有可验证的验收标准，避免"改完不知道好不好"。

### M0 止血（1–2 天，最高优先）

| # | 动作 | 涉及缺陷 |
|---|---|---|
| 1 | 删除 `onSave/onSaveAs/onExport` 里的 `flattenText()` | 05-01、09-03 |
| 2 | 修 `MainWindow::closeEvent` 的 `ImageWindow` 保存分支 | APP-01 |
| 3 | `RenderGraph` 通道归一化（1ch→BGR，4ch→BGRA2BGR）；类型不匹配 `LOG_WARN`；加 compositor 回退开关（默认保守） | CP-01、05-35 |
| 4 | `LayerCommand` undo/redo 统一 emit + invalidate + repaint；补 redo 的 new 值 | LYR-07/08 |
| 5 | `ThreadPool::run_task` 释放 Task；`AdjustmentPanel` 不再捕获全图 clone | CI-01 |
| 6 | 异步渲染回调改 `QPointer` receiver + 析构作废 token | CI-03、05-04、LYR-10 |
| 7 | `bindLeftDockWidgets` 先 detach；dock 存 `QPointer` | APP-02、05-05 |
| 8 | `toolChanged` 时清 `ImageCanvas::m_box` | 04-01 |
| 9 | `GradientMapFilter` LUT 初始化；`qTransformToAffine` 转置修正 | CP-13、06-01 |

**验收（手工回归 12 项）**：灰度 PNG / 带 alpha PNG / 16bit PNG 打开显示正常；多图层文档 Ctrl+S 后文字仍可编辑且无重复合成；退出时保存真实生效；图层改可见性/不透明度后立即重绘；Ctrl+Z/Ctrl+Y 视觉正确；关 tab 后再点工具栏不崩；24MP 图连续拖滑块 60 秒内存平稳；Ctrl+T 后切工具不崩且旋转方向正确。

### M1 核心链路（1–2 周）

1. **像素真相收敛**：`m_current` 只读化；编辑走"活动图层 + 命令"；`setCurrentImage` 不写 `m_original`（05-02/04-02/CP-11）。
2. **图层身份**：`LayerId` 改造（LYR-06/12/16），`m_groups` 改 id key；编组按钮按选区（LYR-05）。
3. **alpha/蒙版**：4 通道或独立 alpha 的合成契约；`LayerMask` 全链路接线（LYR-01/15，CP-18）。
4. **保存链路**：保存前 `rebuildCurrentCache`；失败回滚；`QSaveFile` 原子写（05-03、UI/DocWindow）。
5. **16bit/通道格式**：`convertTo` 量程修正、`matToQImage` 格式契约（05-07、06-02）。
6. **工具写图层**：Brush/Heal/Patch/RedEye 改走图层；修复 4 通道 stamp 寻址（04-02/03）。
7. **撤销内存**：按 ROI/字节预算；选区命令补齐（05-09、06-13/14）。
8. **颜色单一真源**：LeftToolBar/ColorTab/画笔收敛（09-01、04-14）。
9. **预览缓存**：移入 `ImageWindow` 并在析构清理（09-09/CP-14）。

**验收**：§10.3 的 20 项手工回归全过；ASAN 下无 UAF/泄漏；24MP 文档 100 步操作内存 < 4GB。

### M2 PS 语义与 UI（3–4 周）

1. **工程文件格式**（自定义 .vst + 可选 PSD 只读）：图层/蒙版/智能对象/文字/调整参数持久化 + 自动保存 + 崩溃恢复（APP-16、LYR-23、CI-07）。
2. **面板系统**：二选一——(a) 换 `QDockWidget`+`saveState`（推荐，删 1,500 行自研）；(b) 修 `VDraggableTabWidget`（坐标空间/浮动/双索引/跨窗悬垂）（UI-01~05）。
3. **菜单与 action 体系**：按 PS 重排 11 菜单；补图像→调整子菜单；补剪切/复制/粘贴；集中 `updateActionStates()`（APP-07/08/09）。
4. **工具补齐**：工具组（长按弹出）、渐变/油漆桶、橡皮擦、加深减淡、模糊锐化涂抹、抓手/缩放/旋转视图；修裁剪/吸管/画笔选项/Shape/Pen（04 全模块）。
5. **选区体系**：椭圆/多边形/磁性/快速选择；模式 UI 接线；蚂蚁线轮廓；羽化软边；选区限制绘画；撤销（06 选区部分）。
6. **调整与曲线**：对话框实时预览 + 取消回滚；曲线分通道；HSL/B&W/ChannelMixer LUT 真实现（09-02/04、06-04）。
7. **文件树**：换 `QFileSystemModel`；打开路径走 `MediaDispatcher` 路由（02、APP-18）。
8. **i18n/主题**：补 `.ts`、单一 translator、`retranslateUi`；清理 inline stylesheet；修暗色 QSS（APP-10/14/23）。

**验收**：产品级走查（打开→多图层编辑→调整→保存→重开复现）；PS 对照表主要项从"缺失/有但错"变为"部分/无"。

### M3 性能与专业能力（1–2 月）

瓦片增量渲染真接线 + 缓存上限 + 并行合成修复（CP-02/03/04）；GPU 后端决定"接线或下线"；画笔引擎重写（kind/dynamics/压感/纹理）；液化实时化；内容识别填充；图层样式；调整层；色彩管理；16/32bit 全链路；大图虚拟内存。

### M4 工程化（持续）

拆 target、CI（build+ctest+cppcheck+clang-tidy+ASAN）、`CMakePresets`+vcpkg、清理 `.bak`/日志、更新文档、补回归测试（每修一个 P0 加一条）。

### 需要你拍板的 5 个决策

1. **自研 dock 还是 QDockWidget？**（建议换原生：省 1,500 行且立刻获得浮动/布局持久化；代价是 PS 视觉需靠 QSS 补）
2. **GPU 本阶段"接线"还是"下线"？**（建议下线/隐藏：当前是 pass-through 且有 readback 翻转，接线成本高、收益低）
3. **是否投入 PSD 只读 + 自定义工程格式？**（建议自定义工程格式优先，PSD 只读作为导入增强）
4. **自研线程池还是换 oneTBB/Taskflow？**（建议换：自研 Chase-Lev 已有正确性 bug）
5. **未实装模块（音频/视频/视觉/AI/登录/主题画廊）是"隐藏"还是"继续占位"？**（建议隐藏入口：当前空壳会被用户当 bug）

---

## 10. 附录

### 10.1 模块报告索引

| 模块 | 文件 | 缺陷数 | 健康度 | 一句话 |
|---|---|---|---|---|
| 01 外壳 | `audit/modules/01-app-shell.md` | 48 | 4/10 | 退出丢数据、关 tab 崩、菜单顺序错、状态机是影子 |
| 02 UI | `audit/modules/02-ui-layer.md` | 42 | 4/10 | 自研 dock 拖拽 0% 可用；文件树同步卡 UI；设置/主题/语言/GPU 四条链未闭环 |
| 03 基础设施 | `audit/modules/03-core-infra.md` | 38 | 4/10 | 线程池任务泄漏 + Chase-Lev 错误；无 autosave；token 明文 |
| 04 工具 | `audit/modules/04-tools.md` | 42 | 4/10 | 裁剪不裁剪、吸管不吸色、画笔选项不可达、笔画不进图层 |
| 05 画布 | `audit/modules/05-canvas.md` | 38 | 4/10 | 保存毁文档、渲染 UAF、16bit 全白、撤销 GB 级 |
| 06 像素算法 | `audit/modules/06-pixel-ops.md` | 71 | 4/10 | 变换矩阵转置、蒙版错行、画笔引擎名存实亡 |
| 07 图层 | `audit/modules/07-layers.md` | 51 | **3/10** | redo 全废、undo 不刷新、删错层、编组毁画布 |
| 08 渲染/滤镜 | `audit/modules/08-render.md` | 49 | 4/10 | 默认路径黑屏、滤镜写坏图层、GPU 装饰品 |
| 09 面板/对话框/ICC | `audit/modules/09-panels.md` | 18 | 4/10 | 颜色死链、预览信号无人接、曲线不分通道、面板假数据 |

合计 **394 条**（P0 34 / P1 162 / P2 161 / P3 37，按各报告自报口径）。

### 10.2 审计方法与局限

**方法**：
1. 全量盘点：`src/` 350 文件 / 53,033 行按目录与耦合关系切 9 个模块，每模块一名审查员逐文件通读（非抽样）。
2. 统一规范：`audit/RUBRIC.md` 定义严重度、缺陷类型、输出格式、重点排查清单（指针/线程/撤销/坐标/IO/i18n…）。
3. 交叉验证：Lead 对 07/08/04/05 的 10+ 条 P0 独立复核代码（`cvtColor` 丢 alpha、`CV_8UC3` canvas、未初始化 LUT、`m_compositorEnabled` 默认值、`addWeighted` 空 Mat、`flattenText` 破坏性、颜色死链等）。
4. 独立命中：同一根因被 3 个模块独立发现（异步回调 UAF = CI-03/05-04/LYR-10；通道不匹配 = CP-01/05-35；颜色链 = 09-01/04-14），提高结论可信度。
5. 证据化：每条缺陷带 `文件:行号` + 代码摘录 + 复现路径 + 根因 + 修法。

**局限**（请在采纳时注意）：
- **静态审查**：未构建、未运行 GUI、未做 ASAN/并发复现；个别"概率性"结论标了 `[待确认]`。行为类结论基于代码语义与 Qt 机制推导。
- **并发修改**：审计期间工作区被另一进程修改（`ImageCanvas.cpp/.h` 的 P8.3.3 改动、`_probe.png`、`.bak33`），行号可能已偏移；本报告未触碰任何源码。
- **`tests/` 按用户要求未审查**：测试本身的正确性/覆盖率未评估（从抽查看，测试多断言表面行为，是 P0 漏网的原因之一）。
- **`third_party/` 未审查**（spdlog/lcms2/landmark 属第三方）。
- 数值/算法类结论（如滤镜公式是否与 PS 逐像素一致）以"是否明显错误/退化"为标准，未做像素级比对。

### 10.3 手工回归清单（建议固定为发布前必过，20 项）

1. 打开彩色 PNG/JPEG → 显示正常、缩放/平移正常。
2. 打开灰度 PNG、带 alpha PNG、16bit PNG → 显示正常（对应 P0 修复）。
3. 多图层：新建/复制/删除/上下移/合并/拼合 → 每步 Ctrl+Z / Ctrl+Y 视觉与数据一致。
4. 图层组：编组/取消编组/组内编辑 → 画布尺寸不变、不黑屏。
5. 调整不透明度/显隐/混合模式 → 立即重绘、可撤销。
6. 加蒙版 → 画笔涂抹/反相/羽化/禁用 → 渲染正确、可撤销。
7. 文字：创建/编辑/移动/旋转/保存 → 保存后仍可编辑、不黑屏。
8. 画笔：切换画笔/改大小/改颜色/改混合 → 笔画落在活动图层；带 alpha 图不花屏。
9. 裁剪：拖框→回车 → 图像真的被裁剪，撤销能还原。
10. 选区：矩形/套索/魔棒 + 加/减/交 → 蚂蚁线沿轮廓；羽化软边；撤销有效。
11. 滤镜：应用 + 预览 + 取消/确定 → 预览不残留；多图层不被重复合成。
12. 变换：Ctrl+T 旋转/缩放/斜切/透视 → 方向正确、无黑边错位；保存结果正确。
13. 调整对话框：曲线/色阶/HSL 拖动 → 实时预览；取消回滚；曲线分通道有效。
14. 保存：Ctrl+S / 另存为 / 导出 PNG/JPEG/TIFF/WebP → 文件可用；文字不被烧；旧像素不写入。
15. 退出：文档有改动 → 保存 → 真的保存；取消 → 不退出。
16. 关闭当前 tab（多文档）→ 再点左侧工具/面板 → 不崩。
17. 崩溃恢复：强杀进程 → 重启 → 提示恢复且主窗口显示。
18. 面板：拖动/浮动/重排/重启 → 布局保持（若实现）。
19. 主题：明/暗切换 → 所有面板可读，无白底白字。
20. 长会话：24MP 图连续 100 次操作 → 内存 < 4GB、无卡死。

### 10.4 快速收益清单（"半天内能明显变好"的 10 件事）

1. 删 3 处 `flattenText()` → 保存不再毁文档。
2. 补 `closeEvent` 的 ImageWindow 保存分支 → 退出不丢数据。
3. 加 compositor 回退开关 + 通道归一 → 灰度/alpha 图不再黑屏。
4. `LayerCommand` undo/redo 后统一 emit+invalidate+repaint → 撤销立刻"可见"。
5. `ThreadPool` 释放 Task → 拖滑块不再无限涨内存。
6. 异步回调改 `QPointer` receiver → 关 tab 不再概率崩。
7. `bindLeftDockWidgets` 先 detach → 工具点击不再重复触发。
8. `GradientMap` LUT 初始化 → 滤镜不再出随机噪点。
9. 菜单按 PS 顺序重排 + 补"图像→调整"入口 → UI 第一印象立刻接近 PS。
10. 修 `QTabBar::tab:selected` 的 QSS → 暗色主题 tab 不再白底白字。

---

*报告生成：2026-10-26 · 9 个模块审计 + Lead 汇总 · 所有缺陷证据与逐条修法见 `audit/modules/`。*
