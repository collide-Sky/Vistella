# 08 · 渲染与滤镜（Compositor DAG / GPU 后端 / 滤镜 / 液化）— 审计报告

> 范围：`src/media/compositor/**`（9 + nodes 18 文件）、`src/media/gpu/**`（9 文件）、
> `src/media/filters/**`（FilterCommand / FilterDialog / FilterFactory / FilterStrategy）、
> `src/media/filters/liquify/**`（18 文件）；共 62 文件 / 6121 行（不含 `.bak`）
> 审查者：audit-render

## 1. 模块概览

- **入口与调用关系**：`ImageWindow::rebuildCurrentCache()`（imagewindow.cpp:1929）→
  `LayerStack::render()`（LayerStack.cpp:1551）。`render()` 默认走
  `renderViaCompositor()`（:1557）→ `LayerStackAdapter::build()` 每次重建
  `CompositorGraph` → `RenderGraph::execute()` 逐瓦片拉取节点输出、拼成整幅 canvas。
  `render()` 的大图分支走 `renderAsync()`（背景线程），但**该入口不把 engine 传给
  RenderGraph**，因此 `RenderGraph` 的并行/后端分发路径在生产中永不执行。
- **关键类**：`CompositorNode`（节点基类，`compute()` 返回一块瓦片）、`TileGrid/TileKey`
  （256×256 网格 + 缓存键）、`TileCache`（`QHash<TileKey, cv::Mat>` + QMutex）、
  `RenderGraph`（`requestNodeTile()` 递归求值 + 拼装 + 命中统计）、
  `LayerStackAdapter`（把 LayerStack 翻译成链式 DAG，节点所有权在 `Result`）。
- **GPU**：`BackendRegistry`（Auto/ForceGpu/ForceCpu + BrushKind 路由）→
  `CpuBackend`（clone）/`GpuBackend`（QOpenGLContext + offscreen surface + FBO + pass-through
  shader）。**`RenderGraph::setBackend()` 在 src/ 中零调用**，GPU 链路未接入渲染。
- **滤镜**：`FilterFactory`（49 个 `FilterKind` → Strategy）→ `FilterDialog`（非模态，
  Apply=实时预览、OK=入撤销栈）→ `FilterCommand`（存 before/after 两整幅图）。
  智能滤镜只在 SmartObject 上以「每次渲染重跑 filterType」的方式存在。
- **液化**：`LiquifyDialog` 拥有 `BackingStore`(snapshot+freeze mask+mesh+dirty rect)、
  `LiquifyEngine`（逐像素 inverse-warp 渲染）、`LiquifyCanvas`（鼠标→stamp→增量重绘）、
  `FaceDetector`（cv::dnn + third_party/landmark 硬编码绝对路径的 ONNX）。
- **依赖方向**：`layers/LayerStack` ↔ `compositor` 双向（LayerStack.cpp 反向 include
  compositor 头，adapter 又依赖 LayerStack 的 public rasterize API）；`filters` 直接依赖
  `ImageWindow`（`FilterCommand`/`LiquifyCommand` 反向依赖 UI 层）。

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| CP-01 | P0 | compositor/RenderGraph.cpp:68 | logic | 灰度图/带 alpha 的 PNG 打开后画布全黑 | canvas 固定 `CV_8UC3`，节点返回 1ch/4ch 时 tile 被静默丢弃 | 进出节点统一做格式归一（1/4ch→3ch），或 canvas 跟随 base 类型 |
| CP-02 | P1 | compositor/TileCache.cpp:13 | memory | 大图多图层一次渲染峰值数百 MB~GB | 每个节点每块瓦片 `clone()` 常驻、无上限无淘汰 | 加字节预算 LRU，或只缓存「跨瓦片复用」的节点 |
| CP-03 | P1 | layers/LayerStack.cpp:1768 | design | “增量合成器”完全不增量，每次渲染全量重算 | `RenderGraph`+`TileCache` 是函数内栈对象，随渲染销毁 | 把 graph/cache 提升为 ImageWindow 级成员，节点用稳定 ID |
| CP-04 | P1 | compositor/RenderGraph.cpp:143 | threading | 并行路径一旦提交失败/任务抛异常就**永久挂死** | 忽略 `submit_fn` 返回值；池内 `catch(...)` 吞异常；忙等无超时 | 改用 future/屏障 + 检查返回值 + 超时取消 |
| CP-05 | P1 | nodes/LayerRasterNode.h:96 | threading | 并行渲染下栅格化/蒙版记忆化数据竞争，可崩溃 | `compute()` 内改 `m_raster/m_rasterValid`，非线程安全 | 记忆化加锁或移到渲染前的 prepare 阶段 |
| CP-06 | P1 | nodes/LayerMaskNode.cpp:56 | memory | 就地写回上游缓存 tile，污染共享缓存；非 3ch 时抛 cv::Exception | `convertTo(base, CV_8UC3)` 命中同型同尺寸直接复用缓冲区 | 输出到独立 `cv::Mat`，永不改写 `requestNodeTile()` 返回值 |
| CP-07 | P1 | nodes/LayerMaskNode.cpp:47 | logic | 蒙版区域变黑而不是露出下层；Multiply 等模式会主动压暗下层 | 蒙版被实现为「颜色乘法」，整条管线无 per-pixel alpha | 引入 alpha/预乘合成（Porter-Duff over） |
| CP-08 | P1 | nodes/LayerBitmapNode.cpp:43 | logic | 小图层（小于画布）边缘出现随机色块 | `cv::Mat(rows, cols, type)` 不初始化，padding 未清零即返回 | 用 `cv::Mat(..., cv::Scalar::all(0))` |
| CP-09 | P1 | compositor/RenderGraph.cpp:225 | crash | 任一节点 cv::Exception 直接终止进程 | 渲染全链路无 try/catch，异常穿过 Qt 事件循环 | `requestNodeTile` 内捕获并降级为空 tile + 日志 |
| CP-10 | P1 | compositor/RenderGraph.cpp:203 | crash | 若图出现环，递归渲染栈溢出 | `topologicalOrder()/hasCycle()` 从未被 `execute()` 调用 | execute 前跑一次拓扑排序并拒绝环 |
| CP-11 | P1 | filters/FilterCommand.cpp:54 | data-loss | 多图层文档应用滤镜后，上方图层被重复合成；undo 无法还原 | 滤镜作用在合成图 `m_current` 上，结果又写回 base 层 | 滤镜只作用于活动图层像素，或走智能滤镜 |
| CP-12 | P1 | app/mainwindow.cpp:2582 | data-loss | Liquify 应用后红蓝通道互换 | `Format_RGB888` 数据被当成 `CV_8UC3`(BGR) 使用 | 包 cv::Mat 前做 `cvtColor(RGB2BGR)` 或直接 BGR 转换 |
| CP-13 | P1 | filters/FilterStrategy.cpp:266 | logic | 渐变映射输出随机噪点/全黑 | `cv::Mat(1,256,CV_8U)` 作为 LUT 但从未初始化 | 显式构造恒等/双色 LUT |
| CP-14 | P1 | media/imagewindow.cpp:1878 | state-sync | 滤镜对话框用 X/Esc 关闭后，画布永久停在预览图 | 只有 Cancel 按钮调 `clearPreview`；`WA_DeleteOnClose` 无 finished 钩子 | 在 `QDialog::finished` 统一清预览并在析构时 erase 注册表项 |
| CP-15 | P1 | media/imagewindow.cpp:1880 | memory | 预览图按裸 `ImageWindow*` 常驻，窗口关闭后泄漏/错投 | 注释声称“随析构自动清空”，实际无任何析构钩子 | 改 `QHash<QPointer<ImageWindow>, ...>` 或存成员 |
| CP-16 | P1 | filters/FilterStrategy.cpp:131 | crash | 灰度图上「反锐化蒙版 threshold>0」越界写 | 用 `Vec3b` 访问 `CV_8UC1/8UC4` 数据 | 按 `channels()` 分派或统一转 3ch |
| CP-17 | P1 | media/imagewindow.cpp:564 | memory | 200 步撤销 × 2 张整图，12MP 文档可达数 GB | `FilterCommand/LiquifyCommand` 各存 before+after 全图 | 命令存瓦片/量化差量或加字节预算 |
| CP-18 | P1 | layers/LayerStack.cpp:1645 | logic | 蒙版画笔/矢量蒙版/密度/羽化/反相 对渲染完全无效 | 渲染只用旧 `layerMask`，`LayerMaskRenderer` 全项目零调用 | 渲染改为消费 `Layer::mask`（含 density/feather/invert） |
| GPU-01 | P1 | compositor/RenderGraph.h:98 | design | 状态栏/Auto·Force GPU 开关对渲染零影响 | `setBackend()` 在 src/ 中无调用点 | 在 `renderViaCompositor()` 里 `rg.setBackend(&reg)` |
| GPU-02 | P1 | gpu/BackendRegistry.cpp:14 | logic | 即使接入，`pick()` 也永远返回 CPU | 构造时把 `BackendCapabilities::isGpuAvailable()` 当作 provider，而该值恒 false（`setProvider` 生产中零调用） | provider 改为惰性 `probeOpenGL()`，探测结果缓存 |
| GPU-03 | P1 | gpu/GpuBackend.cpp:292 | logic | 若启用，瓦片上下翻转 | `glReadPixels` 自下而上却被写入自上而下的 QImage | readback 后 flip 或把 UV 反向 |
| GPU-04 | P2 | gpu/GpuBackend.cpp:281 | logic | 部分驱动上画面错乱 | 未 `bindAttributeLocation`，靠声明顺序碰运气；`tupleSize` 传 4 靠 stride 推导凑对 | 链接前绑定属性位置，`tupleSize=2, stride=4*sizeof(float)` |
| GPU-05 | P2 | gpu/GpuBackend.cpp:105 | crash | 跨线程销毁 QOpenGLContext/FBO 有驱动级崩溃风险 | 首次调用线程创建、析构可能发生在主线程 | 显式 `moveToThread` / 在主线程 `makeCurrent` 后再释放 |
| GPU-06 | P2 | gpu/GpuBackend.cpp:120 | logic | 首次探测失败后本会话永不再试 | `m_probed=0` 粘滞 | 加用户触发的 re-probe 入口 |
| GPU-07 | P2 | gpu/GpuBackend.cpp:211 | perf | “GPU 加速”比 CPU `clone()` 更慢 | 纯 pass-through：上传+绘制+回读 | 未实现真实 shader 前不要把 GPU 路径接进渲染 |
| FLT-01 | P1 | filters/FilterStrategy.cpp:172 | logic | 查找边缘/照亮边缘输出单通道，画面瞬间变灰 | 判断 `out.channels()==3`（此时 out 为空）而非 `in.channels()` | 按 `in.channels()` 决定回 3ch |
| FLT-02 | P1 | layers/LayerStack.cpp:1175 | perf | 智能对象每次渲染都重跑整幅高斯模糊 | `applySmartFilterChain` 无缓存，随 render 全量重算 | 按 (smartIdx, chain version) 缓存结果，脏了才重跑 |
| FLT-03 | P2 | filters/FilterStrategy.cpp:126 | logic | 反锐化蒙版亮/暗halo不对称 | `cv::subtract` 在 CV_8U 上把负差饱和到 0 | 转 CV_16S/32F 计算 |
| FLT-04 | P2 | filters/FilterStrategy.cpp:155 | ui-visual | 浮雕高光被截断 | `filter2D` 输出 CV_8U 后加 128，负值先被 clamp | 用 CV_16S/32F 后 bias 再 saturate |
| FLT-05 | P2 | filters/FilterStrategy.cpp:434 | logic | 4ch 图像白平衡按 3ch 跨步写，通道错位 | `channels() >= 3` 分支用 `Vec3b` | 单独处理 4ch 或先 BGRA2BGR |
| FLT-06 | P2 | filters/FilterStrategy.cpp:390 | crash | 4ch 图像「减少噪点」抛 cv::Exception | `bilateralFilter` 只支持 1/3ch | 4ch 先转 3ch |
| FLT-07 | P2 | filters/FilterDialog.cpp:235 | ux | 添加噪点/减少噪点/中值降噪/白平衡无可调参数 | 参数控件 switch 未覆盖这些 kind | 补 stddev / temp / tint 等 slider |
| FLT-08 | P2 | app/mainwindow.cpp:1112 | ux | 14 个滤镜菜单项置灰（仅声明 enum），8 个是别名的“伪滤镜” | `FilterFactory` 返回 nullptr 或复用别的 Strategy | 菜单标注「未实现」，或删除别名项 |
| FLT-09 | P2 | media/imagewindow.cpp:1878 | threading | 两个滤镜对话框并存时预览互相覆盖、Cancel 清掉对方的预览 | 单槽 `s_previewRegistry[this]` | 预览 key 加 dialog token |
| FLT-10 | P2 | filters/FilterDialog.cpp:362 | ux | 占位滤镜对话框 OK 后静默无操作 | strategy 为空时只 `accept()` | OK 置灰或提示「未实现」 |
| LQ-01 | P1 | liquify/LiquifyDialog.cpp:105 | logic | 选 Twirl/Pucker/Bloat/Reconstruct/Smooth、调笔刷大小/压力全部无效 | 槽函数 `Q_UNUSED`；canvas 硬编码 ForwardWarp/50/0.5 | 把 tool/radius/pressure 下传 canvas |
| LQ-02 | P1 | liquify/LiquifyBackingStore.cpp:32 | perf | 每笔越来越慢，最终整幅重绘 | `dirtyRect` 只并集、渲染后从不 `clearDirty()` | `renderDirty` 后立即清 dirty |
| LQ-03 | P1 | liquify/LiquifyEngine.cpp:68 | perf | 打开/确定液化时界面冻结数秒 | 全图逐像素 `pixelColor/setPixelColor` + 双线性 QColor 运算 | 用 `scanLine`+`cv::remap` 或 OpenGL 纹理 |
| LQ-04 | P1 | liquify/LiquifyCanvas.cpp:51 | logic | 画笔笔刷参数被忽略、无缩放 | `widgetToImage` 恒等、默认值写死 | 引入 zoom/pan 与 panel 参数 |
| LQ-05 | P1 | liquify/FaceDetector.cpp:52 | build | 任何其它机器上人脸检测都不可用 | ONNX 路径硬编码 `D:/Collide/...` | `applicationDirPath()`/资源内嵌 |
| LQ-06 | P1 | liquify/FaceDetector.cpp:186 | crash | 模型/形状异常时进程崩溃 | `forward()` 未包 try/catch | 捕获 cv::Exception 返回空结果 |
| LQ-07 | P1 | liquify/FaceDetector.cpp:198 | logic | 同一张脸被列为多个 Face | 无 NMS、无坐标 clamp | `cv::dnn::NMSBoxes` + clamp |
| LQ-08 | P1 | liquify/LiquifyDialog.cpp:122 | data-loss | 改网格尺寸后所有液化工作丢失 | `rebuildMesh()` 重建 mesh 并清零位移 | 按比例重采样旧位移到新网格 |
| LQ-09 | P1 | liquify/LiquifyMesh.cpp:174 | perf | 网格分辨率上限 32×32，局部笔刷被抹平 | 控制点过少 + 每 stamp 遍历全部顶点 | 默认网格随图像尺寸自适应（≈8px/格） |
| LQ-10 | P2 | liquify/LiquifyCanvas.cpp:145 | perf | 勾选「Show Frozen」后每次重绘卡顿 | 全图 ARGB32 + 逐像素 `setPixelColor` | 单通道 mask 直绘 + 缓存 |
| LQ-11 | P2 | liquify/LiquifyOptionsPanel.cpp:32 | logic | Freeze/Thaw 工具不存在；freeze mask 无任何写入点 | 只注册 6 个工具，mask 全 0 | 实现冻结笔刷或移除 UI |
| LQ-12 | P2 | liquify/LiquifyBackingStore.cpp:12 | logic | 液化形变过于“整体拉伸” | 固定 5×5 网格且以 meshSize 为边长 | 同 LQ-09 |
| LQ-13 | P2 | liquify/LiquifyDialog.cpp:156 | ux | 面型滑杆 Apply 后无撤销、参数无法回看 | 直接改 mesh，无 session 级 undo | 记录 mesh 快照栈 |
| LQ-14 | P2 | liquify/LiquifyCommand.cpp:25 | data-loss | 液化 undo 同 CP-11（整幅合成写回 base 层） | 复用 `replaceCurrentImage` | 与 CP-11 一并修 |

（共 49 条：P0=2，P1=30，P2=17；下列展开仅 P0/P1。）

### CP-01 默认合成路径把非 3 通道文档渲染成黑屏 (P0)

- **位置**：`src/media/compositor/RenderGraph.cpp:56-57,68-72`、`src/media/compositor/nodes/LayerBitmapNode.cpp:39,43`、
  `src/media/imagewindow.cpp:851`
- **证据**：

```cpp
// RenderGraph.cpp:56
cv::Mat canvas(m_grid->imageHeight(), m_grid->imageWidth(), CV_8UC3,
               cv::Scalar(0, 0, 0));
...
// RenderGraph.cpp:67
cv::Mat dst = canvas(r);
if (tile.size() == dst.size() && tile.type() == dst.type()) {
    tile.copyTo(dst);
}
// Mismatched size / type = silently skip        <-- 无日志、无兜底
// LayerBitmapNode.cpp:43
cv::Mat tile(r.height, r.width, m_layer->image.type());   // 跟随图层类型
// imagewindow.cpp:851
cv::Mat img = cv::imread(path.toLocal8Bit().toStdString(), cv::IMREAD_UNCHANGED);
// 之后只做 depth 转换（16bit→8U），从不做通道归一
```

- **现象 / 复现路径**：打开任意灰度 JPEG/PNG（imread UNCHANGED → `CV_8UC1`），或带 alpha 的
  PNG（`CV_8UC4`）→ base layer 保持 1/4 通道（`LayerStack::setBaseLayer` 只 clone 不转格式）
  → `LayerBitmapNode` 返回 1/4ch tile → 与 canvas 类型不符 → 每个瓦片都被静默跳过 → 画布全黑。
- **根因**：P8.2 把 compositor 设为默认（`LayerStack.h:327 m_compositorEnabled = true`，
  `LayerStack.cpp:1557`）时，只验证了 3ch 的 A/B 一致性，没有处理 legacy 路径天然支持
  的“canvas 类型 = base 图像类型”这一事实；adapter 里 `LayerBlendNode::fitToSize()` 会
  把 1/4ch 转 3ch，但 **source 节点（base/bitmap）不做归一化**，因此单层文档、或 base 层
  本身就会全黑。
- **修复建议**：在 `LayerStackAdapter::build()` 里对 `base->image.type() != CV_8UC3` 做一次
  `cvtColor`（1ch→GRAY2BGR、4ch→BGRA2BGR）后再建 `LayerBitmapNode`；或在
  `RenderGraph::execute*()` 拼接处用 `cvtColor` 归一并在 mismatch 时 `qWarning` 而不是静默跳过。
  同时 `loadFile()` 应把 `m_original` 规范化为 `CV_8UC3`（UI 层本来就按 3ch 处理，
  `currentImageAsQImage()` 的 1ch/4ch 分支保留给其它用途即可）。
- **关联**：保存/导出、图层缩略图、滤镜（滤镜作用在 `m_current` 上，1ch 会走灰度分支）。

### CP-11 滤镜/液化作用在“合成结果”上再写回 base 层：重复合成 + undo 不还原 (P0)

- **位置**：`src/media/filters/FilterCommand.cpp:45-65`、`src/media/imagewindow.cpp:831-846`、
  `src/media/imagewindow.cpp:2070-2087`、`src/app/mainwindow.cpp:2577-2595`
- **证据**：

```cpp
// FilterCommand.cpp:62  redo
m_host->replaceCurrentImage(m_after);      // m_after = filter(m_current) = 已含所有图层
// imagewindow.cpp:835
if (m_layerStack && m_layerStack->count() > 0) {
    auto base = m_layerStack->baseLayer();
    if (base) base->image = img.clone();   // 把「合成图」写进 base 层
}
invalidateCurrentCache();                  // 下一次 render() 再把上方图层叠加一遍
m_current = img.clone();
```

- **现象 / 复现路径**：文档有 ≥2 个图层 → 图层 2 选一个滤镜 → OK。第一次显示正常（
  `m_current` 被直接替换），但随后任何编辑触发 `invalidateCurrentCache()` +
  `encode render()`，上方图层会被**再次**叠到“已含它自己”的 base 图上（内容加深/重影）；
  撤销回到 before 图（同样是合成图），仍然重复。多次套滤镜等于反复压平整篇文档。
- **根因**：P0-5 滤镜实现年代只有 `m_current` 单图，没有“作用于活动图层”的概念；
  P1 引入图层栈后没有同步改写入目标。P8 后 `render()` 又会重构整个栈，使问题从“显示错”
  升级为“保存出来的也是错的”。
- **修复建议**：`FilterCommand` 改为持有 `(layerId, beforeMat, afterMat)` 并只写目标
  `Layer::image`（发 `layerChanged`），不要碰 base 层；若暂时只能整图处理，则先
  `flattenVisible()` 成单图层再套滤镜（PS 语义上也要求先栅格化/合并）。`LiquifyCommand`
  同样处理。undo 侧恢复 `Layer::image` 原像素。
- **关联**：撤销栈、保存、液化（LQ-14）、智能滤镜（真正非破坏的替代方案）。

### CP-02 瓦片缓存无上限：一次渲染的峰值内存远超整幅图 (P1)

- **位置**：`src/media/compositor/TileCache.cpp:9-14`、`src/media/compositor/RenderGraph.h:126`、
  `src/media/compositor/RenderGraph.cpp:248-250`
- **证据**：

```cpp
void TileCache::put(const TileKey& k, const cv::Mat& tile) {
    if (!k.isValid()) return;
    QMutexLocker lock(&m_lock);
    m_tiles.insert(k, tile.clone());   // 每块 256×256×3 ≈ 196 KB，永久常驻
}
```

- **现象 / 复现路径**：4000×3000（192 瓦片）× 10 图层（≈21 个节点：base + 每层 bitmap 与
  blend 各一 + mask/opacity）≈ **780 MB** 额外常驻；6000×4000（384 瓦片）≈ 1.5 GB，
  叠加 canvas 与各节点临时副本即可 OOM（32 位构建必崩；64 位也是严重抖动）。
  而且这些瓦片在一次 `execute()` 中**每块只被消费一次**（线性链），缓存收益为 0。
- **根因**：设计假设“增量渲染会跨帧复用瓦片”，但 `RenderGraph` 是每次 `render()` 的栈对象
  （CP-03），缓存生命周期 == 一次渲染，于是“缓存”退化成“把所有中间结果留在内存里”。
- **修复建议**：短期把 `TileCache` 的 put 改为“只缓存命中过两次以上的 key”，或给
  `m_tiles` 加字节上限（如 256 MB）的 LRU；中期按 CP-03 做跨渲染缓存，并按节点类型
  决定是否可缓存（source 节点有实际收益，blend 链没有）。
- **关联**：`renderAsync` 的大图阈值判断（imagewindow.cpp:1946）只看原图尺寸，未考虑该峰值。

### CP-03 “增量合成器”在生产中完全不增量 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1755-1770`、
  `src/media/compositor/CompositorGraph.cpp:101`、`src/media/compositor/nodes/LayerBitmapNode.cpp:60`
- **证据**：

```cpp
cv::Mat LayerStack::renderViaCompositor() const {
    compositor::LayerStackAdapter::Result built =
        compositor::LayerStackAdapter::build(const_cast<LayerStack&>(*this));
    compositor::TileGrid grid(built.canvasSize.width, built.canvasSize.height);
    compositor::RenderGraph rg(&built.graph, built.outputNode, &grid);  // 栈对象
    return rg.execute();                                               // cache 随函数销毁
}
```

- **现象 / 复现路径**：任何一次图层操作（改不透明度、拖动滑杆、画一笔）都重建全部节点
  （每节点一次堆分配）+ 重建 `TileCache` + 全量重算所有瓦片。`markDirty()`、
  `CompositorNode::version()`、`markLayerEdited()`、`executeAndReuse()`、
  `lastHitCount()` 在 src/ 中**没有任何调用方**（全项目 grep 仅有定义与 tests）。
  与 legacy `render()`（整幅 addWeighted）相比，单次渲染的常数因子约 2–3 倍。
- **根因**：P8.1 的缓存/脏传播是为“持久 DAG”设计的，但 P8.1.2 的接入点选择“每次 render
  重建 DAG”，两者矛盾；且 `CompositorNode::id()` 用 `reinterpret_cast<NodeId>(this)`
  （CompositorNode.h:35），节点地址复用会让持久缓存命中错误内容——这是“不敢持久化”的
  隐含原因。
- **修复建议**：给节点稳定 ID（`LayerId + 节点角色` 或图内自增），把 `CompositorGraph`
  与 `TileCache` 提升为 `ImageWindow`/`LayerStack` 的成员；图层属性变化走
  `graph.markDirty(nodeId)`，只有结构性变化（增删层/换 kind）才重建子图。若短期不做，
  应把注释与任务声明中的“增量/缓存”降级为“未启用”，避免后续误判。
- **关联**：CP-02（内存）、P8 性能指标、`ImageWindow::rebuildCurrentCache` 的异步阈值。

### CP-04 并行合成：提交失败或任务异常 → 永久挂死 (P1，接线后即 P0)

- **位置**：`src/media/compositor/RenderGraph.cpp:140-159`、
  `src/core/ThreadPool/thread_pool.cpp:219-225,320-323`
- **证据**：

```cpp
for (int i = 0; i < nTiles; ++i) {
    engine->background().submit_fn( ... [.., &doneCount](TaskContext&) {
            cv::Mat tile = requestNodeTile(m_output, tx, ty, localCtx);
            ...
            doneCount.fetch_add(1, std::memory_order_relaxed);
        }, "P8.4 render tile", Priority::Normal);
    // 返回值 bool 被丢弃
}
while (doneCount.load(std::memory_order_relaxed) < nTiles) {
    QThread::msleep(1);      // 无超时、无取消、无 fallback
}
```

- **现象 / 复现路径**：`submit_fn` 在队列满/背压 `Reject`（thread_pool.cpp:320-323）或池
  shutdown 时返回 false → 该瓦片永远不会执行；`run_task` 用 `catch (...)` 吞掉
  `cv::Exception`（thread_pool.cpp:219-225）→ 计数不增。两种情况都让调用线程死循环，
  若在 UI 线程调用即整个应用卡死（且无法用超时/取消恢复）。
- **根因**：把“提交成功”和“任务必达”当成同一件事；用计数忙等代替真正的 barrier。
  另外 `ready[i]`/`tileSlots[i]` 由 worker 写、主线程读，仅靠 relaxed 原子计数，
  C++ 内存模型下没有 happens-before，属数据竞争（x86 上侥幸正确）。
- **修复建议**：用 `std::vector<std::future<cv::Mat>>` 或池的 continuation/`wait_for_counter`
  （executor.hpp:131 已提供 acquire 语义等待）替换手写计数；检查 `submit_fn` 返回值，
  失败即降级 `executeSequential()`；`requestNodeTile` 内 try/catch，异常瓦片记日志后返回空。
- **关联**：CP-05、CP-09；目前该路径仅被 tests 触发，但一旦 `renderAsync` 把 engine 传给
  `RenderGraph` 就会变成生产 P0。

### CP-05 节点内记忆化在并行下不是线程安全的 (P1)

- **位置**：`src/media/compositor/nodes/LayerRasterNode.h:96-97,29-49`、
  `src/media/compositor/nodes/LayerStackAdapter.cpp:53-54,59-73`、
  `src/media/compositor/RenderGraph.cpp:137-155`
- **证据**：

```cpp
// LayerRasterNode.cpp:29
const cv::Mat& LayerRasterNode::raster() {
    if (m_rasterValid) return m_raster;      // 多个 worker 同时进
    m_raster = m_stack->rasterizeLayer(m_layer);   // 同时写 m_raster（cv::Mat 头 + 堆）
    ...
    m_rasterValid = true;
}
```

- **现象 / 复现路径**：`CompositeStrategy::Parallel` 下，同一 source 节点的不同瓦片被不同
  worker 并发 `compute()` → 同时读/写 `m_raster`/`m_rasterValid`、`MaskBitmapNode::m_normalized`
  → 重复栅格化（QPainter 并发）、cv::Mat 引用计数竞争、可能 use-after-free。
  同一文件 `RenderGraph.cpp:137` 的 `localCtx.hits/misses` 也是普通 int 被多线程 `++`。
- **根因**：注释声称“compute() 是线程安全的（无共享可变状态）”，但两个节点都持有
  mutable memo，`MaskBitmapNode` 还缓存“归一化到 canvas”的整幅蒙版。
- **修复建议**：渲染前置 prepare 阶段（单线程）把 raster/normalized mask 算好放进节点；
  或用 `std::once_flag`/`QMutex` 保护 memo；`RenderContext` 统计改 `std::atomic<int>`。
- **关联**：CP-04。

### CP-06 LayerMaskNode 就地改写上游缓存 tile（污染 + 非 3ch 抛异常）(P1)

- **位置**：`src/media/compositor/nodes/LayerMaskNode.cpp:47-57`、`RenderGraph.cpp:213-215`
- **证据**：

```cpp
cv::Mat base = ctx.requestNodeTile(m_base, tileX, tileY);   // 返回缓存 Mat 的浅拷贝
...
cv::Mat baseF, maskF;
base.convertTo(baseF, CV_32FC3);        // 注意：通道数取自 base 自身
mask.convertTo(maskF, CV_32FC1, 1.0 / 255.0);
std::vector<cv::Mat> channels(3, maskF);
cv::merge(channels, maskBgr);           // 恒为 3ch
cv::Mat outF = baseF.mul(maskBgr);      // base 为 1/4ch 时 CV_Assert 抛异常
outF.convertTo(base, CV_8UC3);          // base 与缓存同一缓冲区 → 就地覆盖
```

- **现象 / 复现路径**：`cv::Mat::create` 在“尺寸/类型相同且已有 data”时直接复用缓冲区、
  不看引用计数，所以 `convertTo(base, CV_8UC3)` 会把乘过蒙版的像素写回 **TileCache 里
  上游节点的缓存条目**。当前 adapter 生成的是线性链、每个瓦片只有一个消费者，所以暂时
  看不到错图；一旦 DAG 出现分叉（同一节点被两个下游引用）或改成持久缓存，就会读到
  “被蒙版改过”的中间结果；并行路径下还可能被另一个线程读到写了一半的瓦片。
  另外若 `base` 是 1ch（灰度图层）或 4ch（带 alpha 的图层）且启用了蒙版，
  `baseF.mul(maskBgr)` 触发 `CV_Assert` → `cv::Exception` 逃出渲染（见 CP-09）→ 进程崩溃。
- **根因**：节点契约没有写明“compute() 不得修改 `requestNodeTile()` 的返回值”，
  而 `TileCache::tryGet` 返回的是浅拷贝（TileCache.cpp:22）。
- **修复建议**：输出必须写到新分配的 `cv::Mat`（`outF.convertTo(out, CV_8UC3)` 后返回
  `out`）；mask/color 先统一到 3ch；在 `RenderContext::requestNodeTile` 注释里固化
  “返回值只读”契约，并考虑返回 `const cv::Mat&` + `clone()` 的显式 API。
- **关联**：CP-07、CP-18。

### CP-07 蒙版语义错误：变黑而不是透明（两条路径共有）(P1)

- **位置**：`src/media/compositor/nodes/LayerMaskNode.cpp:47-57`、
  `src/media/imageworker/layers/LayerStack.cpp:684-705,1644-1648`
- **证据**：

```cpp
// legacy applyMask：mask 0 → 像素 *0 → 黑
cv::Mat mask3ch; cv::cvtColor(maskR, mask3ch, cv::COLOR_GRAY2BGR);
cv::multiply(layerF, maskF, outF);
// 随后 applyBlend(l->blend, canvas, layerMat, l->opacity) —— 黑像素照常参与混合
```

- **现象 / 复现路径**：给图层画蒙版遮住一部分 → 该区域变成**黑色**而不是露出下层；
  若该层混合模式是 Multiply/Screen 等，黑像素还会主动压暗/影响下层（Multiply 下=纯黑），
  与 PS“完全透明、不参与合成”的语义相反。
- **根因**：整条渲染管线只有 `CV_8UC3`、没有 per-pixel alpha，蒙版只能被降级成“乘颜色”。
  compositor 的 `LayerMaskNode` 忠实复刻了这一错误（以保证 A/B parity），因此换路径也修不了。
- **修复建议**：合成改为 pre-multiplied RGBA：`applyMask` 产出 alpha 通道，
  `applyBlend` 用 `out = src*a + dst*(1-a)`（Porter-Duff over），blend mode 只作用于
  `a` 覆盖区。这是 P8 之后“蒙版/软边/羽化”全部功能的前置条件。
- **关联**：CP-18（结构化蒙版完全未参与渲染）、FLT/液化、以及与 layers 模块审查员的交叉结论。

### CP-08 LayerBitmapNode 的 padding 是未初始化内存 (P1)

- **位置**：`src/media/compositor/nodes/LayerBitmapNode.cpp:43,53-57`
- **证据**：

```cpp
cv::Mat tile(r.height, r.width, m_layer->image.type());   // ← 未初始化
...
if (copyW < r.width || copyH < r.height) {
    // Zero-init already happened via cv::Mat ctor; nothing to do.   ← 与事实相反
}
return tile;
```

- **现象 / 复现路径**：图层图像小于画布（小图粘贴/组内子层/画布扩大后未同步的图层）→
  tile 超出图层图像的部分保留堆内存残留 → 画面出现随机彩色条带，且不同次运行/不同机器
  内容不同（截图、保存、导出一致性问题，评审时最难复现的一类）。
- **根因**：`cv::Mat(rows, cols, type)` 只分配不初始化；注释误以为构造会清零。
  （同项目的 `LayerRasterNode.cpp:71`、`MaskBitmapNode` 都正确用了 `cv::Scalar(0)`，
  可见这是单点遗漏。）
- **修复建议**：`cv::Mat tile(r.height, r.width, m_layer->image.type(), cv::Scalar(0,0,0));`，
  并加一条单测：小图层 + 多瓦片，断言边缘像素为 0。
- **关联**：CP-07（蒙版按 canvas 归一后，garbage 区可能落在 mask>0 区域被保留）。

### CP-09 渲染链路无异常边界 (P1)

- **位置**：`src/media/compositor/RenderGraph.cpp:203-252`（`node->compute()`）、
  `LayerStack.cpp:1755`；典型抛出点 `LayerMaskNode.cpp:55`、`LiquifyEngine`/`FaceDetector`。
- **证据**：

```cpp
cv::Mat tile = node->compute(tileX, tileY, *m_grid, ctx);   // 无 try/catch
```

- **现象 / 复现路径**：任何节点里的 `CV_Assert`/`cvtColor` 通道不匹配（CP-06）/内存不足，
  异常都会穿过 `LayerStack::render()` → `ImageWindow::rebuildCurrentCache()` → Qt 事件循环，
  由于没有顶层捕获 → `std::terminate()`。同类问题在 `renderAsync` 的 worker 线程里则表现为
  静默丢帧（线程池吞异常，thread_pool.cpp:221）。
- **根因**：项目没有统一的异常策略；OpenCV 用异常做参数校验，而 Qt 事件循环不捕获。
- **修复建议**：`RenderGraph::requestNodeTile()` 包 `try { node->compute() } catch (const
  cv::Exception&) { LOG_ERROR(...); return cv::Mat(); }`；`LayerStack::render()` 外层再兜一层
  并回退 legacy 路径（配合 CP-19 的开关）。
- **关联**：CP-01、CP-06、LQ-06。

### CP-10 渲染入口不做环检测，递归求值可能栈溢出 (P1)

- **位置**：`src/media/compositor/CompositorGraph.cpp:48-83`（实现了 `topologicalOrder()/hasCycle()`）、
  `src/media/compositor/RenderGraph.cpp:203-225`（实际入口）
- **证据**：

```cpp
QList<CompositorNode::NodeId> CompositorGraph::topologicalOrder() const { ... 
    throw std::logic_error("CompositorGraph: cycle detected"); }
// RenderGraph::requestNodeTile 只做：cache 查找 → node->compute() → 节点内部再递归
```

- **现象 / 复现路径**：`CompositorGraph::addEdge()` 公开且不做环检测；`hasCycle()`/`topologicalOrder()`
  在 src/ 中无调用者。任何“调整层套回自己”或未来可交互的图层组一旦形成环，递归 `compute()`
  会无限展开直至栈溢出（崩溃，且异常形式的环检测 `throw` 也没人接）。
- **根因**：环检测写了但没接进渲染入口；用 C++ 异常作为图校验手段，与项目其它地方的错误处理不一致。
- **修复建议**：`execute()` 开头调用 `if (m_graph->hasCycle()) return cv::Mat();`（并 LOG_ERROR），
  或把 `topologicalOrder()` 改成返回 `std::optional`/`Result`；`addEdge()` 里即时拒绝成环。
- **关联**：CP-09。

### CP-12 Liquify 结果红蓝通道互换 (P1)

- **位置**：`src/app/mainwindow.cpp:2579-2589`、`src/media/imagewindow.cpp:781-785`
- **证据**：

```cpp
// currentImageAsQImage(): CV_8UC3 → cvtColor(BGR2RGB) → QImage::Format_RGB888
// mainwindow.cpp:2582
} else if (result.format() == QImage::Format_RGB888) {
    after = cv::Mat(result.height(), result.width(), CV_8UC3,
                    (void*)result.bits(), result.bytesPerLine()).clone();   // 当成 BGR 用
}
```

- **现象 / 复现路径**：图像菜单 → 液化 → 任意形变 → OK。结果的整体色相偏移（红↔蓝互换），
  `QImage::Format_ARGB32` 在内存中的字节序恰为 B,G,R,A，因此 4ch 分支（走
  `convertToFormat(ARGB32)` → `CV_8UC4`）碰巧正确；**只有 `Format_RGB888` 分支是错的**。
- **根因**：QImage ↔ cv::Mat 的通道序约定不统一：`ImageProcessor::matToQImage` 走 RGB888，
  但反向转换在两处各写一遍，其中液化这里漏了 `cvtColor`。`FaceDetector::qImageToBgr` 反而
  写对了，说明是复制粘贴遗漏。
- **修复建议**：抽一个 `ImageProcessor::qImageToMatBgr(const QImage&)`（按 format 分支 +
  `cvtColor(RGB2BGR/BGRA2BGR/GRAY2BGR)`）并让 liquify / FilterDialog 预览回读统一调用。
- **关联**：CP-11、LQ-03。

### CP-13 渐变映射使用未初始化 LUT (P1)

- **位置**：`src/media/filters/FilterStrategy.cpp:255-271`（菜单入口 mainwindow.cpp:1168 可用）
- **证据**：

```cpp
out = gray;
cv::LUT(gray, cv::Mat(1, 256, CV_8U), out);  // identity LUT placeholder ← 从未填值
```

- **现象 / 复现路径**：颜色 → 渐变映射 → OK/Apply。输出为随机灰度映射表的结果（可能全黑、
  也随堆状态随机），不可预期；`paramText()` 还显示“默认黑白”。
- **根因**：`cv::Mat(1,256,CV_8U)` 只分配不初始化；`cv::LUT` 不校验表内容。
- **修复建议**：显式 `for (int i=0;i<256;++i) lut.at<uchar>(i)=i;`（恒等）或按 PS 默认
  黑→白渐变生成；若要真实现，需增加渐变编辑器并把 LUT 存进参数/命令。
- **关联**：FLT-08（同类“启用了但没实现”的菜单项）。

### CP-14/CP-15 滤镜预览：X/Esc 关闭后画布冻结在预览 + 注册表泄漏 (P1)

- **位置**：`src/media/imagewindow.cpp:1868-1888,1891-1911,2055-2068`、
  `src/app/mainwindow.cpp:2487-2501`
- **证据**：

```cpp
namespace { QMutex s_previewRegistryMutex;
            QHash<ImageWindow*, std::shared_ptr<cv::Mat>> s_previewRegistry; }
std::shared_ptr<cv::Mat> lookupPreview(ImageWindow* w) { ... }   // 只有 clearPreview 会 erase
// renderToView(): preview 优先于 m_current
auto preview = lookupPreview(this);
const cv::Mat* display = preview ? preview.get() : &m_current;
// mainwindow: dlg->setAttribute(Qt::WA_DeleteOnClose); 只连了 applyRequested/okRequested
```

- **现象 / 复现路径**：滤镜对话框 → Apply（出预览）→ 点窗口右上角 X 或按 Esc → 对话框关闭，
  但 `clearPreview()` 未调用：画布**永久**显示那份预览图，之后所有图层编辑/撤销都“不可见”
  （`renderToView` 永远优先预览），直到下一次打开滤镜对话框并 OK/Cancel。若此时关闭
  ImageWindow，注册表项既不 erase 也无析构钩子（注释 imagewindow.cpp:1876 声称“自动随
  ImageWindow 析构清空”，代码里没有任何 `destroyed` 连接）→ `shared_ptr<cv::Mat>` 泄漏
  （12MP 预览约 48 MB/次）；更糟的是新 ImageWindow 若分配到同一地址，会**继承旧预览**，
  画面直接显示上一张图的滤镜结果。
- **根因**：预览状态放在文件作用域静态表里（注释解释了原因：为避免 class layout 变动触发
  历史 segfault），但随之放弃了“生命周期跟随对象”的保障；关闭路径也只覆盖了 Cancel 按钮。
- **修复建议**：`connect(dlg, &QDialog::finished, img, &ImageWindow::clearPreview)`；
  给 `ImageWindow` 加 `destroyed` 时 erase 的钩子（或在 `~ImageWindow` 里显式清）；
  注册表 key 改 `QPointer<ImageWindow>` 并在 lookup 时清理空 key。根本修法仍是把预览图
  做成 `ImageWindow` 的 `std::unique_ptr<cv::Mat>`（layout 问题应用 PIMPL 解决，而不是靠静态表）。
- **关联**：FLT-09。

### CP-16 反锐化蒙版在灰度图上越界写 (P1)

- **位置**：`src/media/filters/FilterStrategy.cpp:131-138`
- **证据**：

```cpp
if (threshold > 0) {
    for (int y = 0; y < out.rows; ++y)
        for (int x = 0; x < out.cols; ++x) {
            const cv::Vec3b m = mask.at<cv::Vec3b>(y, x);   // mask 可能是 CV_8UC1
            if (std::abs(m[0]) < threshold)
                out.at<cv::Vec3b>(y, x) = in.at<cv::Vec3b>(y, x);   // 对 1ch Mat 写 3 字节
        }
}
```

- **现象 / 复现路径**：灰度文档（或任何 1ch 输入）+ 加深/锐化 → 反锐化蒙版，把 threshold
  调到 >0 → 每像素写 3 字节到 1 字节/像素的矩阵：堆越界写（最后一行可越过分配区），
  轻则图像垃圾、重则崩溃；同时读的 `m[0]` 也只取到相邻像素的 B 分量。
- **根因**：按 3ch 写死像素访问，未按 `channels()` 分支（同一个文件里其它滤镜都做了
  `in.channels()==3` 判断，此处漏了）；另外 `std::abs(m[0])` 对 uchar 恒等，逻辑本身也不对。
- **修复建议**：先在 CV_32F 三通道上算 mask（`in - blurred`），按 `|mask| < threshold`
  逐通道回填；或统一 `cvtColor(in, 3ch)` 后处理。加 1ch/3ch/4ch 的单元测试。
- **关联**：CP-01（灰度文档在 compositor 下已黑，但 legacy 回退或其它入口仍可达）。

### CP-17 撤销栈按“整幅图 × 2”计费，无字节预算 (P1)

- **位置**：`src/media/imagewindow.cpp:564`、`src/media/filters/FilterCommand.cpp:14,34`、
  `src/media/filters/liquify/LiquifyCommand.cpp:12-13`
- **证据**：

```cpp
m_undoStack->setUndoLimit(200);      // 只限条数，不限字节
FilterCommand::FilterCommand(...) : m_before(before.clone()) { strategy->apply(before, m_after); }
```

- **现象 / 复现路径**：4000×3000 文档（36 MB/幅），20 次滤镜 = 40 幅 ≈ **1.4 GB**；
  配合 CP-11 每次滤镜还会触发整篇重渲染。液化同理。再加 `setBaseLayer`/`replaceCurrentImage`
  的额外 clone，长时间会话必然 OOM/换页，用户感知为“越用越卡最后崩”。
- **根因**：`QUndoLimit` 是唯一的内存治理手段，粒度是“命令条数”而非字节；滤镜类命令天然
  是 O(图像大小) 的。
- **修复建议**：给 `ImageWindow` 维护 `undoBytes()` 并在 push 后按字节裁剪（或自定义
  `QUndoStack` 子类重写 `push`）；滤镜命令改存“参数 + 受影响区域”而非全图（配合 CP-11
  的智能滤镜方向）；液化命令存 mesh 快照（几 KB）而不是两整幅图。
- **关联**：CP-11、LQ-14、CP-02。

### CP-18 结构化蒙版（画笔/矢量/密度/羽化/反相）完全不参与渲染 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1645-1648`（渲染只用旧字段）、
  `src/media/tools/MaskBrushTool.cpp:79`（写 `mask.pixel`）、
  `src/media/imageworker/layers/LayerMaskRenderer.cpp:87`（`apply()` 全项目零调用）
- **证据**：

```cpp
// LayerStack.cpp:1645（legacy）与 LayerStackAdapter.cpp:272（compositor）都只看：
if (l->maskEnabled && !l->layerMask.empty()) { layerMat = applyMask(layerMat, l->layerMask); }
// 而 UI/画笔写入的是：
// MaskBrushTool.cpp:79  cv::Mat& mask = l->mask.pixel;
```

- **现象 / 复现路径**：用「蒙版画笔」涂抹 → 画面上毫无变化（像素写进 `Layer::mask.pixel`，
  渲染读的是 `Layer::layerMask`）；「密度/羽化/反相」滑杆同样无视觉反馈；
  矢量蒙版（`addVectorMask`）也完全不被渲染。legacy 与 compositor 两条路径一致地忽略。
- **根因**：P1.3.2 引入了新 `LayerMask` 结构与 `LayerMaskRenderer`（含 density/feather/invert/
  vector 完整实现），但渲染侧没人切换过去，旧字段成为“事实标准”，形成双份状态（RUBRIC §5.4）。
- **修复建议**：渲染统一走 `LayerMaskRenderer::buildAlpha(l->mask, canvasSize)`（矢量/羽化/
  反相一次到位），compositor 侧让 `MaskBitmapNode` 调用同一函数；删除旧 `layerMask` 或让它
  成为 `mask.pixel` 的兼容视图。需要 CP-07 的 alpha 合成才能让羽化真正好看。
- **关联**：CP-07、CP-06；该结论与 `layers` 模块审查员可能重叠，建议 Lead 合并去重。

### GPU-01/GPU-02 GPU 三级开关是“装饰性 UI”，且即使接线也永远走 CPU (P1)

- **位置**：`src/media/compositor/RenderGraph.h:98`、`src/media/gpu/BackendRegistry.cpp:14-18,39-60`、
  `src/media/gpu/BackendCapabilities.h:33`、`src/ui/settingsgpupanel.cpp:114-127`、
  `src/app/mainwindow.cpp:3464-3476,3505-3507`
- **证据**：

```cpp
// 全项目 grep：RenderGraph::setBackend / BackendCapabilities::setProvider 在 src/ 中无调用
BackendRegistry::BackendRegistry()
    : m_cpu(new CpuBackend()), m_gpu(new GpuBackend(&BackendCapabilities::isGpuAvailable)) {}
bool BackendCapabilities::isGpuAvailable() { return instance().m_gpuAvailable; }  // 默认 false
// 设置页的“探测”只显示文本，不落任何状态：
const bool available = gpu::BackendCapabilities::probeOpenGL();  // 结果只进 QLabel
```

- **现象 / 复现路径**：状态栏菜单选 “Force GPU” → 文本变成 `FORCE GPU | cpu`；设置页
  “OpenGL 探测”显示“可用”，但选 ForceGpu 仍是 CPU。真实渲染路径（`renderViaCompositor`）
  从未调用 `rg.setBackend()`，即使调用了，`pick()` 也会因 `isGpuAvailable()` 恒 false
  而返回 `m_cpu`。用户看到的是“GPU 加速已开启”的假象。
- **根因**：P8.2/P8.3 分三层落地（抽象层 / 探测 / UI）但缺少最后一根线：① `setBackend` 未接；
  ② capability provider 选了一个永久 false 的静态函数，真正的 `probeOpenGL()` 只被设置页
  当按钮用、结果被丢弃；③ 探测成功也没有地方 `setForced(true)`。
- **修复建议**：启动时跑一次 `probeOpenGL()` 并 `setProvider` 到缓存的 lambda；在
  `renderViaCompositor()` 里 `rg.setBackend(&BackendRegistry::instance())`；把状态栏文本改成
  反映**真实**参与渲染的后端（当前应为 “CPU（GPU 未接入）”）。若短期内不打算接入 GPU，
  应把三态菜单隐藏或标注“(未接入)”。
- **关联**：GPU-03~07（这些缺陷在接线后才会显现，属于“接线前必修”清单）。

### GPU-03 GPU pass-through 回读上下翻转 (P1)

- **位置**：`src/media/gpu/GpuBackend.cpp:291-292`（配合 :250-268 的 UV 与 :71-79 的 quad）
- **证据**：

```cpp
QImage out(w, h, QImage::Format_BGR888);
m_functions->glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, out.bits());
// glReadPixels 原点在 FBO 左下；QImage::bits() 第 0 行是图像顶部
```

- **现象 / 复现路径**：把 `setBackend()` 接上后，每个瓦片的内容上下颠倒；由于是逐瓦片，
  出现的是“全球面拼接错位”而不是简单整图翻转。
- **根因**：上传时 `glTexImage2D` 的 data 第 0 行对应纹理底部，而 quad 的 UV 又把底部映射到
  纹理 t=1（视觉上正确），但 readback 到 top-down 的 QImage 时没有做 flip。
- **修复建议**：readback 后 `out = out.flipped(Qt::Vertical)`，或把 quad 的 UV 反过来再读。
  加一个“pass-through 与原图逐像素相等”的单测（当前 tests 若只比 size/均值会漏掉）。

### FLT-01 查找边缘 / 照亮边缘 输出单通道，把文档变成 1ch（连带 CP-01 黑屏）(P1)

- **位置**：`src/media/filters/FilterStrategy.cpp:159-198`
- **证据**：

```cpp
cv::Laplacian(gray, edges, CV_8U, 3);
cv::bitwise_not(edges, edges);
if (out.channels() == 3 && in.channels() == 3) {     // out 此刻是空 Mat → channels()==0
    cv::cvtColor(edges, out, cv::COLOR_GRAY2BGR);
} else {
    out = edges;                                     // 3ch 输入也走这里 → 输出 1ch
}
```

- **现象 / 复现路径**：颜色 → 查找边缘（或 风格化 → 照亮边缘）→ OK。结果替换 `m_current`
  并写回 base 层：文档从 3ch 变成 1ch；因为 `render()` 默认走 compositor，下一次重渲染
  直接命中 CP-01 → **整幅画布变黑**。即便不考虑 CP-01，图层栈与 `m_current` 的通道数也
  从此不一致（保存/导出按 1ch 处理，滤镜菜单里 3ch 专用路径行为改变）。
- **根因**：判据写成了 `out.channels()`（输出参数尚未赋值），应为 `in.channels()`；
  且没有把“滤镜输出必须与输入同类型”作为契约。
- **修复建议**：改为 `if (in.channels() == 3) cvtColor(edges, out, GRAY2BGR); else out = edges;`；
  在 `FilterStrategy::apply()` 的约定里写明“输出与被处理图像同 size/type”，并在
  `FilterCommand` 里断言。
- **关联**：CP-01、CP-11。

### FLT-02 智能对象的滤镜链每次渲染重跑整幅滤波 (P1)

- **位置**：`src/media/imageworker/layers/LayerStack.cpp:1175-1187`、
  `src/media/compositor/nodes/LayerRasterNode.cpp:29-49`、`LayerStack.cpp:1641-1643`
- **证据**：

```cpp
cv::Mat LayerStack::applySmartFilterChain(const cv::Mat &src, int smartIdx) const {
    const auto &chain = smartFiltersFor(smartIdx);
    cv::Mat out = src;
    for (int fIdx : chain) {
        auto fL = at(fIdx);
        if (!fL || !fL->visible || fL->opacity <= 0.0f) continue;
        cv::Mat filtered = applySmartFilter(fL->filterType, fL->filterStrength, out);
        if (!filtered.empty()) out = filtered;      // 整幅图像逐个滤镜，无缓存
    }
    return out;
}
```

- **现象 / 复现路径**：给 4000×3000 的 SmartObject 挂 1 个高斯模糊 → 之后每次任何编辑
  （拖动不透明度、切图层、画一笔）都会重新对**整幅 12MP 图像**跑一次 / 多次
  GaussianBlur（数十至数百 ms），且 compositor 的 `LayerRasterNode::raster()` 记忆化
  只活在一次渲染内。连续拖动滑杆时每次刷新都重跑，大图明显卡顿。
- **根因**：P1.4.4 的“非破坏”实现选择“渲染时重算”，但没有版本号/缓存；同时
  `appendSmartFilter` 还在子层里存了一份已过滤图像（LayerStack.cpp:1064-1090），
  这份数据在渲染中完全没用，纯占内存。
- **修复建议**：给每个 SmartObject 维护 `(chainVersion, lastResult)` 缓存，
  `setSmartFilter*/move/remove/源刷新` 时 bump version；或把滤镜链实现成 compositor 的
  真实节点（接受 halo 的 tile 化高斯）。顺手删掉子层的冗余像素。
- **关联**：CP-03、P8 性能指标。

### LQ-01 液化工具模式 / 笔刷尺寸 / 压力全部不生效 (P1)

- **位置**：`src/media/filters/liquify/LiquifyDialog.cpp:105-120`、
  `src/media/filters/liquify/LiquifyCanvas.cpp:45-65`
- **证据**：

```cpp
void LiquifyDialog::onToolModeChanged(LiquifyToolMode mode) {
    Q_UNUSED(mode);   // “Real per-tool routing lives in a follow-up”
}
void LiquifyDialog::onBrushSizeChanged(int size)     { Q_UNUSED(size); }
void LiquifyDialog::onBrushPressureChanged(qreal p)  { Q_UNUSED(p); }
// LiquifyCanvas::stampAt:
constexpr qreal kDefaultRadius = 50.0;
constexpr qreal kDefaultPressure = 0.5;
m_engine->applyToolStamp(imagePos, kDefaultRadius, kDefaultPressure,
                         direction, LiquifyToolMode::ForwardWarp);
```

- **现象 / 复现路径**：液化对话框里点击 Twirl/Pucker/Bloat/Reconstruct/Smooth 按钮、
  把 Brush Size 调到 200、Pressure 调到 100%，再去画布上涂抹——行为永远是
  “半径 50、强度 0.5 的向前变形”。六个工具里只有一个可达，UI 与行为完全脱节。
- **根因**：`LiquifyCanvas` 自己处理鼠标（构造时只拿到 engine），没有接收 panel 状态的通道；
  dialog 的槽函数被留成 TODO 且从未被后续 commit 补上。`stampExternal()` 存在但无人调用。
- **修复建议**：给 `LiquifyCanvas` 加 `setToolMode/setBrushRadius/setBrushPressure`，
  dialog 的槽转发；或让 canvas 持有 `LiquifyOptionsPanel*` 直接读。补一条“每种工具
  stamp 后 mesh 位移方向正确”的单测。
- **关联**：LQ-04、LQ-09。

### LQ-02 dirty rect 只增不减，每笔越来越慢直至整幅重绘 (P1)

- **位置**：`src/media/filters/liquify/LiquifyBackingStore.cpp:32-43,45-48`、
  `src/media/filters/liquify/LiquifyEngine.cpp:89-120`、`LiquifyCanvas.cpp:55-64`
- **证据**：

```cpp
void LiquifyBackingStore::markDirty(const QRect& r) {
    if (m_dirtyRect.isEmpty()) m_dirtyRect = clamped;
    else m_dirtyRect = m_dirtyRect.united(clamped);   // 只并集
}
// 全项目唯一 clearDirty() 的两处调用：setSnapshot() 与 resetMesh()
// renderDirty() 渲染完后没有 clearDirty()
```

- **现象 / 复现路径**：一开始每一笔只重绘笔刷范围（好），但 `dirtyRect` 是累计并集，
  随着用户在图上游走，累计矩形迅速覆盖整幅图；之后每一笔都要重绘整个累计区域，
  再叠加 LQ-03 的逐像素 QColor 渲染 → 每一笔从几 ms 变成几秒，用户以为程序卡死。
- **根因**：`renderDirty()` 的契约写了“返回本次要合成的区域”，但没人消费后清空；
  在 `LiquifyCanvas::stampAt/stampExternal` 里 `renderDirty(dirty)` 之后应立即
  `store->clearDirty()`。
- **修复建议**：`renderDirty()` 末尾自行清 dirty（或由 canvas 在 drawImage 后清），
  并加断言/日志（dirty 面积 > 图像 25% 时改走 `renderFull` 以避免多次合成）。
- **关联**：LQ-03。

### LQ-03 液化渲染逐像素 QColor，打开/确定时冻结数秒 (P1)

- **位置**：`src/media/filters/liquify/LiquifyEngine.cpp:68-87`、
  `src/media/filters/liquify/LiquifyDialog.cpp:38,47-50`
- **证据**：

```cpp
for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
        if (m_store->isFrozen(x, y))  result.setPixelColor(x, y, snapshot.pixelColor(x, y));
        else { const QPointF src = ...->mesh().inverseWarp(x, y);
               result.setPixelColor(x, y, bilinearSample(snapshot, src.x(), src.y())); }
    }
// bilinearSample 内部再 4 次 pixelColor() + QColor 构造
```

- **现象 / 复现路径**：对 12MP 图像打开液化 → 构造期 `m_canvas->rebuildCache()`
  即 `renderFull()` → 每像素约 5 次 `pixelColor()`（含 format 转换与 QColor 构造），
  12M×5 = 6000 万次调用，实测为秒级到十秒级 UI 冻结；点 OK 时 `resultImage()`
  又整幅重算一次。
- **根因**：用 QColor 高级 API 做像素级热循环，没有走 `scanLine()`/`bits()` 原始字节，
  也没有利用 OpenCV（项目已有依赖）做重映射。
- **修复建议**：`cv::remap` + 预计算 `map_x/map_y`（mesh 反变形场只在 stamp 时更新受影响
  区域），整幅 remap 由 OpenCV 多线程完成；冻结区用 mask 做 `copyTo`。预期 10~50 倍加速。
- **关联**：LQ-02、LQ-10。

### LQ-04 画布无缩放/平移，笔刷坐标恒等映射 (P1)

- **位置**：`src/media/filters/liquify/LiquifyCanvas.cpp:40-43,104-108`、
  `LiquifyDialog.cpp:63-68`
- **证据**：

```cpp
QPointF LiquifyCanvas::widgetToImage(const QPointF& p) const {
    return p;   // 1:1 映射：widget 像素 == 图像像素
}
m_canvas->setFixedSize(m_source.size());   // 4000×3000 的固定尺寸 widget
```

- **现象 / 复现路径**：打开 4000×3000 图像的液化对话框 → 画布固定 4000×3000（滚动条内），
  只能看到局部、无法缩放到全图，也无法在高倍下精修；笔刷半径是图像像素，视觉上
  随“看到的比例”变化剧烈。PS 的液化是“适配窗口 + 缩放 + 手形工具”。
- **根因**：刻意把坐标映射写成恒等以简化实现；`resizeEvent` 注释明确“No re-layout of cache”。
- **修复建议**：引入 `m_zoom`/`m_pan`，`widgetToImage = (p - pan)/zoom`；渲染缓存按缩放
  显示（`QPainter::drawImage(rect, m_cache)`），笔刷半径除以 zoom 换算；加滚轮缩放 +
  空格拖拽。工作量约 1~2 人天。
- **关联**：LQ-01、LQ-12。

### LQ-05 ONNX 模型路径硬编码到开发机 (P1)

- **位置**：`src/media/filters/liquify/FaceDetector.cpp:51-59`
- **证据**：

```cpp
bool FaceDetector::loadDefaults() {
    const QString detectPath = QStringLiteral(
        "D:/Collide/MyCode/QT6Projects/MultiDoc/third_party/landmark/det_10g.onnx");
    const QString landmarkPath = QStringLiteral(
        "D:/Collide/MyCode/QT6Projects/MultiDoc/third_party/landmark/2d106det.onnx");
```

- **现象 / 复现路径**：任何其它机器/安装目录/打包版本上，`loadDetectModel()` 失败 →
  `isLoaded()==false` → 点 “Detect Faces” 只把标题改成 “(face detector unavailable)”，
  面部感知液化整体不可用（且 UI 不解释原因）。
- **根因**：本地快捷实现；没有资源部署方案（CMake 里没有 copy/embed 这两个 ONNX）。
- **修复建议**：CMake `configure_file`/`qt_add_resources` 或安装时把模型放到
  `QStandardPaths::AppDataLocation/model/`；`loadDefaults()` 依次尝试
  `applicationDirPath()/model`、资源 `:/landmark/`、`AppDataLocation`；
  失败时把原因写进 UI（而非只改标题）。
- **关联**：LQ-06、LQ-07（模型格式假设）。

### LQ-06 cv::dnn 的 forward() 无异常保护 (P1)

- **位置**：`src/media/filters/liquify/FaceDetector.cpp:185-186,236-237`
- **证据**：

```cpp
m_impl->detectNet.setInput(detBlob);
cv::Mat detOut = m_impl->detectNet.forward();     // 未 try/catch
...
m_impl->landmarkNet.setInput(lmBlob);
cv::Mat lmOut = m_impl->landmarkNet.forward();    // 未 try/catch
```

- **现象 / 复现路径**：模型文件损坏/被替换成其它版本/OpenCV 不支持的算子 →
  `forward()` 抛 `cv::Exception` → 穿过 `onDetectFacesRequested()` 的按钮槽 →
  Qt 事件循环不捕获 → `std::terminate()`。`loadDetectModel` 只保护了 `readNetFromONNX`。
- **根因**：只对“读模型”做了错误处理，没有对“跑模型”。
- **修复建议**：把整段 `detect()` 包进 `try { ... } catch (const cv::Exception& e)
  { LOG_WARN(...); return {}; }`；对输出 shape 做显式校验（dims/size/total）。
- **关联**：CP-09（同一类“异常无边界”问题）。

### LQ-07 人脸检测无 NMS、无坐标 clamp，坐标/关键点语义存疑 (P1)

- **位置**：`src/media/filters/liquify/FaceDetector.cpp:180-252`
- **证据**：

```cpp
for (int i = 0; i < rows; ++i) {
    const float* row = data + i * cols;
    if (row[4] < 0.5f) continue;
    const int x1 = int(row[0] * scaleX), y1 = int(row[1] * scaleY);
    const int x2 = int(row[2] * scaleX), y2 = int(row[3] * scaleY);
    boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);   // 未 NMS、未 clamp 到图像范围
}
...
QPointF mapPt = [&](int idx) { return QPointF(box.x + lm[2*idx] * box.width, ...); }
```

- **现象 / 复现路径**：一张正脸常被列为多个 Face（RetinaFace 解码后多条高分框）；
  人脸左下角越界时 `crop` 用 `std::max(0,...)` 只修了原点，宽度仍可能超出 → 采样越界 ROI
  被 OpenCV 断言或裁出错位关键点。另外两处 `[待确认]`：
  ① `2d106det` 的输出惯例是 `[-1,1]`（InsightFace 里要 `(pred+1)*96`），代码按 `[0,1]`
  映射 → 若属实则所有关键点整体偏移/落在框外；
  ② `landmark_indices`（36..47、68..81）用的是 **68 点 iBUG** 编号，与 106 点模型的实际
  语义不一致；③ 关键点 ROI 是 bbox+10% 而不是正方形，`blobFromImage` 会各向异性缩放。
- **根因**：检测后处理按“理想输出”直接写，没有 NMS/边界处理/输出归一化校验；
  模型 API 契约未验证。
- **修复建议**：`cv::dnn::NMSBoxes(boxes, scores, 0.5f, 0.4f, keep)`；box 与 crop 一律
  `& imageRect`；对 `lm` 先按模型真实归一化（读模型文档/打印输出范围）再映射；ROI 扩成
  正方形并记录 106 点真实索引表。补单测：一张人脸 → 恰好 1 个 Face，8 个 anchor 落在
  bbox 内。
- **关联**：LQ-05、LQ-06、LQ-13。

### LQ-08 调整网格尺寸会清空全部液化结果 (P1)

- **位置**：`src/media/filters/liquify/LiquifyDialog.cpp:122-126`、
  `LiquifyBackingStore.cpp:16-21`、`LiquifyMesh.cpp:8-19`
- **证据**：

```cpp
void LiquifyDialog::onMeshSizeChanged(int size) {
    m_store->rebuildMesh(size);   // build() 里 m_vertices.clear() + 全部 displacement=0
    m_canvas->rebuildCache();     // 回到原图
}
```

- **现象 / 复现路径**：用户先推了几十笔，再想“把网格调细一点” → 所有形变瞬间消失，
  dialog 内也没有 undo（LQ-13）。PS 改网格密度时保留形变场。
- **根因**：`rebuildMesh()` 语义是“重建”，但没有把旧 mesh 的位移重采样到新网格。
- **修复建议**：重建前用旧 mesh 的 `displacementAt()` 对新网格每个顶点采样旧位移
  （双线性/重心插值），再 `build()` + 回填；`onResetRequested` 才是清零。
- **关联**：LQ-09、LQ-13。

### LQ-09 网格分辨率过低，局部笔刷被抹平 (P1)

- **位置**：`src/media/filters/liquify/LiquifyBackingStore.cpp:8-21`、
  `LiquifyOptionsPanel.cpp:84-92`、`LiquifyMesh.cpp:170-259`
- **证据**：

```cpp
void LiquifyBackingStore::setSnapshot(const QImage& snapshot) {
    ...
    rebuildMesh(5);                       // 固定 5×5 = 25 个控制点覆盖整幅图
}
// panel 范围 2..32，即最密也只有 32×32
```

- **现象 / 复现路径**：4000×3000 图上 5×5 网格 → 单元格约 1000×750 px；半径 50px 的笔刷
  只影响 0~2 个顶点，反变形在三角形内线性插值 → 形变被“拉平”成整块区域的仿射位移，
  看起来像整体平移/拉伸而不是局部捏脸。即便调到 32×32，单元格仍约 125px，无法做
  眼、唇级别的精修（PS 的液化网格密度可达 ~1 控制点/几像素）。
- **根因**：把 `meshSize` 当“顶点数/边”而不是“控制点间距”，默认值与图像尺寸无关。
- **修复建议**：按 `max(2, imageSize / cellSize)` 计算行列数（cell ≈ 8~16 px，
  并给上限保护，如 512×512 = 26 万顶点，2MB 位移数据），`meshSize` 滑杆映射为 cell 大小；
  `applyTool` 用空间哈希只遍历笔刷附近顶点（当前是每次全量 O(V)）。
- **关联**：LQ-03（渲染成本随顶点数上升）、LQ-12。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 图层混合模式 | 27 种 + 图层混合颜色带 | 15 种枚举，实际实现 12 种（Normal/Multiply/Screen/Overlay/SoftLight/HardLight/ColorDodge/ColorBurn/Darken/Lighten/Difference/Exclusion）；Hue/Sat/Color/Luminosity 静默退化为 Normal（BlendMode.cpp:301-309） | 部分 | P2 |
| 图层混合真实效果 | SoftLight 用 PS 官方公式 | 注释自称 Pegtop 近似（BlendMode.cpp:110-114），与 PS 有可见差异 | 有但错 | P3 |
| 图层蒙版 | 像素/矢量蒙版，羽化/密度/反相，隐藏=透明 | 渲染只用旧 `layerMask` 做**颜色乘法**：遮住=变黑、密度/羽化/反相/矢量蒙版全部无效 | 有但错 | P1 |
| 不透明度 | 每层 + 组 + 填充不透明度 | 每层/组 opacity 一致（legacy 与 DAG 都对）；无“填充不透明度” | 部分 | P2 |
| 图层组 | 组可带蒙版/混合模式/隔离混合 | 组 opacity 生效；组蒙版与组混合模式被忽略（两条路径一致）；无穿透/隔离选项 | 部分 | P2 |
| 调整层 | 曲线/色阶/色相饱和/可选颜色…无损、可再编辑 | 只有「256×1 LUT」一种模型（Adjustment 层），只能做曲线/色阶类；无参数持久化/重编辑 | 部分 | P2 |
| 智能对象 / 智能滤镜 | 智能滤镜可反复重编辑参数、可开关、可叠多层 | 仅 SmartObject 可挂链；每次渲染重跑 filter（无缓存）；只能改 strength，不能重新开对话框调参；子层还存了一份冗余过滤结果 | 部分 | P1 |
| GPU 加速 | Camera Raw / 画布 GPU 合成 | 有 3 个文件与 UI 开关，但渲染链未接入，恒走 CPU | 缺失 | P1 |
| 瓦片增量渲染 | 只重算脏矩形 | 有完整 DAG/TileKey/脏传播代码，但每次渲染重建 DAG + 新 cache，命中率为 0 | 有但错 | P1 |
| 滤镜数量/真实性 | 100+ 滤镜，全部参数化实时预览 | 49 个枚举：24 个独立实现（其中 2 个是空壳）、8 个别名复用、14 个返回 nullptr 且菜单置灰；6 个已启用滤镜实际无参数 UI | 部分 | P1 |
| 滤镜预览 | 全分辨率实时预览、可对比、可取消 | Apply 才出预览，非模态、无前后对比；关闭方式不当会残留（CP-14） | 部分 | P1 |
| 液化 | 网格可调、6 种工具 + 冻结/解冻蒙版 + 面部识别、实时、可保存网格 | 网格固定 5×5（上限 32）；工具/尺寸/压力不生效；无冻结；面部识别路径写死、无 NMS；点击 OK 时整图逐像素重渲染 | 部分 | P0/P1 |
| 液化非破坏 | 智能滤镜形式可重编辑 | 一次性写回像素（且 CP-11 会破坏图层栈） | 缺失 | P2 |
| 历史记录面板 | 每条命令可点、有图标/名称 | 只有线性 undo/redo，无面板；滤镜命令名在别名场景下显示错误 kind | 部分 | P3 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 高吞吐瓦片合成（blend/蒙版/滤镜） | QT-3RD | QPainter 的 `CompositionMode` 只有 15 种且无 PS 公式；CPU 上逐像素太慢 | 引入 OpenCV `cv::UMat`+T-API/`cv::parallel_for_`，或 Halide/ISPC；长期走 GPU（Qt RHI/OpenGL compute）。CV 已是现有依赖，零新增体积 |
| 真 GPU 合成（FBO/纹理/shader） | QT-NATIVE | Qt 自带 `QOpenGLFramebufferObject`/`QOpenGLShaderProgram`/`QRhi`，本项目已引入 Qt6::OpenGL 但只做了 pass-through | 把 `RenderGraph` 的瓦片合成搬进一个常驻 context 的 RHI 后端；注意本项目已经踩的坑（跨线程 context、readback 翻转） |
| 液化网格形变实时预览 | QT-EFFORT | 现有实现逐像素 `pixelColor`（QColor 转换比 `cv::remap` 慢 2~3 个数量级） | 用 `QImage::scanLine` 直接读写 + 预计算三角索引；或 `cv::remap` + `cv::Mat` 反变形场。约 5~8 人天 |
| 液化 GPU 版 | QT-3RD | PS 液化是 GPU 网格变形 | OpenGL 顶点/片段 shader 或 CUDA/OpenCL（可后期）。3~5 人天（含回退） |
| 人脸 106 点关键点 | QT-3RD | Qt 无此能力 | 已引入 ONNX（InsightFace buffalo_l）；建议同时接 ONNX Runtime 或 OpenCV DNN 的正确预处理；模型体积 buffalo_l ≈ 300 MB，建议换 `det_500m`+`2d106det` 精简版 |
| 图像卷积类滤镜 | QT-3RD | QImage 无卷积 | OpenCV `filter2D/GaussianBlur/bilateralFilter`（已用），复杂滤镜（表面模糊/导向滤波/Non-Local Means）需 `cv::ximgproc` 或自写 |
| 撤销栈内存治理 | QT-NATIVE | `QUndoStack::setUndoLimit` 只有条数，没有字节上限 | 自定义 `QUndoStack` 子类 + 命令暴露 `costInBytes()`，按字节裁剪 |
| 工具栏/面板式滤镜参数 UI | QT-NATIVE | QSlider/QSpinBox 已用，但 sync 用 `void*` 存成员指针 | 用 `QFormLayout` + 独立 `FilterParams` POD + `QDataWidgetMapper`（Qt 原生） |
| 滤镜预览“前后对比” | QT-NATIVE | 未实现 | 一个 `QGraphicsOpacityEffect` 或自绘分屏；纯 Qt 可做，1 人天 |
| 大图异步渲染 | QT-NATIVE | 已有 EngineContext/线程池，但 `renderAsync` 未把 engine 传进 compositor | 把 engine 一路传到 `RenderGraph::execute(engine)`；注意 CP-04 的挂死风险要先修 |

**结论**：本模块没有真正的 `QT-LIMIT` 项——渲染/滤镜/液化都能用 Qt + OpenCV（+RHI/ONNX）
实现。真正的瓶颈是“没有 per-pixel alpha 的合成模型”和“每次重算的渲染架构”，属设计问题
而非 Qt 局限。

## 5. 设计问题

1. **双渲染路径没有安全阀（P8 最大风险）**：`m_compositorEnabled` 默认 true
   （LayerStack.h:327），而 `setCompositorEnabled()` 在 src/ 中**无任何调用点**——没有设置页、
   没有菜单、没有配置项、没有环境变量。也就是说 P8 之后如果 compositor 出现 parity 缺口
   （CP-01/CP-08/CP-11 都是），用户侧**没有任何回退手段**，只能改代码重编译。同时
   `LayerStack.cpp:1750` 的注释还写着“Default is OFF”，与 header 的 `= true` 矛盾，
   维护者极易误判。**建议**：立刻加一个隐藏开关（QSettings/环境变量）+ 渲染失败自动回退，
   并把注释修正。
2. **“增量”名不副实**：TileKey/version/markDirty/invalidateNode/executeAndReuse/
   CompositeStrategy 全套基础设施只被 tests 使用（CP-03）。这属于“架构先行、接线未做”，
   风险是后续维护者基于错误假设做优化（以为有缓存而省 clone，实际没有）。
3. **节点契约不严**：`compute()` 的注释只说“返回尺寸必须匹配”，没说“不得修改
   `requestNodeTile()` 的返回值”（CP-06 就踩了）、没说线程安全责任归属（CP-05）、
   没说类型必须 3ch（CP-01）。建议把 `CompositorNode` 的接口注释升级成可执行契约
   （debug 下断言：`tile.type()==CV_8UC3`、`tile.data != cached.data`）。
4. **缓存/内存之间没有权衡**：TileCache 无预算（CP-02）、撤销栈无预算（CP-17）、
   预览图存静态表（CP-15）、智能滤镜子层存冗余像素、`m_current`/`m_original`/base 层
   三份全图并存。整个模块缺少“一张图在内存里最多存在 N 份”的统一约束。
5. **状态双份**：`Layer::layerMask`（渲染用）vs `Layer::mask`（UI/画笔写）——CP-18；
   `ImageWindow::m_current`（合成结果）vs `LayerStack`（真实数据）在滤镜/液化写回时冲突——CP-11。
   这两处是“同一状态两份拷贝、只更新一份”的典型，也是本模块最容易产生“看起来生效了/
   实际没生效”的根源。
6. **错误处理风格混乱**：`CompositorGraph::topologicalOrder()` 用 C++ 异常表示环
   （CompositorGraph.cpp:70），而 Qt/OpenCV 项目其它地方全是 bool/LOG；`FaceDetector`
   只对 `readNetFromONNX` try/catch，对 `forward()` 不 catch。建议统一成
   `Result<T>` 或 `bool + out param + LOG`。
7. **UI 层被数据层反向依赖**：`filter::FilterCommand`/`filters::liquify::LiquifyCommand`
   直接 `#include "../imagewindow.h"` 并调用 `replaceCurrentImage()`；`LiquifyDialog`
   依赖 `ImageWindow` 的 QImage 约定（RPB/BGR）——导致 CP-12 这类通道序错误无人负责。
   建议在 `media/` 内定义一个 `IEditTarget`（`applyLayerEdit(layerId, mat)`）接口。
8. **硬编码路径**：`FaceDetector::loadDefaults()`（FaceDetector.cpp:52-55）与
   `third_party/landmark` 的绝对路径绑定开发机；同类问题在 CMake/资源部署上也会复发。
9. **测试覆盖偏“数学正确”，缺“用户路径正确”**：tst_P8_1_1 只比 3ch 矩阵逐像素；没有
   1ch/4ch/小图层/多图层+滤镜/对话框关闭等路径的测试——这正是 CP-01/08/11/14 能溜过去的原因。

## 6. 修复建议（按优先级）

**P0 立即修（1~2 天）**
- CP-01：adapter 里 base/bitmap 图像归一为 `CV_8UC3`；`loadFile()` 同时归一 `m_original`。
  验收：灰度 JPEG、带 alpha PNG 打开后画布正常显示且与 legacy 输出逐像素一致。
- CP-11：`FilterCommand`/`LiquifyCommand` 改为写目标图层（layerId）而不是 base 层；
  暂时无法分层时先 `flattenVisible()` 再处理。验收：两层文档套滤镜 → 撤销/重做 与
  “只对活动层生效”的用户预期一致。
- 加一个 **compositor 回退开关**（QSettings `render/compositor=0`）+ 启动参数，
  并把 CP-09 的 try/catch 接上（任何节点异常→自动回退 legacy 并提示）。风险最低、
  收益最大：让 P8 从“不可回退”变成“可回退”。

**P1 本轮修（3~5 天）**
- CP-12/CP-13：通道序统一到 `ImageProcessor::qImageToMatBgr()`；渐变映射 LUT 初始化。
- CP-14/CP-15：预览清理接到 `QDialog::finished` + `ImageWindow` 析构；注册表改 QPointer。
- CP-08/CP-16：padding 清零；USM 按通道数分支（加 1/3/4ch 单测）。
- CP-17：撤销栈按字节裁剪（自定义 `costInBytes`）。
- CP-02：TileCache 加 256 MB 预算 LRU；同时给 `RenderGraph` 拼接 mismatch 加日志。
- CP-04/CP-05/CP-10：并行路径用 future/屏障 + 检查 submit 返回值 + 节点 memo 加锁 +
  `execute()` 前置环检测。（若短期不启用 Parallel，建议直接删除 `executeParallel()`
  以免误用，或加 `#ifdef` 标注“未验证”。）
- CP-06/CP-07/CP-18：统一到 RGBA 合成 + `LayerMaskRenderer::buildAlpha()`（较大的
  重构，可与 layers 模块合并；先修 CP-06 的 in-place 与 CP-18 的双份状态）。
- GPU-01/GPU-02/GPU-03：要么把 GPU 接通并修正翻转/属性/线程问题，要么把 UI 标注为未接入。
  建议本轮**先做后者**（成本 0.5 天），GPU 真接入放到 P8.x 独立任务。
- LQ-01/LQ-02/LQ-05/LQ-06/LQ-07/LQ-08：液化工具参数下传、dirty 清空、模型路径、
  try/catch、NMS、网格重建保留位移。

**P2 后续**
- LQ-03（`scanLine`/`cv::remap` 重写渲染）、LQ-09/LQ-12（自适应网格）、LQ-11（冻结蒙版）、
  LQ-10（冻结叠加改用单通道直绘）。
- FLT-02（智能滤镜结果缓存）、FLT-03~06（USM/浮雕/4ch 分支）、FLT-07（补参数 UI）、
  FLT-08（菜单标注未实现）。
- GPU-04~07（属性位置、跨线程析构、re-probe、真实 shader）。
- CP-03/CP-02 的持久 DAG + 稳定节点 ID（架构级，见下）。

**架构级重构（1~2 周，P9 之前）**
- 合成模型从 `CV_8UC3 + 颜色乘法` 改为 `CV_8UC4 预乘 alpha + Porter-Duff over`，
  blend mode 只作用于覆盖区。这是蒙版/羽化/软边/矢量蒙版/液化边缘全部功能的前置。
- 持久化 DAG：`LayerId → 稳定 NodeId`，`CompositorGraph`/`TileCache` 挂到
  `ImageWindow`，逐层脏标记；这才是 P8 声称的“增量合成”。
- 渲染后端抽象收敛到 `IRenderTarget`（CPU/GPU 同接口），GPU 后端在一个常驻 context 的
  线程上串行执行，CPU 回退是一等公民。

## 7. 需要 Lead 协调的跨模块问题

1. **与 `layers` 审查员**：CP-07（蒙版=颜色乘法）、CP-18（`Layer::mask` 与
   `Layer::layerMask` 双份状态、`LayerMaskRenderer` 零调用）跨越
   `layers/` 与 `compositor/nodes/`，结论可能重叠，需要合并去重并统一修复方向
   （建议由 layers 模块牵头改 `LayerMaskRenderer`，compositor 侧只负责调用）。
2. **与 `ImageWindow / UI` 审查员**：CP-11（滤镜写回 base 层）、CP-14/CP-15（预览注册表）、
   CP-17（撤销内存）、CP-12（QImage↔Mat 通道序）都在 `imagewindow.cpp`/`mainwindow.cpp`，
   我的结论依赖它们的 `currentImageAsQImage`/`replaceCurrentImage`/`m_currentDirty` 状态机，
   需要交叉确认 `renderAsync` 与编辑操作的并发（背景线程渲图 + UI 线程改 `Layer::image`
   的竞争，见 §5.5）。
3. **与 CMake / 部署**：`third_party/landmark/*.onnx` 的定位方式需要改 CMake
   （`configure_file`/`qt_add_resources` 或 `QStandardPaths::AppDataLocation`），
   否则 LQ-05 无法在打包版修复；同时 GPU 接入需要确认 Qt6::OpenGL 在所有目标平台的可用性。
4. **与 `app/mainwindow` / 设置页**：GPU 三态菜单与设置页文案需要同步（GPU-01）；
   建议由 Lead 决定本轮是“接线”还是“隐藏未接入功能”。
5. **与 tests 负责人**：本模块所有 P0/P1 都缺对应回归测试（1ch/4ch 文档、小图层、
   多图层+滤镜撤销、对话框 X 关闭、并行渲染）。`tst_P8_1_1` 目前锁定了 compositor 的
   `m_compositorEnabled=true` 默认值，修 CP-01 时它会成为“必须一起改”的约束。

---

**模块健康度评分：4 / 10**

理由：架构意图清晰（DAG/瓦片/后端抽象分层、adapter 为 parity 写了很多注释、单测覆盖了
数学层），但**默认路径（compositor）存在两个 P0（黑屏、滤镜写坏图层栈）且没有回退开关**；
号称的增量渲染/缓存/GPU 三层都处于“代码写完但未接线”的状态，形成大量死代码与误导性注释；
蒙版模型缺 alpha 导致核心修图功能语义错误；液化/面部识别的交互参数被完全忽略、
ONNX 路径写死。可维护性尚可（命名/注释质量高于项目平均），但“完成度”与“声明”差距过大，
是本项目 P8 阶段最大的风险点。
