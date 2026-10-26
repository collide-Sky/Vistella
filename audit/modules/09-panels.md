# 09 · 面板与调整对话框 — 审计报告

> 范围：`src/media/docks/**`（25 文件 2881 行）、`src/media/dialogs/**`（16 文件 1710 行）、
> `src/media/mediators/**`（6 文件 476 行）、`src/media/icc/**`（8 文件 773 行）
> 审查者：lead（团队 8 人上限，本模块由 Lead 亲自审查；覆盖度为"架构 + state-sync + 存盘链路 + 已完成度抽检"，
> 未逐行通读全部 5840 行，个别数值/算法细节标 `[待确认]`）

## 1. 模块概览

右侧面板在 P7.6.x v3 后是**两层结构**：

- `RightPanelDock`（QWidget，内含垂直 QSplitter）= 3 个 `VDraggableTabWidget` 分组：
  - 颜色组：`VColorPage`(ColorTab) / `VSwatchPage` / `VGradientPage` / `VPatternPage`
  - 属性组：`VPropertyPage`(PropertiesDock) / `VAdjustmentPage`(AdjustmentPanel)
  - 图层组：`VLayerPage`(LayersDock→LayerPanel) / `VChannelPathPage` / `VHistoryPage`(HistoryDock)
- 每个 `V*Page` 是 `src/ui/dock/VTabPageBase` 适配器，负责"页标题 + invalidateCache + preferredRenderPath"，
  内部包一个旧 dock widget（`inner()`）。

**关键问题：这是一次未完成的迁移**。旧 accessor（`colorDock()` / `historyDock()` / `layersDock()` /
`propertiesDock()` / `channelPathPanel()`）全部改成"返回 nullptr + 打 warning"的 deprecated shim
（`RightPanelDock.cpp:251-284`），但调用方没全部迁移（见 09-01）。

调整对话框是"模板方法"结构：`AdjustDialogBase` 提供 OK/Cancel/Apply 按钮与 `preview`/`applied` 信号，
`DialogMediator` 负责非模态生命周期，`DialogFactory` 负责按 id 创建，`AdjustmentPanel` 负责接 `applied` 参数并
落到 LUT。`ExportDialog` 负责导出参数，实际写盘在 `ImageIOController::onExport`。

ICC 是"嵌入能力"而非"色彩管理"：lcms2 封装（`IccProfile`）能 load/create/save/transform，
JPEG/PNG/TIFF 三种容器能注入 profile，但**打开图片不读 profile、显示不做转换、保存不嵌 profile**。

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议 |
|---|---|---|---|---|---|---|
| 09-01 | P1 | mainwindow.cpp:3407-3416 | state-sync | 用左侧工具栏改前景/背景色完全无效；右侧颜色面板也不跟着变 | `rightDock->colorDock()` 是 deprecated shim，**永远返回 nullptr**，`if (auto* colorDock = ...)` 整块死代码 | 改用 `colorPage()->inner()` 或删除该块、在 `ImageWindow` 内集中做双向同步 |
| 09-02 | P1 | AdjustDialogBase.cpp:67-71 + imagewindow.cpp:402-412 | logic | 曲线/色阶/黑白/通道混合器/HSL 五个对话框拖动参数时**没有实时预览**，只有点 OK 才生效 | `AdjustDialogBase::preview(QVariantMap)` 全项目**无任何 connect**，`triggerPreview()` 白发 | 在 `ImageWindow::showAdjustDialog` 里接 `preview → AdjustmentPanel::previewStandaloneParams`，配 `clearPreview` 于 Cancel |
| 09-03 | P0 | ImageIOController.cpp:75,101,128 + TextOverlayController.cpp:170-208 | data-loss | **按 Ctrl+S / 另存为 / 导出都会把文字烧成像素**：文字 item 被 `delete` 清空、不入撤销栈，保存后文字永久不可编辑 | `onSave/onSaveAs/onExport` 开头都调 `flattenText()`，而 `flattenText` 是破坏性操作（`removeItem + delete`，不 push 命令） | 删除保存路径里的 `flattenText()`；保存只需 `currentImage()` 的合成结果；文字保留在 scene/图层里 |
| 09-04 | P1 | CurvesAdjustDialog.cpp:95-158 + AdjustmentPanel.cpp:840-851 | logic | 曲线对话框切 RGB/Red/Green/Blue 通道**没有任何区别**，四个通道共享同一组控制点 | 对话框只存一份 `m_controlPoints`；`applyCurvesFromArgs` 读了 `"channel"` 但只对 `"points"` 生效 | `m_curves` 改为 `QHash<QString,QPolygonF>`；对话框按通道切换点集；LUT 按通道合成 |
| 09-05 | P1 | ChannelPathPanel.cpp:130-156 + grep 无消费者 | logic | 点"通道/路径"列表只有 LOG，什么也不会发生；没有真实通道数据、路径系统 | `channelSelected`/`pathSelected` 无 connect；通道列表硬编码 6 项静态标签 | 先做"RGB 分解 + 单通道可见性"（成本低、PS 语义明确）；路径要么实装 `QPainterPath` 模型，要么从 UI 移除 |
| 09-06 | P2 | HistoryDock.cpp:39,69-81 | ux/perf | 历史记录面板高度被硬编码上限 200px；每次 undo 全量重建列表；灰色用硬编码 RGB；没有 PS 的"打开"初始状态/快照 | `setMaximumHeight(200)`；`rebuildFromStack()` 每次 `clear()+appendRow` 全量重建；`QColor(160,160,160)` | 去掉高度上限、按 `QUndoStack` 增量插入（`indexChanged` 只移动当前行）、用 palette 颜色、增加"打开"伪条目 |
| 09-07 | P2 | SwatchTab.cpp/GradientTab.cpp/PatternTab.cpp | logic | 色板 15 个、渐变 8 个、图案 6 个**全是硬编码假数据**；不能保存自定义；`SwatchTab::setHighlightedIndex` 只写日志不绘制；渐变/图案**没有对应工具消费** | 三个 tab 是 P7.8 占位实现，注释明写"P7.11 接 .aco/.gpl"；ToolId 里没有 Gradient / PatternStamp | 或实装（.aco/.gpl 解析 + 自定义色板持久化 + Gradient/Pattern 工具），或明确降级为只读展示并在 UI 上标注 |
| 09-08 | P2 | ColorDock.cpp:73-80 + ColorTab.cpp:132-138 | design/ui | `ColorDock`（4 tab 容器）已是**死代码**（真实 UI 走 VColorPage 内的 ColorTab）；`showEvent` 每次强制跳回"色板"页；取色是阻塞式 `QColorDialog` | 迁移未清理；`showEvent` 里无条件 `setCurrentWidget`；ColorTab 没有 HSB/RGB 滑块与数值输入 | 删除 ColorDock 或明确保留为测试用；`showEvent` 只在首次设置默认页；实装 PS 风格 HSB/RGB 颜色面板 |
| 09-09 | P2 | imagewindow.cpp:1879-1885,579-608 | memory | 静态 `s_previewRegistry`（`QHash<ImageWindow*, shared_ptr<cv::Mat>>`）在 `~ImageWindow` **没有 clear** —— 预览矩阵永久泄漏，且窗口地址复用后可能显示旧文档的预览 | 预览缓存用文件作用域 static + 裸指针 key；析构函数只 reset 了 `m_brushCursor` | 预览缓存改成 `ImageWindow` 成员（或 `QHash` 存 `QPointer`/弱引用），析构必 `clearPreview()` |
| 09-10 | P2 | ImageIOController.cpp:124-223 | ux/logic | 导出要过两道对话框（ExportDialog 选参数 → 再弹 QFileDialog 选路径）；ExportDialog 里选的格式与第二次文件对话框扩展名可能不一致；GIF 格式大概率写不出（Qt GIF 插件只读 `[待确认]`） | `onExport` 先 `dlg.exec()` 再 `QFileDialog::getSaveFileName`，两者无联动 | 合并成一个对话框（PS 的"导出为"就是单窗口）；按 `QImageWriter::supportedImageFormats()` 动态过滤格式；GIF 若不支持就隐藏 |
| 09-11 | P2 | ImageIOController.cpp:68-111 | data-loss | 普通保存（Ctrl+S）**不写 ICC profile**，只有"导出"才嵌 ICC；EXIF/分辨率元数据全丢；保存为 PNG 时 alpha 之外的信息不可控 | `onSave/onSaveAs` 直接 `ImageProcessor::saveImage`，没有 profile/metadata 参数 | `SaveOptions` 增加 `iccData` + `metadata`；保存沿用文档当前 profile |
| 09-12 | P2 | ExportDialog.cpp:174-180 | logic | GIF 页只有一句"(GIF 仅支持 256 色索引)"，没有调色板/透明度/循环选项；TIFF 压缩选项只有部分枚举 | 占位 UI | 按格式补齐选项或移除格式项 |
| 09-13 | P2 | DialogMediator.cpp:66 | design | 非模态对话框 parent 用 `QApplication::activeWindow()`，可能挂到任意窗口（含 QMessageBox/主窗口），导致对话框跑到错误窗口上面或随主窗口最小化 | 用全局活动窗口推断归属 | parent 显式传 `ImageWindow*`（`showDialog` 增加 owner 参数），并按 owner 管理 |
| 09-14 | P3 | CurvesAdjustDialog.cpp:65,102,110 | i18n/ui | 对话框标题是 `"Curves (P0-3.2 v2)"`（内部版本号泄漏给用户）；`"Channel:"`、`"Reset"` 硬编码英文 | 开发期字符串未清理 | 全部走 `tr()`，标题改"曲线"；与 `AdjustmentPanel` 的曲线 UI 统一 |
| 09-15 | P3 | dialogs/AdjustDialogBase.cpp:18 | ui-visual | 所有调整对话框固定 `resize(420,320)`，曲线/色阶塞不下（曲线编辑器本身要 256×256 + 通道行 + 按钮） | 固定尺寸 | 用 `adjustSize()` + 每对话框 `setMinimumSize`，记住上次尺寸 |
| 09-16 | P3 | IccProfile.cpp:134-162 | design | `applyTo()` 恒以 sRGB 为源、原地转换，没有"文档工作空间"概念；调用点极少 | 色彩管理未设计 | 文档持有 `ColorSpace`（profile + bit depth），加载时解析、显示时转换、导出时嵌入 |
| 09-17 | P3 | mediators/* | design | 三套 Mediator（Tool/Workspace/Dialog）+ 每个 dock 自己的信号，形成 4 层信号转发；`DialogMediator` 还自己 connect 自己的信号（emit→self-slot） | 过度分层 | 保留 Tool/Workspace；`DialogMediator` 直接 `createDialog()` 不用自发信号；考虑合并到 `ImageWindow` 的 dialog 管理器 |
| 09-18 | P3 | LayersDock.cpp:27-39 | ui-visual | 图层 dock 里仍有 `QLabel("Layer Panel (F-G.3)")`、`QLabel("Channels/Paths (F-M)")` 占位标签，未被替换时用户能点到 | 迁移遗留 | 删除占位 label，空态显示 PS 风格"(无图层)" |

### 09-01 左侧工具栏取色是死链 (P1)

- **位置**：`src/app/mainwindow.cpp:3407-3416`；`src/media/docks/RightPanelDock.cpp:251-256`
- **证据**：

```cpp
// mainwindow.cpp
if (auto* rightDock = iw->colorDock()) {                 // RightPanelDock*  OK
    if (auto* colorDock = rightDock->colorDock()) {      // ← deprecated shim
        connect(leftToolBar, &tools::LeftToolBar::foregroundColorChanged, ...);
```

```cpp
// RightPanelDock.cpp
ColorDock* RightPanelDock::colorDock() const
{
    LOG_WARN("[RightPanelDock] DEPRECATED colorDock() called; ...");
    return nullptr;                                       // ← 永远 nullptr
}
```

- **现象**：左侧工具栏底部的前景/背景色块点开取色 → 画笔颜色不变、右侧颜色面板不更新。反过来在右侧颜色面板取色 → 左侧色块不更新（`ImageWindow` 里的注释说"搬到 MainWindow 了"，但 MainWindow 连的是反方向）。
- **根因**：P7.6.x v3 把 `ColorDock` 换成 `VColorPage` 后，旧 accessor 改成返回 nullptr 的兼容层，主窗口的同步代码没跟着改，`if` 静默失败。
- **修复**：`mainwindow.cpp` 改为 `if (auto* colorTab = rightDock->colorPage()->inner()) { ... }`；
  更彻底的做法是在 `ImageWindow` 里持有唯一的"当前前景/背景色"，`LeftToolBar` / `ColorTab` / 画笔都只做 view，
  由 `ImageWindow::setForegroundColor()` 单点广播（消除现在三份状态副本）。

### 09-02 调整对话框没有实时预览 (P1)

- **位置**：`src/media/dialogs/AdjustDialogBase.cpp:67-71`、`src/media/imagewindow.cpp:402-412`
- **证据**：

```cpp
void AdjustDialogBase::triggerPreview() { emit preview(m_args); }   // 全项目唯一 emit 点
// imagewindow.cpp 只接 applied：
connect(dlg, &dialogs::AdjustDialogBase::applied, this, [this, dialogId](const QVariantMap& args) {
    if (m_adjustmentPanel) m_adjustmentPanel->setStandaloneParams(dialogId, args);
});
```

- **现象**：打开"曲线"，拖动控制点，画布毫无变化；点"确定"后突然跳到最终效果。PS 是实时预览。
- **根因**：`AdjustDialogBase` 设计了 `preview` 信号，但没有任何订阅者；`AdjustmentPanel` 也没有"临时预览/回滚"接口。
- **修复**：`AdjustmentPanel` 增加 `previewStandaloneParams()`（写入临时 LUT 并只重绘，不 push undo）+ `clearStandalonePreview()`；
  `ImageWindow` 接 `preview` 与 `rejected` 两个信号。

### 09-03 保存/导出把文字烧成像素（不可逆）(P0)

- **位置**：`src/media/imagewindow/ImageIOController.cpp:75, 101, 128`；`src/media/imagewindow/TextOverlayController.cpp:170-208`
- **证据**：

```cpp
// ImageIOController::onSave() 第一句
if (m_host->textOverlay()) m_host->textOverlay()->flattenText();
```

```cpp
// TextOverlayController::flattenText()
m_host->setCurrentImage(newMat);            // 合成结果写回图像
for (auto* item : m_items) {                // 直接删除，不入 undo
    m_host->sceneRef().removeItem(item);
    delete item;
}
m_items.clear();
```

- **现象**：文档里打了一行字，按 Ctrl+S。字"消失"（变成像素），再也不能改字号/内容/位置；`Ctrl+Z` 也回不来（没有 push 命令）。
  另存为、导出同样如此。PS 保存永远不会破坏正在编辑的文字层。
- **根因**：把"位图保存格式没有图层概念"这个限制，用"提前烧图"的方式硬解，且发生在保存路径（副作用）。
- **修复**：
  1. 立刻从 `onSave/onSaveAs/onExport` 删除 `flattenText()`；
  2. `currentImage()` 本身已是图层合成的结果（`rebuildCurrentCache`），直接写它即可；
  3. 若要"所见即所得"的带文字导出，临时合成一份（clone）再写盘，不修改文档状态；
  4. 中长期：实装分层工程格式（见 3. 功能差距）。

### 09-04 曲线没有按通道 (P1)

- **位置**：`src/media/dialogs/CurvesAdjustDialog.cpp:81-87, 125-143`；`src/media/imagewindow/AdjustmentPanel.cpp:840-851`
- **证据**：

```cpp
connect(m_channelBox, &QComboBox::currentTextChanged, this, [this](const QString& text) {
    m_channel = text;                                  // 只改了一个 QString
    updateParam(QString("channel"), m_channel);
    triggerPreview();                                  // 但 m_editor 的点集没变
});
```

- **现象**：切到 Red，曲线形状不变；拖 Red 的曲线，RGB 也跟着变。PS 的曲线是 Per-Channel 的。
- **修复**：`CurveEditor` 持 4 组点（RGB/R/G/B），切换时换点集；`AdjustmentPanel::m_curves` 改 `QHash`；
  `buildCurvesLUT` 按通道分别生成 256 项 LUT 再合并。

### 09-05 通道/路径面板是展示品 (P1)

- **位置**：`src/media/docks/ChannelPathPanel.cpp:130-156`；`grep` 结果：`channelSelected`/`pathSelected` 无任何 connect
- **现象**：点击"Red"通道没有任何反应（PS：只显示红通道 / 只有红通道可编辑）；"路径"列表是静态字符串。
- **修复**：先做最小可用版——`selection::SelectionModel` 已有单通道 mask，可以直接做"选区作为 Alpha 通道"的
  保存/载入/显示；通道可见性 = `LayerStack::render()` 输出时按通道置零。路径属于大工程，建议先隐藏入口。

### 09-09 预览缓存泄漏 (P2)

- **位置**：`src/media/imagewindow.cpp:1879-1885`（static registry）、`579-608`（析构未清理）
- **证据**：

```cpp
// 匿名 namespace 里的全局状态
QMutex s_previewRegistryMutex;
QHash<ImageWindow*, std::shared_ptr<cv::Mat>> s_previewRegistry;
```

- **现象**：每次滤镜预览都会存一份与图像同尺寸的 `cv::Mat`；关闭文档后仍留在静态表里（4K 图约 100MB/份）。
  地址复用后新文档可能命中旧预览（显示错误图像）。
- **修复**：改为 `ImageWindow` 成员 `std::shared_ptr<cv::Mat> m_preview;`，或 `QHash<QPointer<ImageWindow>, ...>`
  并在析构里 erase。这里也建议统一改掉"文件作用域 static 当成员用"的写法。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 颜色面板 | HSB/RGB 双模式滑块 + 数值输入 + 色相条 + 前景/背景/交换/默认 | 只有两个色块 + "拾取颜色"(阻塞 QColorDialog) + X/D 按钮 | 部分 | P1 |
| 色板面板 | 多组预设、载入 .aco/.ase/.gpl、保存/替换/追加、右键菜单 | 3 组共 15 个硬编码色 | 缺失 | P2 |
| 渐变面板 | 预设库、渐变编辑器（色标/透明度/中点/平滑/噪点）、5 种类型 | 8 个硬编码双色预设 + 类型下拉；**没有渐变工具** | 缺失 | P1 |
| 图案面板 | 预设库、自定义图案、缩放/对齐 | 6 个程序生成的假图案；**没有图案图章工具** | 缺失 | P2 |
| 图层面板 | 组/嵌套/混合/锁定/颜色标记/搜索过滤/多选/批量操作 | 有基本列表 + 拖拽 + 蒙版缩略图（详见 07 报告） | 部分 | P1 |
| 通道面板 | 真实 RGB/专色/Alpha 通道、可编辑、通道选区、分离/合并 | 6 个静态标签，点击无效果 | 缺失 | P1 |
| 路径面板 | 钢笔路径、路径/形状/选区互转、填充/描边 | 静态列表 | 缺失 | P2 |
| 历史记录 | 快照、非线性历史、可拖动、保留大量步骤 | 只是 `QUndoStack` 的镜像列表，高度限 200px | 部分 | P2 |
| 调整面板 | 15+ 调整层、参数可重编辑、剪贴蒙版 | Curves/Levels/HSL/B&W/ChannelMixer 5 个，单曲线无分通道 | 部分 | P1 |
| 调整对话框实时预览 | 拖动即预览 | 无预览（09-02） | 缺失 | P1 |
| 导出 | 单窗口、多格式、元数据、ICC、预览、批处理 | 双窗口、格式受限、无元数据、ICC 仅部分格式 | 部分 | P2 |
| 色彩管理 | 指定/转换配置文件、软打样、溢色警告、工作空间 | 仅"导出时嵌入 profile" | 缺失 | P2 |
| 信息面板（坐标/颜色/尺寸） | 实时 | 无 | 缺失 | P2 |
| 直方图面板 | 实时 RGB/亮度/通道 | 无独立面板（AdjustmentPanel 里有静态直方图） | 缺失 | P2 |
| 导航器面板 | 缩略图 + 视口框 | 无 | 缺失 | P3 |
| 字符/段落面板 | 完整排版 | 无（只有字号/颜色/粗斜体菜单） | 缺失 | P2 |
| 画笔预设面板 | 预设库 + 分组 + 导入 .abr | BrushPickerDialog 有基本 picker | 部分 | P2 |
| 动作/批处理 | 录制/回放/批处理 | 无 | 缺失 | P3 |
| 图层复合 | 多套可见性/位置组合 | 无 | 缺失 | P3 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 面板拖拽/停靠/浮动/分组 | `QT-NATIVE` | `QDockWidget` + `QMainWindow::addDockWidget/splitDockWidget/tabifyDockWidget` 原生支持；项目自研 `VDraggableTabWidget` 反而带来复杂度 | 评估把自研 dock 换回 QDockWidget（PS 风格需要"面板可拖成浮动窗口"，QDockWidget 直接支持） |
| 颜色面板 HSB/RGB 拾取器 | `QT-NATIVE` | `QColorDialog` 只有一种布局；自绘 `QWidget` 画色相条/三角/滑块是常规做法（QPainter） | 自绘 `ColorPickerWidget`（约 1-2 人天）；或引 `QtColorWidgets`（MIT） |
| 曲线编辑器 | `QT-NATIVE` | QPainter + 控制点拖拽；已有 `CurveEditor` 雏形 | 扩展为 4 通道 + 输入输出数值框 |
| 色板 .aco/.ase/.gpl 解析 | `QT-EFFORT` | 无 Qt 原生支持，格式简单（二进制/文本），自写解析器约 300-500 行 | 自研解析（推荐），或 `Krita` 的 `KisResourceLoader`（LGPL）参考 |
| 渐变编辑器 | `QT-NATIVE` | QPainter 自绘色标 + 透明标；渐变填充用 `QLinearGradient/QRadialGradient/QConicalGradient` | 自绘约 2 人天 |
| 图案库/图案填充工具 | `QT-NATIVE` | `QBrush(QPixmap)` + `QPainter::setBrushOrigin` 即可 | 加 PatternStamp 工具 |
| 通道真实数据 | `QT-NATIVE` | OpenCV `cv::split/cv::merge` + 单通道显示 | 在 `LayerStack` 上做 channel view |
| 历史快照 | `QT-NATIVE` | `QUndoStack` 够用；快照 = 复制 `cv::Mat`，要控制内存 | 加"快照"命令 + 内存上限（LRU） |
| 软打样/CMYK | `QT-3RD` | lcms2 已集成，能做 proofing/CMYK 转换；Qt 显示层只支持 RGB | lcms2 transform + 自绘；CMYK 文档需自管 4 通道数据 |
| 16/32 位/浮点文档 | `QT-NATIVE` | `QImage::Format_RGBA64`/`RGBA64FP` 存在；OpenCV `CV_16U/CV_32F` 完整支持 | 需要统一"文档像素格式"抽象，目前硬编码 8UC3 是最大障碍 |
| 导出 GIF 动画 | `QT-3RD` | Qt 内置 GIF 只读（`[待确认]` 需实测）；写 GIF 需 giflib 或自研 LZW | `giflib`（MIT）或 `libimagequant` |
| 元数据（EXIF/XMP/IPTC） | `QT-3RD` | Qt 无公开 API 写 EXIF；`libexif`/`exiv2`（BSD/GPL 双许可） | exiv2 或自研最小 EXIF 读写 |
| 导出 PDF | `QT-NATIVE` | `QPdfWriter` | 若需要可加"导出 PDF" |
| 打印 | `QT-NATIVE` | `QPrinter` + `QPrintDialog` | 加"打印"菜单 |
| 面板布局记忆/工作区 | `QT-NATIVE` | `QMainWindow::saveState/restoreState` | 已有 WorkspaceMediator，可对接 saveState |

## 5. 设计问题

1. **迁移半途而废**：`RightPanelDock` 里同时存在"新 V*Page 体系"和"旧 dock 类 + deprecated nullptr shim"。
   返回 nullptr 的 accessor 是最危险的兼容形态：调用方 `if (auto* p = ...)` 静默跳过，bug 不会崩只会"没反应"，
   极难发现（09-01 就是典型）。建议：迁移完就删 shim，或 shim 用 `Q_ASSERT(false)` + 崩溃暴露。
2. **颜色状态三份副本**：`LeftToolBar::m_fgColor` / `ColorTab::m_foreground` / 画笔 `m_paintColor`。
   三者靠信号单向同步，任何一条链断掉就出现 09-01。应该收敛为 `ImageWindow` 单一真源 + 各 view 订阅。
3. **面板知道业务、业务知道面板**：`ImageWindow` 里大量 `m_rightDock->xxxPage()->inner()->connect(...)`，
   层级穿透 3 层。建议给 `RightPanelDock` 提供"转发信号"的 facade，或让 page 自己订阅 `ImageWindow` 的 model 信号。
4. **对话框 = 参数收集器 + 副作用**：`AdjustDialogBase` 只发 `QVariantMap`，真正的"应用"落在 `AdjustmentPanel`
   的 `setStandaloneParams`，形成"对话框 → mediator → imagewindow → adjustmentpanel"四跳。可简化为
   `AdjustmentPanel::openDialog(kind)` 内部闭环。
5. **静态全局当成员**：`s_previewRegistry`（09-09）是文件作用域 static + 裸指针 key，违反"谁拥有谁管理"。
6. **重复实现**：`CurvesAdjustDialog` 与 `AdjustmentPanel` 各有一份曲线 UI/逻辑；`TransformDialog` 与
   `TransformBox` 也各有一套变换输入。应抽公共控件。
7. **i18n 与调试字符串混入 UI**：`"Curves (P0-3.2 v2)"`、`"Channel:"`、`"Reset"`、`"(GIF 仅支持 256 色索引)"` 等。

## 6. 修复建议（按优先级）

**P0 立即修（小时级）**
- 09-03：删除 `onSave/onSaveAs/onExport` 里的 `flattenText()`（1 h，风险低，收益最大）。

**P1 本轮修（1-3 天）**
- 09-01：修 `mainwindow.cpp` 的颜色同步（改成 `colorPage()->inner()`，并补反方向），0.5 天。
- 09-02：`AdjustmentPanel` 加预览/回滚接口 + `ImageWindow` 接 `preview` 信号，1 天。
- 09-04：曲线 4 通道化（`CurveEditor` + `m_curves` + LUT），1.5 天。
- 09-05：通道面板最小可用版（Alpha/选区通道显示），2 天；路径入口先隐藏。
- 09-09：预览缓存移入 `ImageWindow`，0.5 天。

**P2 后续（3-5 天）**
- 09-06 历史面板、09-07 色板/渐变/图案实装或收缩、09-08 颜色面板自绘、09-10/09-12 导出合并与格式过滤、
  09-11 保存 ICC/元数据、09-13 DialogMediator owner 化。

**架构级（1-2 周）**
- 面板/颜色/对话框统一到"文档 model → view 订阅"结构，删掉 deprecated shim 与 `ColorDock` 死代码。
- 文档像素格式抽象（8UC3 → 8/16/32F、RGB/CMYK、带 profile），这是色彩管理与图层格式的前提。

## 7. 需要 Lead 协调的跨模块问题

- **09-01 / 颜色单一真源**：涉及 `LeftToolBar`（04 工具组）、`ImageWindow`（05 画布组）、`ColorTab`（本组），需要一次统一改造。
- **09-03 / 保存语义**：涉及 `ImageIOController`（05）、`TextOverlayController`（05/06）、`LayerStack`（07）；
  若要做"保存保留可编辑内容"，必须引入工程格式（涉及 07 的序列化设计）。
- **GIF/WebP/TIFF 写盘**：依赖 Qt imageformats 插件部署（CMake windeployqt），需要 03 构建组确认部署清单。
- **调整层非破坏**：现在调整只作用于 `m_current`（像素），真正 PS 语义的"调整层"依赖 07/08 的合成器；
  09-02 的预览接口设计要和 08 的 RenderGraph 对齐。
- **icc 工作空间**：与 05 的加载/保存、08 的最终合成色彩空间相关，需要统一定义。
