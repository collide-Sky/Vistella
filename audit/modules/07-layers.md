# 07 · 图层系统 — 审计报告

> 范围：`src/media/imageworker/**`（ImageWorker / ImageWorkspace / ImageProcessorAsync，6 文件 526 行）+
> `src/media/imageworker/layers/**`（Layer.h、LayerStack.*、LayerPanel.*、LayerCommand.*、BlendMode.*、
> LayerMask*、SmartObjectWatcher.*，16 文件 5311 行）+ `src/media/imageworker/CMakeLists.txt`；
> 上下文另读 `src/media/compositor/**`（LayerStackAdapter 与全部 node）、`src/media/imagewindow.cpp`、
> `src/media/imagewindow/ImageIOController.cpp`、`src/app/mainwindow.cpp`、`tests/tst_LayerKindRender.cpp`。
> 审查者：audit-layers（teammate）

## 1. 模块概览

`LayerStack`（QObject，`shared_ptr<Layer>` 列表 + `QHash<LayerId, vector<LayerPtr>>` 组子树 + `QHash<int, QList<int>>` 智能滤镜链）是图层数据的唯一真源：
`ImageWindow` 持有它，`LayerPanel`（QTreeWidget）只做显示/发信号，`MainWindow`/`ImageWindow` 的槽再调 `LayerStack` API 并 `push` `LayerCommand` 进 `QUndoStack`；
渲染上 `LayerStack::render()` 默认转发到 `renderViaCompositor()`（`LayerStackAdapter` 建 DAG + `RenderGraph` 分块合成），失败路径才是遗留的逐层循环。
`BlendMode.cpp` 提供 12 种混合算式，`LayerMaskRenderer` 只在 tests 里被调用。
`ImageWorker`→`ImageWorkspace`→`ImageWindow` 只做模块外壳；`ImageProcessorAsync` 无生产调用者。

### 1.1 七个重点问题的逐项结论

| # | 重点 | 结论 |
|---|---|---|
| 1 | 数据模型 | 组用 `m_groups` 以 **LayerPtr 地址** 为 key（图层换地址即丢子树）；智能滤镜链用 **int 下标** 为 key（任何增删移动即错位）；`Layer` 默认拷贝 **不 clone cv::Mat**（注释写反）；6 种 kind 字段基本够用但 **Text/Vector 无 alpha、无变换、无段落**；**零序列化**；无图层数上限；内存峰值为「层数 × 画布」（compositor 路径另加无界 TileCache） |
| 2 | 渲染正确性 | 遗留路径与 DAG 路径在「隐蔽 base 图层」「非零 base 下标」「层尺寸 ≠ 画布」「组内 Adjustment/SmartFilter」上行为不同；两条路径**共同**的错误：`visible=false` 的 base 仍参与、组不隔离且忽略组 blend、无 alpha 通道；compositor 默认 ON 且每帧重建 DAG |
| 3 | LayerCommand | 持 **int 下标 + LayerStack 裸指针**；`Merge` undo 不恢复上层；`Remove` redo 按 `image.data` 指针定位会删错层；**除 Merge 外所有 redo 均为 no-op**；undo 全部静默改字段（不发信号/不刷新） |
| 4 | BlendMode | 实现 12 种（Normal/Multiply/Screen/Overlay/SoftLight/HardLight/ColorDodge/ColorBurn/Darken/Lighten/Difference/Exclusion）；Hue/Saturation/Color/Luminosity **枚举存在但 fallback Normal**；缺 Vivid/Linear/Pin Light、Hard Mix、Linear Burn、Subtract、Divide、Darker/Lighter Color、Pass Through；SoftLight 用 sqrt 近似；全 CPU 逐像素，无 GPU 后端接入 |
| 5 | LayerPanel | **完全没有拖拽排序**（无 `setDragDropMode`/drop 事件）；组子项**只读**（右键直接 return）；无显隐/锁定勾选按钮（只有文字前缀）；无蒙版缩略图；缩略图每次全量重算；选中态以 `LayerStack::selection()` 为真源但 UI 行号与模型下标无稳定映射 |
| 6 | SmartObject | Watcher 以 **索引 + 路径**双键，同源多图层会互相踩；embedded 场景推导出的 cache 路径与 `convertToSmartObject` 实际写入路径**不一致**；无目录监视；轮询在 UI 线程 `QFileInfo`；外部修改→刷新链路可用，但**保存重开后全部丢失**（无序列化） |
| 7 | Workspace/Async | `ImageWorkspace::save()` **无条件返回 true**，`saveAs(path)` **忽略 path** 并弹对话框；`ImageProcessorAsync` 无取消（`TaskContext` 参数被丢弃）、无生产调用者；`LayerStack::renderAsync` 在后台线程无锁读 `m_layers`，且回调捕获 `this`，关窗可 UAF |

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| LYR-01 | P0 | imageworker/layers/LayerStack.cpp:1389-1440 | logic | 新建文字/矢量图层后整幅画布变黑（只剩文字/图形） | 透明背景 `ARGB32` 转 BGR 时丢弃 alpha，黑色被当作「不透明」参与合成 | 合成改用 4 通道 BGRA（或独立 alpha 掩码） |
| LYR-02 | P0 | imageworker/layers/LayerCommand.cpp:75-92 | crash | 对文字/组/智能对象/尺寸不同图层「向下合并」→ cv::Exception 崩溃 | `cv::addWeighted(m_mergeBefore.image, …, src->image, …)` 未校验空/尺寸/通道 | 合并统一走 `rasterize` + `applyBlend`，并对空 Mat 早退 |
| LYR-03 | P0 | imageworker/layers/LayerCommand.cpp:464-474 | data-loss | 合并后 undo，上层图层永久消失 | Merge undo 明确「不重建上层」只恢复下层 image | 命令保存上层完整 Layer 并按原下标重建 |
| LYR-04 | P0 | imagewindow.cpp:745-767、1299-1320 | data-loss | 「拼合图像」后 undo 图层不回来；面板入口连撤销记录都没有 | `flattenVisible()` 内部 `clear()`；只推了 `ImageEditCommand`（图像级） | 引入 `LayerSetCommand` 保存整栈快照（或整栈事务） |
| LYR-05 | P0 | LayerPanel.cpp:856-861 + LayerStack.cpp:316-355、1344-1352、1560-1576 | data-loss | 点「group」按钮后画面变 800×600 白底/只剩左上角内容 | `onGroupClicked` 把 `[0, count-1]` 全栈打包，base Bitmap 进入组 → baseIdx=-1，`canvasSize()` 退化为 800×600 | 按选区编组；`canvasSize()` 从文档级画布读取 |
| LYR-06 | P0 | imageworker/layers/LayerCommand.cpp:677-687 | data-loss | 删除某些图层后 redo，删掉的是**另一个**图层 | `Remove::redo` 用 `m_layer.image.data == l->image.data` 反查；空 image 的层（Text/Group/Adjustment）全为 nullptr，duplicate 层共享缓冲 | 命令持 LayerPtr/LayerId 稳定身份，不用像素指针 |
| LYR-07 | P0 | imageworker/layers/LayerCommand.cpp:403-659 | state-sync | Ctrl+Z 后画面和面板都不变（模型已变） | undo 直接写 `l->opacity/visible/…`，不 emit `layerChanged`、不 `invalidateCurrentCache`、不 `renderToView` | undo/redo 末尾统一 `emit layerChanged + invalidate + render`（或走 setter API） |
| LYR-08 | P0 | imageworker/layers/LayerCommand.cpp:661-782 | logic | undo 之后 redo 什么都不做（除 Merge） | 30 余个 Op 的 `redo()` 被写成 no-op（依赖「调用方已应用」） | redo 必须携带 new 值并重新应用 |
| LYR-09 | P0 | imageworker/layers/LayerStack.cpp:1775-1809 | threading | 大图渲染期间连续操作图层 → 概率性崩溃/画面错乱 | 后台线程 `self->render()` 与 UI 线程修改 `m_layers` 并发，无锁/无快照 | 提交前做不可变快照（或递归加锁 + 队列化） |
| LYR-10 | P0 | imagewindow.cpp:1965-1971、579-608 | crash | 大图渲染未完成时关闭文档 → UAF | 回调 lambda 捕获 `this`；`m_currentTaskId` 不随析构失效；queued invoke 不会被撤销 | 用 `QPointer<ImageWindow>` + 析构时 bump taskId 并断开 |
| LYR-11 | P0 | imageworker/layers/LayerStack.cpp:928、1465 | logic | 用户名含中文时「转换为智能对象/栅格化/智能对象显示」失败或显示 MISSING 占位 | `cv::imwrite/imread(path.toStdString())`，OpenCV 在 Windows 走 ANSI，非 ASCII 路径失败 | 用 `QImage`/`toLocal8Bit` 或 `QSaveFile` 读写，缓存目录用短 ASCII 名 |
| LYR-12 | P0 | imageworker/layers/LayerStack.cpp:1688-1696、1243-1255、304-308 | memory | 拼合/清空后旧组子树常驻；极端情况下新 Group 继承旧 children | `clear()`/`flattenVisible()` 不遍历 `releaseGroupChildren`；`m_groups` 以地址为 key | `clear()` 中 `m_groups.clear()`；地址键改稳定 id |
| LYR-13 | P1 | LayerStack.cpp:1563-1576、1579 | logic | 隐藏背景图层无效（画布仍是它） | canvas 先 `clone` base image，之后才判 `visible` | base 的 visible 参与决策，或统一走逐层循环 |
| LYR-14 | P1 | LayerStack.cpp:1579、1344-1352 | logic | 位于 base 之下的图层永不参与合成；index 0 非位图时文字层尺寸错误 | 循环从 `max(0, baseIdx)` 起；`canvasSize()` 只读 `m_layers[0]` | 用文档级 `m_canvasSize`；循环覆盖全栈 |
| LYR-15 | P1 | LayerStack.cpp:1644-1647、1518-1549；LayerStackAdapter.cpp:272 | logic | 「加/反转/羽化/密度/矢量蒙版」全都没有视觉效果 | render 仅消费旧字段 `layerMask/maskEnabled`；新 `LayerMask mask` 无人消费，`LayerMaskRenderer` 仅被 tests 调用 | 统一到一套蒙版模型并接入两条渲染路径 |
| LYR-16 | P1 | LayerStack.h:337；LayerStack.cpp:438-465、485-503、1049-1110 | state-sync | 移动/删除图层后智能滤镜挂到别的图层或不生效 | `m_smartFilterChain`/`parentSmartIndex` 是 int 下标，增删移动不重键 | 改 LayerId + 子节点存于父 Layer |
| LYR-17 | P1 | mainwindow.cpp:2983 vs imagewindow.cpp:2170；LayerCommand.cpp:612-626 | logic | 一处「智能滤镜」undo 必然无效 | 同一工厂参数语义不同：一处传父 smart 下标、一处传 filter 下标 | 工厂只接收 filter 的稳定 id |
| LYR-18 | P1 | LayerCommand.cpp:475-502 | data-loss | 组操作 undo 后层数/顺序错乱 | Group/Ungroup undo 用裸下标删除/插入 | 保存被删区间快照，按内容恢复 |
| LYR-19 | P1 | LayerStack.cpp:470、Layer.h:128-132 | memory | 复制图层与原件共享像素缓冲，undo 快照也共享 | 默认拷贝构造对 `cv::Mat` 是浅拷贝（注释却写 clone） | 明确「不可变 + COW」策略或显式 clone |
| LYR-20 | P1 | LayerStack.cpp:1218-1241 | logic | 合并结果与所见不符（混合模式/蒙版丢失、透明度双计） | mergeDown 直接 `addWeighted` 原始 image，不 rasterize/不套 mask/blend | 用「渲染该像素区间」的合成结果替换 |
| LYR-21 | P1 | BlendMode.cpp:283-287 vs LayerBitmapNode.cpp:38-57、LayerBlendNode.cpp:29-64 | logic | 同一文档在两条渲染路径下画面不同（层尺寸≠画布时） | 遗留：`canBlend` false → 图层被静默丢弃；DAG：补黑/裁剪后继续合成 | 二选一并统一：先规范化到画布再混合 |
| LYR-22 | P1 | LayerStack.cpp:1518-1549、1590-1603；GroupNode.cpp:35-58 | logic | 组不隔离：组内 Multiply 与背景参与运算；组自身的混合模式无效 | 子层直接在「画布 clone」上迭代；组只用 Normal + addWeighted | 组内先合成到透明缓冲，再按组 blend/opacity 一次合成（Pass Through 需另分支） |
| LYR-23 | P1 | ImageIOController.cpp:68-111 | data-loss | 保存后再打开只剩一张扁平图；图层/蒙版/智能对象全丢 | 保存写 `m_host->currentImage()`，无任何项目格式 | 定义项目格式（JSON + 分层 PNG），save/saveAs 走项目保存 |
| LYR-24 | P1 | ImageWorkspace.cpp:72-86 | logic | `IWorkspace::save()` 永远成功；`saveAs(path)` 忽略 path | 转发到 void 的 `savePublic()/saveAsPublic()`（后者内部再弹对话框） | 让 ImageIOController 返回 `bool/Result`，`saveAs` 接路径 |
| LYR-25 | P1 | LayerPanel.cpp:161-172、897-1009 | ux | 图层不能拖拽排序/入组/出组；组内层无法任何操作 | 未配置拖放；`onContextMenu` 遇到 child item 直接 `return` | `setDragDropMode(InternalMove)` + 重写 drop 映射到 moveTo；补组内项操作 |
| LYR-26 | P1 | LayerStack.cpp:1755-1770；TileCache.cpp:9-14；RenderGraph.h:126 | memory | 打开 24MP×多图层文档时内存涨到 GB 级 | 每次 render 重建 DAG；`TileCache` 无上限，缓存「节点数 × 画布」份 tile | 复用 RenderGraph；TileCache 设上限/LRU；大图走分块增量 |
| LYR-27 | P1 | SmartObjectWatcher.cpp:33-47、49-68、157-183 | logic | 智能对象源改动有时不提示；同一源两个图层会互相取消监视；网络盘卡 UI | embedded 路径用 MD5 推导与实例路径不一致；path 为 key 覆盖；2s 轮询在 UI 线程 stat | 每图层独立 watch 项（LayerId 键）；路径取实际缓存路径；轮询移出 UI 线程 |
| LYR-28 | P1 | imagewindow.cpp:1328-1375、2916-2947 | state-sync | 编组/解组/智能对象变换后画面不刷新 | 这些槽只改模型，漏 `invalidateCurrentCache()+renderToView()` | 收敛到统一的 `applyLayerChange()` 收尾函数 |
| LYR-29 | P1 | imagewindow.cpp:1136-1160 | ux | 「新建图层」立刻用黑/灰盖住画面；新文字层直接黑屏 | Bitmap 新层用 `zeros`（不透明黑）；Text 层同 LYR-01 | 新层应为全透明（alpha 0） |
| LYR-30 | P1 | Layer.h:70；LayerStack.cpp:438-576 | logic | 「锁定」只是图标，仍可删除/合并/改名/改透明度 | `locked` 从未被读取 | 在各 mutator 统一 `if (l->locked) return false` |
| LYR-31 | P1 | LayerStack.cpp:1064-1110；LayerPanel.cpp:610-671 | ux | 智能滤镜以「?」行混在图层列表顶层，删除父层后残留 | filter 子层占用主栈下标且无归属显示 | 面板按 `parentSmartIndex` 缩进到父层下；删父层联动清理 |
| LYR-32 | P2 | BlendMode.cpp:302-308；LayerPanel.cpp:314-327 | logic | 选「色相/饱和度/颜色/明度」与「正常」表现一样 | 枚举存在但 dispatcher fallback Normal；菜单也没列出 | 实现 4 种 HSL 模式并加入菜单 |
| LYR-33 | P2 | BlendMode.cpp（全文件） | logic | PS 的 10+ 模式缺失 | 未实现 | 见 §3 清单 |
| LYR-34 | P2 | BlendMode.cpp:110-137、85-108 | logic | 柔光/叠加在中间调与 PS 有可见差异 | SoftLight 用 sqrt 统一近似；阈值用 128 而非 127.5 | 用 PS 分段公式 + float 归一化 |
| LYR-35 | P2 | BlendMode.cpp:20-27；LayerStack.cpp:684-705 | design | 蒙版边缘出现黑边、任何半透明都退化为「乘黑」 | 全链路 8UC3 无 alpha，透明度只能靠改颜色 | 改 BGRA 合成 |
| LYR-36 | P2 | BlendMode.cpp:50-277 | perf | 大图逐层混合明显卡顿（4K 图单层数百 ms） | 每像素 double 双循环，无 SIMD/并行；每层再 2 次全图分配 | 用 `cv::MatExpr`/LUT/并行 `cv::parallel_for_` |
| LYR-37 | P2 | LayerPanel.cpp:599-702、340-348 | perf | 图层多时每次增删都卡一下 | `rebuildList` 全量重建；每层对全尺寸图做 resize 缩略图；`m_thumbs` 缓存未被读取 | 增量插入 + 异步缩略图 + 真正使用缓存 |
| LYR-38 | P2 | LayerPanel.cpp:340-348 | ui-visual | 灰度/BGRA 图层缩略图颜色错乱 | 固定 `Format_BGR888` | 按 channels 分支 |
| LYR-39 | P2 | LayerStack.cpp:684-705、1544-1547 | memory | 每次渲染每层额外 ~5 份全图 float 缓冲 | `applyMask` 两次 `convertTo` + 中间 3 通道掩码 | 直接在 8U 上乘或预乘到 alpha |
| LYR-40 | P2 | LayerStack.cpp:178-197；Layer.h:139-157 | logic | 非法图层（Bitmap 无像素等）可以进栈 | `addLayer` 不调用 `isValid()`；全项目 `isValid` 零调用 | 入口校验并返回错误 |
| LYR-41 | P2 | imagewindow.cpp:1587-1600、1701-1733 | logic | LUT 复位/蒙版反相/密度/羽化不可撤销 | 这些槽没有 push 命令 | 统一走 `LayerMaskCommand`/新增 LutCommand |
| LYR-42 | P2 | imagewindow.cpp:1739-1752；LayerStack.cpp:763-775 | logic | 「添加矢量蒙版」必失败 | UI 传空 paths，`addVectorMask` 对空路径返回 false | 允许空矢量蒙版（后续 PenTool 填充） |
| LYR-43 | P2 | LayerStack.cpp:438-465、485-503 | state-sync | 删除/移动后高亮行指向别的层 | 删除时只在下越界时修 `m_selection`；移动不修 | 用 LayerId 表示选中 |
| LYR-44 | P2 | LayerStack.cpp:1261-1294；LayerStack.h:376-383 | design | 两套「组」概念并存（`LayerKind::Group` 与 `LayerGroup`+link 标记） | 旧 API 未删除，`groupLayers` 实际只是置 `isLinked` | 删除旧 group API 与 `LayerGroup` |
| LYR-45 | P2 | LayerPanel.cpp:278-292；imagewindow.cpp:848-950 | memory | 重复 loadFile 会再 new 一个 LayerPanel，旧面板留在 dock；bindStack 从未被调用 | 生命周期交给 dock，却又每次新建 | `loadFile` 复用/销毁旧面板，或调 `bindStack` |
| LYR-46 | P2 | LayerStack.cpp:288-299 | logic | 复制组后两个组共享同一批子图层（改一个影响两个） | `m_groups.insert(clone, it.value())` 复制的是 shared_ptr 向量 | 复制组时深拷贝子层 |
| LYR-47 | P2 | LayerStack.cpp:1743-1753、LayerStack.h:325-327；LayerStack.h:37-38、272-276 | design | 注释与实现矛盾（默认 OFF/ON、callback 保活承诺、Mat clone） | 长期迭代未同步注释 | 清理过期注释，改为单一事实来源 |
| LYR-48 | P2 | imageworker/CMakeLists.txt:13-39；media/CMakeLists.txt:278-284 | build | 图层蒙版实现编在 `vistella_media` 而非 imageworker；源码树有 4 个 `.bak32` | 历史迁移未收尾 | 归位到 imageworker；删除 .bak32 |
| LYR-49 | P2 | LayerStack.cpp:60-107 | logic | 「智能滤镜」参数语义混乱（Contrast 用 alpha 乘法、Brightness 的 beta 与 PS 不符） | 5 个滤镜硬编码、强度映射各家不同 | 接入 FilterStrategy 统一 strength 语义 |
| LYR-50 | P3 | LayerPanel.cpp:46-67、628-663；LayerCommand.cpp:26-29 | i18n | 混合模式名、单位、命令名硬编码中文/英文混排，无 tr() | `QStringLiteral` 直接用 | 统一 `tr()` + 上下文 |
| LYR-51 | P3 | LayerStack.cpp:39、168-172、1261-1294 | design | `g_nextGroupId` 进程级全局（无锁/不随文档重置）；`disconnectLayer` 为空实现 | 历史残留 | 删死代码，group id 走文档内计数器 |

### LYR-01 文字/矢量图层丢弃 alpha → 整幅画布被黑底覆盖 (P0)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1389-1413`（Text）、`1414-1440`（Vector）；消费侧 `1648`、`src/media/imageworker/layers/BlendMode.cpp:20-27`
- **证据**：

```cpp
// text 层：透明底画字
QImage img(sz.width, sz.height, QImage::Format_ARGB32);
img.fill(Qt::transparent);
...
cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
cv::Mat bgra = mat.clone();
cv::Mat bgr;
cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);   // ← alpha 被丢弃，透明→黑
return bgr;
```

- **现象 / 复现路径**：任意打开一张彩色照片 → 图层面板「+ → 文字图层」→ 画面立刻整幅变黑，只剩白字；矢量图层同理。`tests/tst_LayerKindRender.cpp:269` 用 `Scalar(0,0,0)` 黑底做基线，恰好掩盖了这个缺陷。
- **根因**：整条合成链是 `CV_8UC3`，「透明」只能靠把颜色乘成黑；`alphaBlend` 在 `opacity>=1` 时直接 `return blend.clone()`，于是黑底覆盖下层。Text/Vector 的语义是「只有笔画可见」，但没有任何 alpha 承载信息。
- **修复建议**：①`Layer` 增加 `cv::Mat alpha`（或统一 BGRA）并在 `rasterize` 保留 alpha；②`applyBlend` 改 `dst = blend*srcA + dst*(1-srcA)` 的 premultiplied 合成；③在 `LayerBitmapNode/LayerRasterNode/LayerMaskNode/LayerBlendNode` 各增加 alpha 通路；④过渡期可用「透明色键」（把纯黑当 0）但会破坏正常黑像素，不可取。
- **关联**：蒙版黑边（LYR-35）、新建图层黑屏（LYR-29）同根。

### LYR-02 合并向下遇到非位图/异尺寸图层 → cv::Exception 崩溃 (P0)

- **位置**：`src/media/imageworker/layers/LayerCommand.cpp:81-91`、`src/media/imageworker/layers/LayerStack.cpp:1228-1230`
- **证据**：

```cpp
auto target = m_stack->at(mergeIndex - 1);
if (target) m_mergeBefore = *target;
auto src = m_stack->at(mergeIndex);
if (src) {
    cv::addWeighted(m_mergeBefore.image, 1.0f - a, src->image, a, 0.0, merged);
```

- **现象 / 复现路径**：背景层之上放一个文字层（或组、智能对象、调整层）→ 右键「向下合并」→ 程序崩溃（`CV_Assert` 抛 `cv::Exception`，Qt 槽内未捕获 → `std::terminate`）。任何一层尺寸与下层不同（粘贴/导入的图）同样崩溃。
- **根因**：命令构造期直接用 `Layer::image` 做算术，既没检查 `empty()` 也没检查尺寸/通道；`LayerStack::mergeDown` 里有同样问题。而 `LayerCommand` 构造发生在按钮回调（UI 线程栈）中，异常无处可去。
- **修复建议**：合并统一改为「渲染」语义：`cv::Mat upper = rasterize(l); if (upper.empty()) return false;`，并对尺寸不一致先 `resize` 到画布；`mergeDown` 与 Merge 命令共用一个 `mergeLayers(upper,lower)` 实现；命令构造内不抛异常。
- **关联**：LYR-20（合并结果错误同源）。

### LYR-03 合并向下 undo 不恢复上层 → 数据丢失 (P0)

- **位置**：`src/media/imageworker/layers/LayerCommand.cpp:464-474`（undo），调用点 `imagewindow.cpp:1284-1285`、`735-736`
- **证据**：

```cpp
case Merge: {
    // Phase 1 简化: 不知道 upper 的原 Layer (没存), 不可逆 undo
    //   简化: 仅恢复下层 image, 不重建上层
    if (auto target = m_stack->at(m_index - 1)) {
        target->image = m_mergeBefore.image.clone();
    }
```

- **现象**：合并两层后 Ctrl+Z：下层像素恢复，但上层图层**消失**（不复存在，也不在下层里）；再 undo 其它操作会把错误状态继续放大。
- **根因**：命令只保存了 `m_mergeBefore/m_mergeAfter`（下层），没有保存被吞掉的 `src` 层；也没有保存合并前的栈快照。
- **修复建议**：构造时保存 `Layer upperCopy = *src`（含 kind/mask/opacity/blend/name）与原始下标，undo 时 `insertLayerAt(m_index, upperCopy)` 并还原下层 image/flags；更彻底的做法是引入「图层栈快照事务命令」。
- **关联**：LYR-04、LYR-18（同类「undo 不完整」）。

### LYR-04 「拼合图像」不可逆 / 面板入口无撤销 (P0)

- **位置**：`src/media/imagewindow.cpp:745-767`（菜单）、`1299-1320`（面板按钮）；`LayerStack.cpp:1243-1255`
- **证据**：

```cpp
cv::Mat m = m_layerStack->flattenVisible();   // 内部 clear() → 除 Background 外全删
...
if (m_undoStack) {
    m_undoStack->push(new ImageEditCommand(this, backup, m_current.clone(), tr("拼合图像")));
}
```

- **现象**：拼合后撤销 → 只剩一张图（图层数变 1），所有图层/组/智能对象丢失；从图层面板「flatten」按钮触发时连这条无效的撤销记录都没有，Ctrl+Z 无任何反应。
- **根因**：`flattenVisible()` 直接在模型上 `clear()`；撤销栈只拿到「图像级」命令，无法重建图层集合。
- **修复建议**：把拼合实现为 `LayerSetCommand`（保存 `std::vector<LayerPtr>` + `m_groups` 快照）或「事务式」命令；面板入口必须与菜单入口走同一函数。
- **关联**：LYR-12（`m_groups` 未清）。

### LYR-05 编组按钮把整栈打包 → 画布退化为 800×600 (P0)

- **位置**：`src/media/imageworker/layers/LayerPanel.cpp:856-861`；`LayerStack.cpp:316-355`（mergeIntoGroup）、`1344-1352`（canvasSize）、`1563-1570`（base 搜索）
- **证据**：

```cpp
void LayerPanel::onGroupClicked()
{
    // Phase 1 简化: group 整个 stack (标记 link)
    emit groupRequested(0, m_stack->count() - 1);
}
...
// LayerStack::mergeIntoGroup：删原件（含 base），把 Group 追加到末尾
for (int i = sorted.size() - 1; i >= 0; --i) { removeLayer(sorted[i]); }
Layer grp; grp.kind = Layer::Group; ...; const int groupIdx = addLayer(grp);
```

- **现象 / 复现路径**：打开任意 1920×1080 照片 → 图层面板点「group」→ 画面变成 800×600 白底（遗留路径）或只剩左上 800×600 区域（compositor 路径）；再取消组也回不到原始画布。
- **根因**：编组后栈里只剩一个 Group 层，`m_layers[0]->image` 为空 → `canvasSize()` 回落到 `m_canvasSize`（默认 800×600）；base 搜索也失败（无 Bitmap 顶层）→ 白底画布。编组 API 还强制「连续且 ≥2 个」，工具条却总是传全栈。
- **修复建议**：①`canvasSize` 改为文档级状态（创建文档时固定，随「图像大小」命令变更）；②编组按钮按当前选区范围编组，且不允许把 base 层编入（或编组后仍以 base 为画布根）；③`mergeIntoGroup` 用 `LayerPtr` 组容器表达嵌套而不是删掉 base。
- **关联**：LYR-14、LYR-22（组语义）、LYR-28（编组后不重绘）。

### LYR-06 Remove 的 redo 用像素指针反查 → 删错图层 (P0)

- **位置**：`src/media/imageworker/layers/LayerCommand.cpp:677-687`
- **证据**：

```cpp
case Remove: {
    for (int i = 0; i < m_stack->count(); ++i) {
        auto l = m_stack->at(i);
        if (l && l.get() && m_layer.image.data == l->image.data) {
            m_stack->removeLayer(i);
            break;
        }
    }
```

- **现象**：①两个文字层（都没 `image`，`data == nullptr`）→ 删上面那个、undo、redo → 被删掉的是**下面**那个；②`duplicateLayer` 后两个 Bitmap 共享同一 `image.data` → 删除副本再 redo，删掉原件。随后 `Remove::undo` 把层追加到末尾，顺序彻底错乱。
- **根因**：把 `cv::Mat::data` 当身份标识；空 Mat 都相等，共享缓冲也相等。
- **修复建议**：`LayerStack` 维护稳定的 `LayerId`（自增 id 而非 `LayerPtr.get()`），命令保存 `LayerId`；`removeLayer` 提供 `removeById`。
- **关联**：LYR-19（浅拷贝使指针相等）。

### LYR-07 undo 只改模型不发信号 → 撤销视觉无效 (P0)

- **位置**：`src/media/imageworker/layers/LayerCommand.cpp:432-463`（Opacity/Visible/Locked/Blend/Rename）、`503-657`（per-kind），对照 `src/media/imagewindow.cpp:566`（`indexChanged` 只连 `onStackChanged`）
- **证据**：

```cpp
case Opacity: { auto l = m_stack->at(m_index); if (l) l->opacity = m_floatVal; break; }
case Visible: { auto l = m_stack->at(m_index); if (l) l->visible = m_boolVal; break; }
```

- **现象**：改不透明度 100→50 → Ctrl+Z：面板数字、画面都保持 50%；要等下一次任意操作强制重绘才「突然」变成 100%。隐藏图层撤销同理（图层一直不可见）。`LayerPanel::onLayerChanged` 也不会被触发，行内文案不刷新。
- **根因**：命令绕过 `LayerStack::setXxx()`（唯一发 `layerChanged` 的入口），且 `ImageWindow` 没有监听 `QUndoStack::indexChanged` 做重新渲染。
- **修复建议**：undo/redo 内改调 setter（`m_stack->setOpacity(m_index, old)` 等），setter 已带 signal；并在 `ImageWindow::onStackChanged()` 里加 `invalidateCurrentCache(); renderToView();` 兜底。
- **关联**：LYR-08、LYR-28。

### LYR-08 除 Merge 外所有 redo 都是 no-op → 重做失效 (P0)

- **位置**：`src/media/imageworker/layers/LayerCommand.cpp:661-782`（`case Add/Move/Opacity/Visible/Locked/Linked/Blend/Rename/Group/Ungroup/SetText/SetTextFont/SetSmartObject/SetAdjustmentType/SetAdjustmentLut/AddMask/ClearMask/EnableMask/ToggleSmartObjectEmbed/ConvertToSmartObject/RasterizeSmartObject/SetSmartObjectTransform/AppendSmartFilter/RemoveSmartFilter/MoveSmartFilter/SetSmartFilterEnabled/SetSmartFilterStrength` 全是 `break;`）
- **证据**：

```cpp
case Visible: {
    // P2.5 bug fix (2026-09-22): redo is a no-op. Caller did
    //   setVisible(newValue) before pushing; ...
    break;
}
```

- **现象**：任何图层操作 undo 之后按 Ctrl+Shift+Z 均无反应（只有 Merge 会再次合并，反而把下一层也吞掉）。撤销栈的 redo 分支在 UI 上是死按钮。
- **根因**：把「push 时调用方已经应用」推广成了「redo 永远不用实现」，从而抹掉了 redo 的语义；命令里根本没有保存 new 值（例如 `Opacity` 只有 `m_floatVal=old`）。
- **修复建议**：命令改为标准 `(before, after)` 双状态；`redo()` 应用 after，`undo()` 应用 before，push 前由调用方只负责构造（或首个 redo 由 `QUndoStack::push` 自然执行，调用方不再自行应用）。
- **关联**：LYR-03（Merge 仅存的 redo 也是错的）、LYR-07。

### LYR-09 renderAsync 无锁并发读 m_layers（后台线程 vs UI 线程）(P0)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1783-1808`；调用与阈值 `src/media/imagewindow.cpp:1964-1971`
- **证据**：

```cpp
const LayerStack *self = this;
const bool submitted = engine->background().submit_fn(
    [self, cb, ctx](vistella::tp::TaskContext &) -> void {
        cv::Mat result = self->render();       // ← 后台线程遍历/读取 m_layers、m_groups
```

- **现象**：大图（>1024×1024）首次渲染（>50ms）期间继续点图层（切换可见性/删层/拖不透明度）→ 概率性崩溃、图层错乱、或渲染结果张冠李戴。日志里表现为 `QList`/`shared_ptr` 相关的随机崩溃。
- **根因**：`render()` 是 `const` 但只是语法糖，内部遍历 `m_layers`、`m_groups`、`Layer::*`；UI 线程随时 `addLayer/removeLayer/swapItemsAt`。`QList` 隐式共享在并发读写时会 detach/释放旧 buffer，后台线程会踩到已释放内存。项目注释（`LayerStack.h:266-276`）声称「self 指针在闭包里保活」只解决对象生命，不解决数据竞争。
- **修复建议**：①提交前在 UI 线程做**不可变快照**（`std::vector<LayerPtr>` + 所需字段深拷贝）交给后台；②或按文档粒度加 `QMutex`，所有 mutator 与 render 串行；③或干脆只用 compositor 的「单线程分块 + 定时器让出」避免后台线程（大图体验可用进度条补偿）。
- **关联**：LYR-10（生命周期）、LYR-26。

### LYR-10 异步渲染回调捕获 this，关闭文档时 UAF (P0)

- **位置**：`src/media/imagewindow.cpp:1965-1971`；析构 `579-608`（`m_currentTaskId` 初始化在 `300`，析构从不失效）
- **证据**：

```cpp
const uint64_t myTaskId = m_currentTaskId.fetch_add(1) + 1;
m_layerStack->renderAsync(m_engine.get(),
    [this, myTaskId](cv::Mat result) -> void {
        if (m_currentTaskId.load() != myTaskId) return;   // ← this 已析构时读的是野内存
        m_current = std::move(result);
        ...
```

- **现象**：大图渲染中直接关闭标签页（或关主窗口）→ 偶发崩溃（`m_currentTaskId` 读取野指针 / `m_current` 写野指针 / `renderToView` 调已删 widget）。
- **根因**：`LayerStack::renderAsync` 用 `QMetaObject::invokeMethod(ctx, lambda, QueuedConnection)`，队列项挂在 `QCoreApplication` 上；`ImageWindow` 析构时既没有 bump `m_currentTaskId`，也没有 `QPointer` 保护；`m_engine.reset()` 只能保证 worker 退出，不能撤销已入队的 main-thread 回调。
- **修复建议**：闭包捕获 `QPointer<ImageWindow> self`，进入时 `if (!self) return;`；析构里 `m_currentTaskId.fetch_add(1)` 作废在途任务；或给命令加 `QMetaObject::Connection` 并在析构 disconnect。
- **关联**：LYR-09。

### LYR-11 智能对象缓存/读取用 `toStdString()` → 非 ASCII 路径失败 (P0)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:928`（写）、`1465`（读）；路径生成 `904-915`、`1360-1374`
- **证据**：

```cpp
const QString cachePath = generateConvertSmartObjectCachePath();
if (!cv::imwrite(cachePath.toStdString(), l->image)) return false;
...
cv::Mat img = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
```

- **现象**：Windows 用户名含中文（`C:\Users\张三\AppData\...`）或智能对象源放在中文目录时：转换为智能对象失败（statusBar 提示）、智能对象显示为灰底红叉「MISSING」占位、栅格化失败。项目其他位置（`imagewindow.cpp:851`）已用 `toLocal8Bit()`，此处不一致。
- **根因**：OpenCV `imread/imwrite` 在 Windows 用 `fopen`/ANSI 路径，UTF-8 的 `toStdString()` 不匹配当前 ACP。
- **修复建议**：统一封装 `ImageIO::imread/imwrite(const QString&)`：用 `QFile` 读字节 + `cv::imdecode/imencode`（Qt 侧天然支持 Unicode），或 `toLocal8Bit()`；缓存目录与文件名保持纯 ASCII/哈希。
- **关联**：LYR-23（保存路径同样风险）。

### LYR-12 clear()/flatten 不清 m_groups → 泄漏 + 地址复用串组 (P0)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1688-1696`（clear）、`1243-1255`（flattenVisible）、`304-308`（releaseGroupChildren 只有 removeLayer 调）、`345`（`QHash<LayerId, …>`，`LayerId == LayerPtr.get()` 见 `LayerStack.h:44`）
- **证据**：

```cpp
void LayerStack::clear() {
    if (m_layers.isEmpty()) return;
    m_layers.clear();
    m_smartFilterChain.clear();
    m_selection = -1;
    // ← m_groups 未清
```

- **现象**：长期会话（反复「拼合图像」/`loadFile` 复用窗口）内存持续增长（每个组子树整份图层像素常驻）；极端情况下新建的 Group 层如果恰好分配到旧地址，会「继承」上一位主人的子图层，画面出现不属于它的内容。
- **根因**：`m_groups` 以裸地址为 key，对象死亡与表项清理没有强绑定；`clear()` 只清 `m_layers`。
- **修复建议**：`clear()`/`flattenVisible()` 遍历 `m_layers` 中 Group 层调 `releaseGroupChildren`，最后 `m_groups.clear()`；中期把 key 换成自增 `LayerId`（不随地址复用）。
- **关联**：LYR-05、LYR-46。

### LYR-13 隐藏 base 图层无效 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1560-1576`（canvas 初始化）、`1579-1581`（循环里才判 visible）；compositor 侧同病 `LayerStackAdapter.cpp:108-163`
- **证据**：

```cpp
for (int i = 0; i < m_layers.size(); ++i) {
    const auto &l = m_layers[i];
    if (l && l->kind == Layer::Bitmap && !l->image.empty()) {
        canvas = l->image.clone();          // ← 不看 visible
        baseIdx = i; break;
```

- **现象**：点掉背景层眼睛 → 画面毫无变化（该层仍作为画布出现）；再用「合并可见图层」也仍然把它算进去。
- **根因**：base 的处置方式特殊（直接当画布）而不经过 `applyBlend`，所以绕过了 `visible` 判断。DAG 路径刻意「镜像」了这个行为（注释自称 parity），因此两条路径一起错。
- **修复建议**：base 也走统一循环（`canvas = 白底/透明底`，base 作为普通图层参与 blend）；若担心性能，至少要 `if (!base->visible) canvas = 白底`。

### LYR-14 base 之下的图层被丢弃 + canvasSize() 只看 index 0 (P1)

- **位置**：`LayerStack.cpp:1579`（循环从 `max(0, baseIdx)`）、`1344-1352`（canvasSize）；DAG 侧 `LayerStackAdapter.cpp:381-396`、`LayerRasterNode.cpp:63-80`
- **证据**：

```cpp
for (int i = std::max(0, baseIdx); i < m_layers.size(); ++i) {   // baseIdx 之前的层永不参与
...
cv::Size LayerStack::canvasSize() const {
    if (!m_layers.isEmpty() && m_layers[0] && !m_layers[0]->image.empty())
        return m_layers[0]->image.size();
    return m_canvasSize;    // 默认 800x600
```

- **现象**：①`[Background(位图), Text]` 文档删掉 Background → `m_layers[0]` 变成无像素的 Text → `canvasSize()` 回落到 800×600，整个文档的画布几何被静默改小（不可撤销）；②只要「第一个有像素的图层」不在 index 0，之后新增的文字/矢量/智能对象都按 800×600 栅格化，而画布仍是真实尺寸 → 内容尺寸错位；③`baseIdx` 之前的图层在两条路径里都被整体丢弃（`LayerStackAdapter.cpp:392-395` 也照抄了这条规则）。
- **根因**：画布尺寸没有独立状态，靠「第一个有图的层」推断；base 语义是「第一个 Bitmap」而不是「文档背景」。
- **修复建议**：`ImageWindow`/文档对象持有 `canvasSize` 并 `setCanvasSize`（该 API 目前全项目零调用），渲染循环遍历全栈。

### LYR-15 蒙版双状态：新 LayerMask 不参与渲染 (P1)

- **位置**：渲染侧 `LayerStack.cpp:1644-1647`、`1518-1549`；写入侧 `LayerStack.cpp:749-795`；`LayerMaskRenderer.cpp` 生产调用者为零（仅 `tests/tst_LayerMaskStruct.cpp`）
- **证据**：

```cpp
// render(): 只认老字段
if (l->maskEnabled && !l->layerMask.empty()) { layerMat = applyMask(layerMat, l->layerMask); }
// 而 UI/MaskBrushTool 写的是新结构
l->mask.kind = LayerMask::Pixel; l->mask.pixel = grayMask.clone(); l->mask.enabled = true;
```

- **现象**：图层面板「加蒙版」/MaskBrush 涂黑/「反相蒙版」/「羽化」/「密度」都没有任何画面变化；「应用蒙版」却能把黑色烧进像素（`imagewindow.cpp:2237-2285`，硬阈值 <128 → 0）。用户会觉得蒙版功能「时灵时不灵」。
- **根因**：P1.3.2 新增 `LayerMask` 结构后，渲染与面板各写一套；`LayerMaskRenderer`（唯一的正确实现）只被测试调用。
- **修复建议**：删除老 `layerMask/maskEnabled`，渲染侧调 `LayerMaskRenderer::apply`（或让 compositor 的 `MaskBitmapNode` 消费 `l->mask`），并在 `LayerPanel::syncMaskProps` 同步新结构。

### LYR-16 智能滤镜链用 int 下标，增删移动不重键 (P1)

- **位置**：`LayerStack.h:337`（`QHash<int, QList<int>>`）、`LayerStack.cpp:438-465`（removeLayer 不清理，注释明说）、`485-503`（move 不重键）、`1064-1110`
- **证据**：

```cpp
// LayerStack.cpp:441-446
//   SmartFilter chain bookkeeping is intentionally NOT done here. ...
//   Doing chain cleanup here would double-mutate and leave stale indices.
```

- **现象**：两个智能对象各带滤镜时，删除/上移其中一个 → 另一个的滤镜突然作用到错误图层，或滤镜消失；`parentSmartIndex` 也停留在旧下标。
- **根因**：链、父指针都用「栈下标」这一易变标识；任何 `insert/remove/swap` 都会让全部键漂移。
- **修复建议**：`SmartFilter` 子层直接挂在父 `Layer`（`std::vector<LayerPtr>`）而不是主栈；若必须留在主栈，键改为 `LayerId` 并在每个 mutator 里重算。

### LYR-17 AppendSmartFilter 两个调用点参数语义相反 (P1)

- **位置**：`src/app/mainwindow.cpp:2982-2984`（传 `newIdx` = filter 下标）vs `src/media/imagewindow.cpp:2170-2172`（传 `idx` = 父 SmartObject 下标）；消费侧 `LayerCommand.cpp:612-626`
- **证据**：

```cpp
// mainwindow.cpp
undo->push(layers::LayerCommand::makeAppendSmartFilter(stack, newIdx, picked, 1.0));
// imagewindow.cpp
stack->push(sfCmd);   // 其中 makeAppendSmartFilter(m_layerStack.get(), idx, ...)
// undo 侧
auto l = m_stack->at(m_index);
if (l && l->kind == Layer::SmartFilter && l->parentSmartIndex >= 0)
    m_stack->removeSmartFilter(l->parentSmartIndex, m_index);
```

- **现象**：「滤镜 → 转换为智能滤镜」这条链 undo 无效（`at(parentIdx)` 是 SmartObject，kind 检查失败，直接不删）；只有菜单「应用智能滤镜」的 undo 生效，且下标漂移后又失效。
- **根因**：同一工厂参数含义未文档化，「filterIdx」在两处被理解成不同对象的索引。
- **修复建议**：工厂改签名为 `makeAppendSmartFilter(stack, parentLayerId, filterLayerId)`；undo 按 id 查找。

### LYR-18 Group/Ungroup undo 用裸下标 (P1)

- **位置**：`src/media/imageworker/layers/LayerCommand.cpp:475-502`（`flattenGroup(m_index)`；`for (i) removeLayer(m_flattenAtIndex)` + `reinsertGroup`）
- **证据**：

```cpp
if (m_stack && m_flattenChildren.empty() == false) {
    for (int i = 0; i < n; ++i) {
        if (m_flattenAtIndex >= m_stack->count()) break;
        m_stack->removeLayer(m_flattenAtIndex);   // ← 若区间内容已被改动，删的是别的层
    }
```

- **现象**：编组 → 新建图层 → 撤销编组：删除的是「下标区间内现在的图层」，可能是新建的那个；`m_flattenChildren` 在 undo 里被 `std::move`/`clear`，第二次 undo 无操作。
- **根因**：命令用「位置」而非「内容身份」定位；且 undo 破坏自身状态（不可重复 undo）。
- **修复建议**：命令持 `std::vector<LayerPtr>` 快照 + 前后栈全量重放；或每次 undo 前按 LayerId 校验目标，失败则整栈替换。

### LYR-19 duplicateLayer 浅拷贝 + 注释与实现相反 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:467-483`；`Layer.h:128-132`
- **证据**：

```cpp
// Layer.h
// 拷贝 / 移动 (默认 cv::Mat 用 clone 走值语义, QString 走值语义)
Layer(const Layer &) = default;
// LayerStack.cpp:470
Layer copy = *l;   // 拷贝构造 (cv::Mat clone, QString 值语义)  ← 实际是浅拷贝
```

- **现象**：复制图层不额外占内存（好），但任何「原地像素写」都会同时改到原件与 undo 快照。当前多数路径用「新 Mat 替换」侥幸绕开，但 `LayerPtr->image.at<>()=` 形式的代码一出现就是隐性数据损坏；也是 LYR-06 的直接诱因。
- **根因**：`cv::Mat` 拷贝是引用计数浅拷贝，`= default` 无法实现注释宣称的值语义。
- **修复建议**：明确约定「Layer 像素不可变，写时整 Mat 替换」并写进头注释；`duplicateLayer` 若语义是深拷贝则显式 `copy.image = l->image.clone()`（配合内存预算）。

### LYR-20 mergeDown 丢 blend/mask，透明度双计 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1218-1241`
- **证据**：

```cpp
const float a = upper->opacity;
cv::addWeighted(lower->image, 1.0 - a, upper->image, a, 0, merged);
lower->image = merged;    // lower 自己的 opacity/blend/mask 仍保留
```

- **现象**：Multiply 的上层合并后变成 Normal 相加；带蒙版的上层合并后蒙版区被算进去；下层 opacity=50% 时合并结果再被 50% 压暗（双重应用）。
- **根因**：合并没有复用渲染语义（`rasterize + applyMask + applyBlend`），而是用「相加」近似。
- **修复建议**：合并 = 把「上层在该画布区域的最终外观」烘焙进下层：`rasterize(upper) → applyMask → applyBlend(lower.blend, …)`，并把下层 opacity 重置为 1、blend 重置 Normal。

### LYR-21 两条渲染路径在「层尺寸 ≠ 画布」时行为不同 (P1)

- **位置**：遗留侧 `BlendMode.cpp:283-287`；DAG 侧 `LayerBitmapNode.cpp:38-57`、`LayerBlendNode.cpp:29-64`
- **证据**：

```cpp
// BlendMode.cpp
if (!canBlend(base, blend)) { return base.empty() ? cv::Mat() : base.clone(); }  // 静默丢弃图层
// LayerBlendNode.cpp fitToSize(): 小图 → 补黑；大图 → 左上裁剪
```

- **现象**：①已可复现：编组把 base 吞掉后（LYR-05），遗留路径把组内 1920×1080 位图**整层丢弃**（`canBlend` 尺寸不等），DAG 路径却把它**左上裁剪**后画在 800×600 画布上——同一操作两种画面；②潜在：任何尺寸 ≠ base 的位图层（当前 UI 路径多会跟随画布尺寸，`[待确认]` 粘贴/置入是否会产生异尺寸层）都会出现「一条路径丢层、一条路径黑框补齐」。`tst_P8_1_1` 只覆盖等尺寸矩阵，测不出这类差异。
- **根因**：尺寸归一化职责缺失——`rasterize(Bitmap)` 直接返回层自身尺寸（`LayerStack.cpp:1386-1388`），两条路径各自兜底：遗留路径靠 `canBlend` 静默返回 base（丢层），DAG 路径靠 `fitToSize` 补零/裁剪（黑框）。
- **修复建议**：`rasterize` 对 Bitmap 也统一 resize/letterbox 到 `canvasSize()`（或引入「图层变换矩形」），并在 `applyBlend` 里对不匹配尺寸显式报错/记日志，禁止静默丢弃。

### LYR-22 组不隔离、组 blend 被忽略 (P1)

- **位置**：`LayerStack.cpp:1518-1549`（`compositeChildrenInto`）、`1590-1603`（顶层组分支）；`GroupNode.cpp:35-58`
- **证据**：

```cpp
cv::Mat groupMat = canvas.clone();          // 子层直接在“背景+已合成结果”上迭代
compositeChildrenInto(children, groupMat);
target = cv::addWeighted(target, 1.f - child->opacity, groupMat, child->opacity, 0, tmp);
```

- **现象**：组内放一个 Multiply 图层、背景是亮色：Multiply 直接与背景相乘（PS 中组默认 Pass Through 才如此；设为 Normal 组时应与组外隔离）。组的混合模式下拉能改但无任何效果；组内调整层/智能滤镜被静默忽略。
- **根因**：没有「组独立缓冲 + 组 blend」的模型；且把「组 blend mode 忽略」错误归因为「PS 也这样」。
- **修复建议**：组内先合成到透明缓冲（`alpha` 承载），再按组 `blend/opacity` 与下层合成；显式实现 Pass Through（默认）与 Normal/其它模式两种分支。

### LYR-23 图层状态零序列化 (P1)

- **位置**：`src/media/imagewindow/ImageIOController.cpp:68-111`（`onSave`/`onSaveAs`）；`src/app/mainwindow.cpp:3197-3215`（`saveSession` 只存路径）
- **证据**：

```cpp
if (!ImageProcessor::saveImage(m_host->currentImage(), fp, &err)) { ... }   // 只写扁平 m_current
```

- **现象**：存盘再打开：图层数变 1、智能对象/蒙版/组/文字层全丢；会话恢复也只能恢复文件路径。这是「非破坏编辑」产品的根本性缺口。
- **根因**：无项目文件格式，`LayerStack` 没有任何 `toJson/fromJson`。
- **修复建议**：定义 `.vistella` 项目（JSON 描述图层树 + 每层 PNG/WebP 载荷 + 蒙版 + 智能对象引用）；`LayerStack::serialize/deserialize` + `save/saveAs` 分流「项目保存 / 导出扁平图」。

### LYR-24 ImageWorkspace::save/saveAs 契约破损 (P1)

- **位置**：`src/media/imageworker/ImageWorkspace.cpp:72-86`
- **证据**：

```cpp
bool ImageWorkspace::save() {
    m_imageWindow->savePublic();
    return true;              // ← 永远成功
}
bool ImageWorkspace::saveAs(const QString &path) {
    m_imageWindow->saveAsPublic();          // ← 忽略 path，内部另弹对话框
    return m_imageWindow->filePath() == path;
}
```

- **现象**：`IWorkspace` 的调用方（MediaDispatcher/MainWindow 的批量保存、关闭前保存、自动保存）会得到「保存成功」的假象，用户随后关闭导致丢稿。
- **根因**：`ImageIOController::onSave/onSaveAs` 返回 void，且 `saveAs` 写成对话框。
- **修复建议**：`ImageIOController::save(path*)` 返回 `bool/Result`；`ImageWorkspace::saveAs(path)` 直传路径；失败时 `save()` 返回 false 并给出原因。

### LYR-25 LayerPanel 无拖拽排序、组子项完全只读 (P1)

- **位置**：`src/media/imageworker/layers/LayerPanel.cpp:161-172`（`m_tree` 只设了 `customContextMenuRequested`/`currentItemChanged`）、`897-909`（child item 直接 return）
- **证据**：

```cpp
m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &LayerPanel::onContextMenu);
...
const QVariant data = item->data(0, Qt::UserRole);
if (data.type() != QVariant::Int) return;    // 组子项无右键菜单
```

- **现象**：无法用拖拽调整顺序、拖入/拖出组（PS 最常用交互之一）；组内图层看不到右键菜单，也不能显示/隐藏/重命名/删除；只能通过「组整栈」+「解组」这种破坏性方式操作组。
- **根因**：`QTreeWidget` 的拖放与 `Qt::ItemIsDragEnabled/DropEnabled` 未启用；数据模型层也没有 `moveLayerIntoGroup(from, groupId, pos)` 之类 API（`mergeIntoGroup` 只接受连续下标）。
- **修复建议**：启用 `InternalMove` + 重写 `dropEvent`（或自定义 `QAbstractItemModel` 后用 `QTreeView`），把 drop 映射为 `LayerStack::moveTo(from, parentId, pos)`；补齐组内层的右键菜单（复用顶层菜单逻辑）。

### LYR-26 renderViaCompositor 每帧重建 DAG + 无界 TileCache → 内存峰值 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1755-1770`；`src/media/compositor/TileCache.cpp:9-14`；`RenderGraph.h:126`
- **证据**：

```cpp
compositor::LayerStackAdapter::Result built = LayerStackAdapter::build(...);   // 每次 render 新建全部节点
compositor::TileGrid grid(built.canvasSize.width, built.canvasSize.height);
compositor::RenderGraph rg(&built.graph, built.outputNode, &grid);            // 私有 TileCache 无上限
return rg.execute();
// TileCache::put: m_tiles.insert(k, tile.clone());
```

- **现象**：4000×3000 文档、20 个位图层：遗留路径峰值约 4~6 份全图（~200MB）；DAG 路径约 (2×20+α) 个节点 × 全图 tile 缓存 ≈ 1.4GB+，打开/拖滑块即内存暴涨，长时间会话不释放。相同时刻 UI 线程独占（`execute()` 无 engine → 串行）。
- **根因**：图/缓存本应跨调用复用（`markDirty` 机制已具备、注释也写了「cache persists across calls」），但 `renderViaCompositor` 把 `RenderGraph` 建在栈上，每帧从零开始；`put` 还额外 `clone` 一份。
- **修复建议**：把 `CompositorGraph`/`RenderGraph`/`TileCache` 提升为 `LayerStack` 成员并复用，图层属性变更走 `markDirty`；TileCache 加 LRU 上限（如 512MB）；图/缓存常驻后再启用 `execute(engine)` 并行。

### LYR-27 SmartObjectWatcher 双键与路径推导缺陷 (P1)

- **位置**：`src/media/imageworker/layers/SmartObjectWatcher.cpp:33-47`（路径推导）、`49-68`（path 为 key）、`150-155`（目录监视从未注册）、`157-183`（UI 线程轮询）
- **证据**：

```cpp
QString p = l->sourceFilePath;
if (l->sourceEmbedded) p = LayerStack::smartObjectCachePathFor(p);   // MD5 派生
...
m_pathToEntry.insert(absPath, e); m_indexToPath.insert(layerIndex, absPath);
```

- **现象**：①`convertToSmartObject` 把 cache 路径**直接**写进 `sourceFilePath`（`LayerStack.cpp:927-930` 的注释也承认），这里再 MD5 一次 → 监视一个不存在的文件 → 外部修改永不提示；②同一源文件被两个图层引用：`addWatch` 用 path 覆盖旧 entry 并 `m_indexToPath.remove(oldIdx)`，随后任一层 unwatch 会删掉另一层的监视；③`addPath` 只加文件不加目录，`onDirectoryChanged` 是死代码；④每 2s 在 UI 线程对每个受监视路径 `QFileInfo::lastModified()`，网络盘/休眠盘会卡顿。
- **根因**：把「路径」和「图层下标」混在一个双向表里，两者都不是稳定身份；路径推导有两套规则。
- **修复建议**：`QHash<LayerId, WatchItem{path, mtime}>`；路径统一由 `LayerStack::effectiveSourcePath(layer)` 提供（embed 时返回真实缓存文件）；轮询移到 `QTimer` + 后台线程（或改用 `QFileSystemWatcher` + 低频校验）；目录监视加上父目录。

### LYR-28 编组/解组/智能对象变换后不刷新 (P1)

- **位置**：`src/media/imagewindow.cpp:1328-1375`（group/ungroup 槽）、`2916-2947`（`applySmartObjectTransform` 只 `invalidateCurrentCache()`）
- **证据**：

```cpp
m_undoStack->push(layers::LayerCommand::makeMergeIntoGroup(m_layerStack.get(), indices));
statusBar()->showMessage(tr("已编组 ..."), 3000);        // ← 没有 invalidateCurrentCache/renderToView
```

- **现象**：点击 group/ungroup 后画面不动（直到下一次触发重绘）；智能对象旋转/缩放同理（`invalidateCurrentCache` 只标脏，不触发 `renderToView`）。
- **根因**：操作收尾不统一，40+ 个面板槽各自手写 `invalidate+render`，漏写就出 bug。
- **修复建议**：收敛为 `ImageWindow::commitLayerChange()`（invalidate + render + 面板刷新 + dirty），所有槽末尾调用一次。

### LYR-29 新建图层语义错误（黑层/黑底） (P1)

- **位置**：`src/media/imagewindow.cpp:1136-1145`（面板新建 Bitmap 用 `zeros`，Text 见 `1154-1160`）
- **证据**：

```cpp
cv::Mat m = m_current.empty() ? cv::Mat(64, 64, CV_8UC3, cv::Scalar(200, 200, 200))
                              : cv::Mat::zeros(m_current.size(), CV_8UC3);   // 不透明黑
newIdx = m_layerStack->addLayer(QStringLiteral("Bitmap %1")..., m);
```

- **现象**：Ctrl+Shift+N 新建图层后画面立刻变黑（面板入口）或毫无变化（菜单入口，`applyLayerOp` 传空 `cv::Mat`，见 `imagewindow.cpp:652-661`）——两条入口行为还不同。
- **根因**：没有 alpha 通道，新层无法表达「透明」；两个入口用了不同占位。
- **修复建议**：随 LYR-01 改造为 BGRA 后新层填 `(0,0,0,0)`；短期至少把两个入口统一，并在面板用斜线图标提示「空层」。

### LYR-30 锁定标志从不生效 (P1)

- **位置**：`Layer.h:70`（`locked` 注释承诺「rename/opacity/move 都不能」）；`LayerStack.cpp:438-576` 全部 mutator 无检查；`LayerPanel.cpp:811-849` 按钮不检查
- **证据**：

```cpp
bool    locked  = false;      // 锁定后不能编辑 (rename / opacity / move)
```

- **现象**：锁定一个图层后仍可删除/合并/改名/调不透明度/移动；「🔒」只是文案前缀（`LayerPanel.cpp:628`）。
- **根因**：标志只被读取用于显示，没有在业务层强制执行。
- **修复建议**：`LayerStack` 所有 mutator 首行 `if (l->locked) return false;`（并提供 `force` 变体给撤销/内部用）；面板按钮/菜单项按 locked 置灰。

### LYR-31 智能滤镜子层混在主栈、删除父层后链残留 (P1)

- **位置**：`LayerStack.cpp:1064-1090`（append 到 `m_layers` 末尾、名字 `Filter: xxx`）、`LayerPanel.cpp:610-671`（无归属缩进）、`LayerStack.cpp:438-465`（removeLayer 不清理链）
- **现象**：应用两个智能滤镜后，图层列表顶层出现两行「Filter: GaussianBlur」（图标「?」），用户无法把它们与父对象关联；删除父智能对象后这两行仍在（渲染时被跳过 → 存在但无效果），后续 `m_smartFilterChain` 里的下标还可能指向别人。
- **根因**：滤镜被建模成「主栈图层」而不是「父层的属性」。
- **修复建议**：见 LYR-16；面板按 `parentSmartIndex` 缩进显示为子项，并在删除父层时级联删除。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 图层树/组嵌套 | 任意嵌套、可折叠、拖入拖出 | 一层组（可再编组但会重复打包）、**无拖拽**、组子项只读 | 部分 | P1 |
| 组混合模式/Pass Through | 组有 blend mode，默认 Pass Through，可设 Normal 隔离 | 组 blend 被忽略；子层直接与背景混合 | 有但错 | P1 |
| 组不透明度 | 组整体作为单元参与合成 | `addWeighted` 近似（Normal 成立，其它模式不成立） | 部分 | P1 |
| 图层不透明度/显隐 | 实时、可撤销 | 可改，但 undo 后不刷新、隐藏 base 无效 | 有但错 | P0 |
| 混合模式数量 | 27 种 | 12 种；HSL 4 种声明未实现 | 部分 | P2 |
| Alpha / 透明 | 每层 alpha 通道 | 全链路 8UC3，无 alpha，蒙版/文字靠乘黑 | 缺失 | P0 |
| 图层蒙版 | 像素+矢量、密度/羽化/反相、实时 | 新结构存了但**不渲染**；只有「应用蒙版」破坏式烧像素 | 有但错 | P1 |
| 剪贴蒙版 / 图层样式 / 调整层参数面板 | 完整 | 无剪贴蒙版、无图层样式；调整层只有 256×1 LUT + 5 个名字 | 缺失 | P2 |
| 图层合并/拼合/盖印 | 全部可撤销 | 合并崩/undo 丢层；拼合不可撤销；无「盖印」 | 有但错 | P0 |
| 智能对象（嵌入/链接/变换/智能滤镜） | 双入口编辑、变换可累计、滤镜可反复调 | 有基本链路；无序列化、非 affine 变换被忽略、滤镜链下标漂移 | 部分 | P1 |
| 智能对象外部修改提示 | 自动重载/提示 | 有 watcher + 弹框，但路径推导/多引用有缺陷 | 部分 | P1 |
| 文字图层 | 可编辑、字体/行距/段落、可转换 | 单行文本、居中固定位置、无 alpha（黑底）、无变换 | 部分 | P1 |
| 矢量图层 | 形状+路径编辑+属性 | 只能显示 path（PenTool 外部写入），暂无编辑闭环 | 部分 | P2 |
| 图层序列化（PSD/项目文件） | PSD 完整往返 | **零序列化**，保存即扁平 | 缺失 | P0 |
| 图层搜索/过滤/颜色标签 | 有 | 无 | 缺失 | P3 |
| 撤销/重做 | 全操作可逆 | redo 全 no-op；合并/拼合不可逆 | 有但错 | P0 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 逐像素混合算子（27 种 PS 模式） | QT-NATIVE | `QPainter::CompositionMode` 已含 Multiply/Screen/Overlay/Darken/Lighten/ColorDodge/ColorBurn/HardLight/SoftLight/Difference/Exclusion 等；HSL 4 种 Qt 无直接对应 | OpenCV/自研补 4 种 HSL + 缺失模式；或改成 `QImage` 合成复用 Qt 算子（工作量 3~5 人天） |
| 图层 alpha / 蒙版正确合成 | QT-NATIVE | `QImage::Format_ARGB32_Premultiplied` + `QPainter` 天然支持 alpha 与 clip；本项目用 8UC3 自废武功 | 合成缓冲改 BGRA + premultiplied；`QPainter` 画文字/矢量直接带 alpha |
| 图层树拖拽排序/编组 | QT-NATIVE | `QTreeWidget::setDragDropMode(InternalMove)` + `dropEvent`/`QAbstractItemModel::moveRows` 即可 | 见 LYR-25；建议换 `QTreeView + QAbstractItemModel` 以获得稳定 id 与 O(1) 行更新 |
| 图层缩略图/图标 | QT-NATIVE | `QImage::scaled`+`QIcon`，或用 `QQuickWidget` 走 GPU | 异步生成 + 缓存 |
| 大图实时合成（GPU） | QT-3RD | 需要自建 GPU 合成管线；Qt 只给 `QOpenGLWidget`/`QRhi` 底座。当前 `RenderGraph::setBackend` 存在但图层路径从不设置（`renderViaCompositor` 未调用 `setBackend`） | 集成 **Skia (BSD)** 或 **Blend2D (Zlib)** 做 2D 合成，或基于 `QRhi` 自研（10~20 人天）。许可证友好 |
| 图层样式（阴影/描边/发光…） | QT-EFFORT | Qt 无现成实现；`QGraphicsDropShadowEffect` 只适合小控件、不适合逐层非破坏 | 自研图层样式栈（每样式一个 tile-safe 节点）约 15~25 人天 |
| PSD 读写 | QT-3RD | Qt 无 PSD 支持 | `psd_sdk`(MIT) / `libpsd`(BSD) 只读即可，写自有格式（10~15 人天） |
| 项目文件格式 | QT-EFFORT | `QJsonDocument` + `QImageWriter` 足够 | 自定义 `.vistella`：JSON 树 + 分层 PNG；工作量 5~8 人天 |
| 文件修改监视 | QT-NATIVE / QT-LIMIT | `QFileSystemWatcher` 在本地盘可靠；**SMB/网络盘在内核层不保证事件**，官方建议轮询，属平台限制 | 本地盘用 watcher，网络盘降级为低频轮询且必须离开 UI 线程 |
| Unicode 路径图像 IO | QT-NATIVE | `QImageReader/QImageWriter` 完全支持，`cv::imread` 不支持 | 统一封装 `ImageIO`（LYR-11） |
| 取消后台渲染 | QT-NATIVE | `QFutureInterface::cancel`/`QThreadPool` 无法强杀 `cv::Mat` 计算，但可在行/块边界轮询 `TaskContext`（当前参数被丢弃） | 分块渲染 + 每块检查取消标志（2~3 人天） |
| 内存映射大图 | QT-EFFORT | `QFile::map` 可做 tile 存储底座 | 分块 tile 池 + `QSharedMemory`/mmap 落盘（10 人天） |

## 5. 设计问题

- **渲染双路径 + 双蒙版 + 双分组**：`render()` 默认走 compositor，但遗留循环、`applyMask`、`LayerGroup/createGroup/groupLayers` 全部保留；`LayerMask` 与 `layerMask/maskEnabled` 并存。三处「两套实现」是行为不一致的温床（LYR-15/21/22/44）。**建议**：删除遗留路径与旧字段，用编译期开关只保留一个实现（迁移期用 tests 做像素级 parity）。
- **身份系统缺失**：图层身份 = `LayerPtr` 地址（`m_groups` key、`LayerId`）、命令身份 = int 下标、`Remove` 反查 = `cv::Mat::data`、智能滤镜 = int 下标。四种「身份」互相转换、互不同步，直接造成 LYR-06/12/16/18。**建议**：`Layer` 内建 `quint64 stableId`，所有外部引用（命令、组、滤镜、监视器、面板 item）一律用 id，下标只用于渲染顺序。
- **命令模型反了**：`QUndoCommand` 应当是 `(before, after) → redo 应用 after / undo 应用 before`；这里 30 余个 Op 只存 before，靠「调用方先应用」凑出 redo no-op，导致重做失效（LYR-08）且 undo 静默改模型（LYR-07）。**建议**：重写 `LayerCommand` 为单一泛型（`std::function<void()> apply/undo` + 快照），或直接采用 `QUndoStack` + 「整栈快照差分」。
- **渲染/通知耦合缺失**：模型变更与「重绘/面板刷新」由 40 多个 UI 槽手工配对，漏一处就出现「点了没反应」。**建议**：`LayerStack` 统一 emit `layerChanged(id, ChangeFlags, origin)`，`ImageWindow` 单点订阅 → invalidate+render；面板按 id 增量刷新。
- **内存策略缺失**：无图层数上限、无像素预算、`cv::Mat` 浅拷贝与深拷贝语义不明（Layer.h 注释与实际相反），compositor TileCache 无上限，智能对象缓存只「全删」不「引用计数」。**建议**：显式 `PixelBudget`（如 2GB）与超限降级（自动拼合提示/拒绝新建）；缓存加 LRU；`cleanupSmartObjectCache` 改为按引用扫描。
- **线程模型**：`LayerStack` 是 QObject 却在非所属线程被 `render()`；`EngineContext` 只被用来「扔任务」，任务内不检查 `TaskContext`；`m_currentTaskId` 只是「丢弃结果」不是取消。**建议**：明确「图层模型只能在 UI 线程访问」，后台只吃快照；引入可取消的分块渲染。
- **错误处理**：`addLayer/mergeIntoGroup/appendSmartFilter` 等用 `int/-1` 表示失败、`ImageWorkspace::save` 恒 true、`Layer::isValid` 从不调用、`applyBlend` 尺寸不符静默返回 base。**建议**：统一 `Result<T>`（项目已有 `core/Result.h`）并在 UI 显式提示。
- **分层与构建**：`LayerMaskCommand/LayerMaskRenderer` 编在 `vistella_media` 而不是 `vistella_imageworker`（`media/CMakeLists.txt:278-284`），导致 media 反向依赖 imageworker 内部实现；`LayerStack.cpp` 直接 include `compositor/*` 形成环状耦合（imageworker ↔ compositor）。**建议**：把合成接口抽象成 `ILayerRenderer`（由 compositor 注册），`imageworker` 只依赖接口。
- **可测试性**：`tests/tst_LayerKindRender.cpp:269` 用黑底基线让「文字黑底」缺陷变得不可见；`tst_P8_1_1` 只测等尺寸矩阵，测不出 LYR-21。**建议**：补「彩色底 + 文字层/小尺寸层/隐藏 base/组内混合」的像素级回归用例。

## 6. 修复建议（按优先级）

**P0 立即修（约 5~7 人天，风险中）**
1. 合并/拼合链路：`mergeDown` 改「渲染式合并」+ 完整 undo；`flattenVisible` 走整栈快照命令（LYR-02/03/04/20）。
2. `Remove` 命令改 `LayerId` 定位（LYR-06）；`Add::undo` 按 id 删（LYR-06 关联）。
3. 合并前对非 Bitmap/异尺寸层早退，避免异常逃逸（LYR-02）；所有槽外层加 `try/catch(cv::Exception)` 兜底。
4. `LayerCommand` undo/redo 末尾统一 `emit layerChanged + invalidate + render`，并实现真 redo（LYR-07/08）。
5. `renderAsync` 改快照 + `QPointer` 回调（LYR-09/10）。
6. `imread/imwrite` 统一 Unicode 封装（LYR-11）。
7. 修 `onGroupClicked` 与 `canvasSize`（LYR-05/14）。
8. `clear()` 清 `m_groups`（LYR-12）。

**P1 本轮修（约 10~15 人天）**
1. Alpha 通道改造（LYR-01/29/35）——牵动 `Layer/blend/compositor node/mask`，建议与「蒙版统一」一起做。
2. 蒙版统一到 `LayerMask` + `LayerMaskRenderer`，删除旧字段（LYR-15/41）。
3. 稳定 id 重构：`Layer::id`，组/滤镜/命令/监视器全部换 id（LYR-16/17/18/43）。
4. LYR-21 尺寸归一化 + 两条路径 parity 测试。
5. 组隔离/组 blend（LYR-22）。
6. 面板：拖放 + 组子项操作 + 增量刷新 + 缩略图异步（LYR-25/31/37）。
7. 图层面板/菜单漏掉的重绘收尾统一函数（LYR-28）。
8. `ImageWorkspace::save/saveAs` 返回真实结果（LYR-24）。
9. SmartObjectWatcher 改 id 键 + 真实路径（LYR-27）。
10. 锁定语义（LYR-30）。

**P2 后续**
- 实现 HSL 4 种 + 缺失的 PS 模式（LYR-32/33/34），并把 blend 逐像素循环向量化/并行化（LYR-36）。
- RenderGraph/TileCache 常驻 + LRU（LYR-26）。
- 项目文件序列化（LYR-23）——体量大，建议单独排期。
- 缩略图格式分支、selection 用 id、双分组概念清理（LYR-38/43/44）。

**架构级重构（20+ 人天）**
- 图层模型 v2：稳定 id + 组为真正容器（children 存 Layer 内）+ 每层 BGRA + 变换/裁切矩形 + 序列化。
- 渲染 v2：单一合成实现（compositor），tile 常驻缓存，可取消/并行，蒙版/组/filter 都是节点。
- 命令 v2：泛型快照命令 + 统一通知，撤销/重做全操作可逆。

## 7. 需要 Lead 协调的跨模块问题

- **compositor 模块**：`LayerStackAdapter` 刻意「镜像」遗留路径的 bug（隐藏 base、base 前置层丢弃、尺寸不一致）；任何修 LYR-13/14/21/22 都必须同步改 `LayerBitmapNode/LayerBlendNode/LayerRasterNode/GroupNode/LayerMaskNode`，否则出现「修好一条路径、另一条更错」。`tst_P8_1_1` 的 parity 断言需要一起更新。
- **imagewindow / mainwindow**：LYR-07/08/10/28 的收尾修复落在 `imagewindow.cpp` 的 40+ 个槽与 `applyLayerOp`；`ImageIOController` 需改签名（LYR-24）；`mainwindow.cpp:2983` 的参数语义（LYR-17）需与该模块审查员对齐归属。
- **tools 模块**：`MaskBrushTool/MaskBrushCommand` 写的是 `Layer::mask`（不渲染）；蒙版统一（LYR-15）必须同步改动这两处，以及 `masks::RefineEdge/ColorRange` 的下游调用。
- **构建**：`LayerMaskCommand/Renderer` 编码位置与 `.bak32` 文件（LYR-48）需要 CMake 负责人一起改；若采纳 LYR-01 的 BGRA 改造，`ImageProcessor` 全链路（滤镜/调整/画布显示）都要接受 4 通道，影响面最大。
- **依赖引入**：GPU 合成（Skia/Blend2D）与 PSD 读写（psd_sdk）需 Lead 决策（体积/许可证/构建复杂度），本模块只给评估。
- **可能与其他审查员冲突的结论**：`ImageProcessorAsync` 在本模块判定为「无生产调用者的死 facade」；若其它模块（filters/adjustment）另有调用请以他们的结论为准（本报告基于 `src/` 全量 grep：除自身与 tests 外无引用）。
