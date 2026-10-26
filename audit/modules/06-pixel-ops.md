# 06 · 像素算法层（ImageProcessor / 选区 / 蒙版 / 变换 / 画笔引擎 / 文字图元）— 审计报告

> 范围：`src/media/imageprocessor.{h,cpp}`、`graphicstextitem.{h,cpp}`、`texteditorframe.cpp`、
> `src/media/selection/**`(8)、`src/media/masks/**`(4)、`src/media/transform/**`(6)、`src/media/brushes/**`(17)
> —— 共 40 文件 / 约 8,700 行（实际行数比任务书略多，见各文件）。
> 上下文另读：`imagewindow/ImageCanvas.cpp`（MarchingAnts 消费端）、`imagewindow.cpp`（选区/文字/命令）、
> `tools/Brush.cpp`、`tools/TransformTool.cpp`、`imagewindow/AdjustmentPanel.cpp`、`app/mainwindow.cpp`、`src/media/CMakeLists.txt`。
> 审查者：audit-pixel

## 1. 模块概览

本模块是"像素真相层"：`ImageProcessor` 是唯一的 cv::Mat↔QImage 桥 + 算法库（直方图/统计/滤镜/LUT/变换），
被 `ImageWindow::renderToView`（每帧显示）、`AdjustmentPanel`（色彩调整）、`MosaicTool`、`CloneTool`/`RedEyeTool`、
`TransformTool`、`FilterStrategy` 调用。`selection::SelectionModel` 持有全图 1 字节 Alpha8 蒙版（0=未选 / 255=选中）
与 bbox，由 `SelectionStrategy` 的 4 个策略（Rect/Lasso/MagicWand/ColorRange）生成手势蒙版，
`MarchingAnts` 只提供 200ms 相位定时器，真正的蚂蚁线由 `ImageCanvas::drawForeground` 画；
`SelectionCommand` 只覆盖 SelectAll/Deselect/Invert 三个高级操作。
`masks::ColorRange/RefineEdge` 被 `mainwindow.cpp` 的"颜色范围/调整边缘"菜单调用。
`transform::TransformBox/Math/Command` 是 Ctrl+T 的几何/矩阵/撤销三件套，被 `TransformTool` 驱动。
`brushes::BrushEngine` 负责 stamp 生成 + 落笔，被 `tools/Brush.cpp` 每帧多次调用；`BrushPreset/AbrParser/BrushPickerDialog`
是预设/导入/浏览层（当前均无调用方）。文字侧 `GraphicsTextItem` 是 QGraphicsTextItem 派生的可编辑文字对象，
`texteditorframe.cpp` 是**未被任何 CMake target 引用的孤儿文件**。

### 1.1 七个重点问题的逐项结论

| # | 重点 | 结论 |
|---|---|---|
| 1 | cv::Mat↔QImage | 转换点集中在 `imageprocessor.cpp:112-153`，**4 通道分支格式选型错误**（RGBA 数据配 `Format_RGB888`）；`qImageToMat` 一次往返 3~4 份全图；另有 `ImageWindow::currentImageAsQImage`/`layerStackAsQImage` 两套重复实现，三处行为不一致 |
| 2 | ImageProcessor 算法 | `invert` 4 通道把 alpha 清零、`hueShift` 不回绕、`sharpen` 直接减 abs 拉普拉斯、`computeStats` 的 min/max 只扫前 3 列、`pixelAt` 4 通道越位 + 源内存 in-place cvtColor；LUT 里 4/8 个是退化实现 |
| 3 | 选区 | 蒙版为二进制 0/255（PS 是 8-bit 可抗锯齿蒙版）；4 种模式实现正确；蚂蚁线只画 bbox 矩形；选区改动不可撤销；`grow/shrink/smooth` 的 memcpy 忽略 stride（必现错位）；bbox/像素操作几乎只认 bbox 不认形状 |
| 4 | masks | ColorRange 真实现但**每像素做一次 1×1 cvtColor**（24MP 冻结数分钟）；RefineEdge 是 4 个粗暴算子（smooth==feather 语义重复，shift 只是加常数偏置，在二值蒙版上等价于"全选/蒙版损坏"），PS 的 Radius/Edge Detect/Decontaminate 全缺 |
| 5 | transform | `qTransformToAffine` 线性部转置（旋转方向反）；`handlePos/hitTest` 不认 Distort 角点与旋转；Distort 下 Center 拖动抹掉 4 角；Rotate 与 Scale 矩阵不复合；Shift+Skew 恒为 no-op；命令整图双快照 ×200 步 |
| 6 | brushes | 13 种 BrushKind **只是标签**（引擎无 kind 分支，Smudge/Eraser 也走 Normal paint）；压感未接通（恒 1.0）；Multiply/Screen 三通道全用蓝色分量；无笔触累积缓冲（opacity 叠加失控）；stamp 每笔现算无缓存；ABR 解析器无调用方且拒绝 v9/v10 |
| 7 | text item | 旋转围绕 `m_pos`（真实视觉中心是 `m_pos+localCenter`）→ 旋转时文字漂移；手柄随 scale 缩放；撤销只记文本+旋转，不记位置/缩放；文字栅格化完全忽略 item 变换 |

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| 06-01 | P0 | imageprocessor.cpp:375-388 | logic/data-loss | 自由变换的旋转/斜切结果与包围盒方向相反或扭曲错位，保存后内容错误 | QTransform→cv 仿射矩阵时把 `m12/m21` 当行/列转置写入 2×3 | `M=[m11 m21 dx; m12 m22 dy]`（Qt 映射是 x'=m11x+m21y+dx） |
| 06-02 | P1 | imageprocessor.cpp:126-135 | logic | 任何 4 通道图显示错色、从第 2 个像素起逐像素错位 | `COLOR_BGRA2RGBA` 产出 4 字节/像素，却声明 `Format_RGB888`（3 字节/像素） | 4 通道用 `Format_RGBA8888`，1 通道用 `Format_Grayscale8` |
| 06-03 | P1 | imageprocessor.cpp:172-174 | logic | 对 RGBA 图执行反相后整图完全透明 | `cv::Scalar(255,255,255)` 的第 4 分量为 0，alpha 被减成 0 | 4 通道时第 4 分量用 255（或只对前 3 通道取反） |
| 06-04 | P1 | imageprocessor.cpp:828-950；AdjustmentPanel.cpp:657,660,668 | logic | HSL 的 Hue/Saturation 滑条、通道混合器、黑白混色滑条拖了没反应或结果与原图无关 | `buildHueSatLUT` 用 `Q_UNUSED(hueSatPairs)` 只调 lightness；B&W 只把 6 色平均当亮度 scale；ColorBalance 只用 cyanRed；Vibrance 直接 identity | HSL/B&W/ChannelMixer 必须走 per-pixel 3D 变换，不能塞进 1D LUT |
| 06-05 | P1 | imageprocessor.cpp:406-420 | logic | 色相偏移在 0/180 附近颜色被压死（不循环），且每级最小 2° | `ch[0] += shift` 对 uchar 饱和，H 是环形量却没做 `(h+shift+180)%180`；`int(deg/2.0)` 丢精度 | 用 `cv::LUT` 做 H 通道环形查表，并保留浮点角度 |
| 06-06 | P1 | selection/SelectionModel.cpp:311,348,386 | data-loss | `grow/shrink/smooth` 后蒙版整体沿对角线错位（宽度非 4 倍数必现） | `memcpy(bits, data, W*H)` 忽略 `bytesPerLine()` padding | 按行 `memcpy(dst+y*stride, src+y*W, W)`，或统一走 cv::Mat |
| 06-07 | P1 | selection/SelectionModel.cpp:223-276 | logic | "羽化"后边缘仍是硬边（只是整体胀/缩 1~2px），大半径时界面卡死 | box blur 后 `v>=128 ? 255 : 0` 重新二值化；O(W·H·r) 三重循环，注释写 3 passes 实际 2 | 羽化结果保留 0..255 软值；用两次可分离 box blur 或 cv::GaussianBlur |
| 06-08 | P1 | selection/SelectionStrategy.cpp:185,200,225 | logic | 容差 0 时点红色会同时选中所有同亮度(如 0,175,0)的无关颜色 | 容差比较只用 `qGray()` 亮度差，PS 用 RGB/HSB 逐通道差 | 用 `QColor::red/green/blue` 三通道平方距离（或转 HSV）比较 |
| 06-09 | P1 | selection/SelectionStrategy.cpp:200,225 | perf | 魔棒点一下 24MP 图要几十秒且无法取消 | BFS/全图扫描每像素调 `QImage::pixel()`（每次带格式转换） | 先 `constScanLine` 转成 `QRgb*`/灰度数组再扫描，或在 cv::Mat 上做 floodFill |
| 06-10 | P1 | selection/SelectionStrategy.cpp:109；mainwindow.cpp:1016 | logic | 勾选羽化后在 >8.4MP 的图上拉套索，得到噪点状乱选区 | 积分图 `std::vector<int>` 累加 255×W×H，超过 INT_MAX 有符号溢出（UB） | 用 `int64_t`/`double` 累加，或直接用 cv 32F 积分图 |
| 06-11 | P1 | imagewindow/ImageCanvas.cpp:328-338 | ux | 套索/魔棒选区的蚂蚁线是一个矩形框，不是选区轮廓 | `drawRect(sceneBbox)`，注释自认 "For P0, use bbox" | 用 `QPainterPath` 沿蒙版边界生成轮廓（marching squares / `QRegion`） |
| 06-12 | P1 | imagewindow/ImageCanvas.cpp:303-326 | memory/perf | 打开蒙版叠加后，大图每 200ms 重建两张全图 ARGB32 并做 O(W·H) 逐像素循环 → 界面卡死/OOM | 每帧 `QImage(mask.size(), ARGB32_Premultiplied)` ×2 + 3 重循环；24MP 约 384MB/帧 | 缓存 tint 图像，仅在 mask 变化时重建；或改用 `QImage::Format_Alpha8` 直接合成 |
| 06-13 | P1 | selection/SelectionCommand.h:9-12；mainwindow.cpp:2133-2159 | logic | 任何套索/矩形/魔棒选区改动都无法撤销（Ctrl+Z 不动或跳到更早的图像操作） | 手势路径直接 `SelectionModel::setMask`，从不 push 命令 | 手势 release 时 push 一条 mask before/after 命令；redo 必须恢复 after |
| 06-14 | P1 | selection/SelectionCommand.cpp:15,37；imagewindow.cpp:564 | memory | 选区操作 200 步后内存可达数 GB | 每条命令存 before+after 两张全图 Alpha8，`setUndoLimit(200)` 无字节上限 | 只存 before（after 由 redo 重算）或存 RLE/bbox 裁剪后的 mask |
| 06-15 | P1 | masks/ColorRange.cpp:18-27,91-101 | perf | 对 24MP 图层执行"颜色范围"冻结数分钟，任务管理器可见 100% 单核 | 每像素构造 1×1 Mat 并调一次 `cv::cvtColor`（2400 万次） | 整图一次 `cvtColor(BGR2HSV)`，逐像素只做减法/查表 |
| 06-16 | P1 | masks/RefineEdge.cpp:66-78 | data-loss | 调整边缘里 Shift>0 → 整张蒙版全亮（图层全显示）；Shift<0 → 全图半透明 | Shift 只是 `v + shift*2`，二值蒙版 0 被抬到 ≥2 即"选中" | 用距离场/阈值位移实现真正边缘位移，二值输入需先羽化或做形态学 |
| 06-17 | P1 | transform/TransformBox.cpp:45-89 | logic/ux | Distort 拖完一个角后，手柄和框线还在原位；旋转后手柄命中区与视觉错位 | `handlePos()` 永远用 `m_rect` 四角，不读 `m_corners`、不应用 `m_rotationDeg` | handlePos 按 mode 取点：Distort 用 m_corners，其余模式乘旋转矩阵 |
| 06-18 | P1 | transform/TransformBox.cpp:223-229 | data-loss | Distort 模式点中心拖动，之前拖好的 4 角变形立刻复原 | Center 分支 `m_rect.translate(d); syncCornersFromRect();` 覆盖 m_corners | Center 只需 4 角同加 delta，禁止 syncCornersFromRect |
| 06-19 | P1 | transform/TransformMath.cpp:28-56；TransformTool.cpp:263 | logic | 一轮拖动只能施加一种变换；旋转提交后再做非等比缩放，缩放轴是图像轴而非框的旋转轴（物体会被拉斜）；box 状态不随已烘焙的像素更新 | `computeTransform*` 按 mode 只返回单个矩阵，rotation 从不参与 Scale/Skew；commit 后不更新 box.rect() | 维护 `T = R(rot)·S·K·Translate` 的合成矩阵，mode 只决定哪一项被本次拖动修改 |
| 06-20 | P1 | transform/TransformMath.cpp:208-221 | logic | Skew 模式按住 Shift 拖动完全没反应 | `constrainedSkewPoint` 把驱动量归零（Top/Bottom 固定 y，而 skewY 由 dy 驱动） | Shift 应约束"另一轴保持"，不能固定本轴；并改为 X 拖动驱动水平斜切 |
| 06-21 | P1 | transform/TransformCommand.cpp:18-19,32-33；imagewindow.cpp:564 | memory | 每次变换占 2 份全图；24MP 图 100 次变换 ≈ 14GB | before/after 均 `clone()`，undoLimit=200 无字节上限 | 只存 after（undo 用当前图）；或改为"图层+矩阵"命令，不存像素 |
| 06-22 | P1 | transform/TransformMath.cpp:249-266（TransformBox::toTransform） | logic | 该 API 只返回 "平移到 topLeft"，无 scale/rotate/skew，谁用谁错 | P0-6.1 遗留简化实现未删 | 删掉或用 `computeTransform` 委托实现 |
| 06-23 | P1 | tools/TransformTool.cpp:38-49（跨模块） | logic | Ctrl+T 的初始框永远是 800×600，与图像/选区无关 | `if (!sel && !sel->isEmpty())` 条件恒假（null 时还会解引用） | `if (sel && !sel->isEmpty()) initRect = sel->boundingRect(); else initRect = image rect;` |
| 06-24 | P1 | tools/TransformTool.cpp:215-259 + imageprocessor.cpp:353 | data-loss | 自由变换作用在整幅合成图上，旋转/缩放会削掉画布四角、连带改掉其他图层内容 | commit 时 `before = currentImage().clone()` 后对整图 warp，边界填透明黑 | Transform 应作用于选区/图层，并在图层上记录 offset/矩阵，不 flatten 整图 |
| 06-25 | P1 | brushes/BrushKind.h:26-43；BrushPreset.cpp:364-531；tools/Brush.cpp:242 | logic | 13 种画笔视觉上只有大小/软硬差别；选 Smudge/Eraser 仍然是"用前景色画" | kind 只用于 label 和 `kindNeedsGpu`（后者也无调用方）；`applyStamp(..., mode=0)` 恒 Paint | 建立 kind→(shape, dynamics, mode) 映射表并在引擎/工具里分支：Eraser→mode1，Smudge→取色混合，Watercolor→湿边累积 |
| 06-26 | P1 | brushes/BrushEngine.cpp:249-252 | logic | Multiply/Screen 混合下红色画笔画出黑色（或整笔颜色由蓝色分量决定） | 两个分支的三通道循环都用 `b_color` | 分支内分别取 `p[c]` 与 `color_channel[c]`（B/G/R 各自数组） |
| 06-27 | P1 | brushes/BrushEngine.cpp:77-111 | ui-visual | 100% 硬度的圆笔仍是明显软边，实际有效直径只有设定值 ~60%，与 PS 硬边笔差异巨大 | `(1-d/r)^(1.4+0.6h)`：h=1 时半径中点强度仅 0.25，全半径都在衰减 | 采用 PS 形状：`d<=r·hardness` 时 1.0，之后线性/平滑过渡到 0（外加 1~2px AA） |
| 06-28 | P1 | tools/Brush.cpp:144,173；brushengine 全线 | logic | 数位板压感、Size Jitter/Transfer/角度抖动全部无效，笔触粗细不随压力变化 | 所有 `paintSingleStamp(host, p, 1.0)` 恒传 pressure=1.0；`PressureCurve` 实例化后只 delete 从未使用 | 在 mouse/tablet 事件里读 `QTabletEvent::pressure()`，经 `PressureCurve::evaluate` 后传入；同时补 `resolve()` 里的 angle/roundness/flip jitter |
| 06-29 | P1 | brushes/BrushEngine.cpp:210 | ui-visual | 30% 不透明度的笔刷一笔下来很快变成 100% 不透明；Flow/Opacity 行为混同 | 每个 stamp 独立做 `alpha = stamp*opacity*flow` 直接前混，无"笔触覆盖上限"缓冲 | 引入 per-stroke alpha 累积缓冲（PS 的 "opacity 封顶 / flow 逐次累加"），每笔结束再合到图层 |
| 06-30 | P1 | brushes/BrushEngine.cpp:25-75,77-183 | perf | 大笔刷（≥300px）拖动明显掉帧，每帧要做几千万次 `pow/sqrt` | 每个 stamp 都重新生成 falloff+椭圆+噪声，无缓存；`std::pow` 在双重循环里 | 以 (size,hardness,roundness,angle,type) 为 key 缓存 stamp（LRU），并用查表代替 pow |
| 06-31 | P1 | brushes/AbrParser.cpp:171-174,200-202；BrushPickerDialog.cpp:126-135 | design | ABR 导入功能完全不可用（点按钮只弹"将会实现"提示） | AbrParser 无任何调用方；v9/v10 直接报错返回；'t' 采样笔刷跳过；Picker 不调用解析器 | 接通 Picker→AbrParser→BrushPresetManager；补 v9/v10 描述符解析与采样位图（这条是 PS 画笔生态的入口） |
| 06-32 | P1 | brushes/BrushPreset.cpp:537-668；BrushPickerDialog.cpp:75-100 | design | 画笔预设面板在 UI 中不存在；有 13 个内置预设但无缩略图 | `BrushPresetManager`/`BrushPickerDialog`/`generateThumbnail` 均无调用方；Picker 用 QStringListModel 无 icon | 在画笔选项栏接出预设面板，图标用 `generateThumbnail` 缓存 |
| 06-33 | P1 | graphicstextitem.cpp:65-73,253-264 | logic | 旋转文字时文字绕一个偏移点公转（位置漂移），角度越大偏得越远 | `m_transform=T(c)R S T(-c)` 且 `setPos(m_pos)`：视觉中心是 `m_pos+c`，但旋转角按 `scenePos-m_pos` 计算 | 旋转用 `mapToScene(localRect().center())` 作为圆心，或令 `m_pos` 语义等于视觉中心（setPos(m_pos-c)） |
| 06-34 | P1 | texteditorframe.cpp（整文件 260 行） | build/design | 260 行代码完全不参与编译；文件内自述"类定义在 imagewindow.h"但该类不存在 | `src/media/CMakeLists.txt` 无该文件条目，全仓库无 `TextEditorFrame` 声明 | 删除该文件；若需要则补 class 声明并加入 CMake |
| 06-35 | P1 | masks/ColorRange（mainwindow.cpp:2753-2778 调用点） | logic | "颜色范围"用 3 个固定点（1/4、1/2、3/4 处）取样，用户无法点选颜色，结果不可预期 | 菜单用 QInputDialog 简化，采样点硬编码 | 做真正的取样 UI（吸管 + 预览），或至少用当前鼠标位置 |
| 06-36 | P2 | imageprocessor.cpp:193-205 | ui-visual | 锐化过度：边缘黑边/白边，暗部出现硬黑晕 | `subtract(in, absLap)` 无权重、无 clamp 前平滑，且 abs 把负响应翻成正 | 用 `in + amount*(in - GaussianBlur(in))`（USM），amount 可控 |
| 06-37 | P2 | imageprocessor.cpp:638-677 | logic | 统计面板 min/max 永远是前 3 列的极值；宽<3 的图直接抛异常 | `cv::minMaxLoc(bgr.col(c))` 把"通道"当"列"，且多通道 minMaxLoc 全通道混算 | 用 `cv::split` 后逐通道 `minMaxLoc` |
| 06-38 | P2 | imageprocessor.cpp:696-704 | crash/logic | 4 通道图取色数值错误；代码把 1×1 Mat 包在 `const` 源图内存上做 in-place cvtColor | `cv::Mat tmp(1,1,CV_8UC3, bgr.at<Vec3b>(y,x))` 取的是源像素地址（或临时对象地址）后 s 系列 | 先 `const Vec3b px = bgr.at<Vec3b>(y,x);` 再 `cv::Mat tmp(1,1,CV_8UC3, (void*)&px)`；推荐整图一次性转 HSV |
| 06-39 | P2 | imageprocessor.cpp:392-427 | logic | 对 4 通道图做饱和度/色相后 alpha 丢失（输出变 3 通道）；亮度/对比会改 alpha | `cvtColor` 链路固定 3 通道；`convertTo` 对全部通道（含 A）同乘 | 拆分 alpha 单独处理，最后 merge 回去 |
| 06-40 | P2 | imageprocessor.cpp:48-67,86-97 | logic | TIFF 的 "Deflate" 与 "LZW" 走同一路径（compression=99），PNG 压缩等级可能被忽略 | `setCompression(99)` 魔法数；Qt 各 image plugin 的 compression 语义不同 [待确认] | 按 format 查 Qt handler 文档设枚举值，失败回退默认；补单测校验文件头 |
| 06-41 | P2 | selection/SelectionModel.cpp:63-82 | logic | 含羽化软边的选区做反相后软边全部消失（变成硬边） | `(v==0)?255:0` 而不是 `255-v` | `row[x] = 255 - row[x]` |
| 06-42 | P2 | selection/SelectionModel.cpp:185-216,158-173 | perf | 每次选区变化全图扫描两遍（bbox + pixelCount），24MP 时几十 ms × 多次 | O(W·H) 逐像素，无增量/缓存 | bbox 从策略层直接给；pixelCount 用积分图或按需缓存 |
| 06-43 | P2 | selection/SelectionModel.cpp:392-412 | logic | 对选区做旋转/缩放后蒙版出现锯齿，且出现 1..254 的半透明值破坏"二值"约定 | QPainter 默认无平滑；未做阈值/抗锯齿处理；Alpha8 作画布行为未验证 [待确认] | 用 cv::warpAffine 在 CV_8UC1 上做变换，再按需阈值或保留 8-bit 语义并统一全链路 |
| 06-44 | P2 | selection/SelectionModel.cpp:32-42 | logic | 改变画布尺寸/裁切后选区被清空，PS 是裁剪/缩放随动 | `setSize()` 重建并 fill(0) | 按新旧尺寸做平移/裁剪复制，或至少保留交集 |
| 06-45 | P2 | selection/SelectionModel.cpp:28-30,101-105 | logic | 传入非 1 字节/像素的 QImage 会静默破坏蒙版 | 无 format 校验，按 1Bpp 硬 memcpy | 入口断言 `Format_Alpha8/Grayscale8` 或先 convertToFormat |
| 06-46 | P2 | selection/SelectionStrategy.cpp:103-142；mainwindow.cpp:1001-1045 | design | 同一套"羽化"在两处独立实现（结果不一致、都带 int 溢出） | MainWindow 直接内联积分图模糊，绕过 SelectionModel | 统一到 `SelectionModel::feather()` 一个实现 |
| 06-47 | P2 | selection/SelectionModel.cpp:223-412 | design | P3.6 的 feather/grow/shrink/smooth/transform 五个 API 在 UI 中不存在 | 全仓库无调用方（grep 0 hit） | 接 Select→Modify 菜单或在报告期内删除 |
| 06-48 | P2 | masks/RefineEdge.cpp:23-60 | logic | Smooth 与 Feather 滑条效果完全一样 | 二者都是同一个 `cv::GaussianBlur` | Smooth 应做边缘保持平滑（bilateral/guided filter），Feather 才做高斯 |
| 06-49 | P2 | masks/RefineEdge.cpp:51-59,70-77；ColorRange.cpp:111 | memory | 若传入非连续 Mat/多通道，输出后半未初始化、结果乱 | 直接用 `in.data` + `rows*cols`（忽略 step 与通道） | 断言 `isContinuous() && channels()==1` 或 clone 后处理 |
| 06-50 | P2 | masks/ColorRange.cpp:49-53 | ui-visual | 对预乘 ARGB32_Premultiplied 图像取样颜色偏暗，Fuzziness 判定偏差 | 直接把预乘数据当普通 BGRA 用 | 先 `convertToFormat(ARGB32)`（非预乘）再取通道 |
| 06-51 | P2 | transform/TransformBox.cpp:91-238 | logic | 拖动超过对边时无法翻转（直接不动）；rect 宽 0 时矩阵 NaN | `if (r.width()<1) return;` 放弃更新；`sx = w/w0` 无 0 保护 | 允许负宽高并用 `normalized()` 只在计算 anchor 时处理；w0<epsilon 时跳过 |
| 06-52 | P2 | transform/TransformMath.cpp:182-206 | logic | Shift 等比缩放时，向"左+下"拖角点，框会朝反方向翻 | 两个轴共用一个 `s` 并让符号互相覆盖 | sx/sy 各自取符号，比例用 max(|dx|/w0,|dy|/h0) 分别乘 |
| 06-53 | P2 | transform/TransformCommand.cpp:49-56 | design | `id()==9001` 但 `mergeWith` 恒 false，命令永不合并；id 与其他命令冲突风险 | 死代码 | 删除 id 覆盖或实现按 box 状态合并 |
| 06-54 | P2 | transform/TransformTool.cpp:71-103（跨模块） | ux | 自由变换没有实时预览，松手瞬间才跳变 | 拖动只改 box，commit 才 warp | 拖动时用 QGraphicsItem 变换做预览，落笔再 warp |
| 06-55 | P2 | brushes/BrushEngine.cpp:358-388 | logic | Size/Opacity/Flow 抖动只能"变小"不能"变大"；抖动不随 randomSeed 复现 | `size_factor = 1.0 - j`；`(void)randomSeed` 后仍用固定种子 thread_local rng | jitter 用 ±(j/2) 对称分布，rng 由 stroke seed 派生 |
| 06-56 | P2 | brushes/BrushEngine.cpp:294-320 | logic | 纹理功能即使接线也几乎无效果；黑色纹理处不衰减笔迹 | `newval = out*(1-blend+blend*factor)`，factor→0 时 blend→0 | `newval = out * lerp(1, factor, depth)` |
| 06-57 | P2 | brushes/BrushEngine.cpp:326-340 | logic | 双画笔只剩"两个 stamp 取 max"，size/spacing/scatter/count 全无效 | 参数未消费；secondary stamp 要求同尺寸 | 按 dual.size 重采样 secondary、按 count/scatter 多次叠加、按 PS 模式（darken/lighten/multiply）混合 |
| 06-58 | P2 | brushes/BrushPreset.cpp:50-69 | data-loss | 含采样位图的预设保存后再加载，位图消失（静默退化为普通圆笔） | JSON 写 `sampledBitmapPng`，读回只填字节数组，未解码成 `cv::Mat` | `shapeFromJson` 里 `QImage::fromData(...).convertToFormat(Grayscale8)` → cv::Mat |
| 06-59 | P2 | brushes/PressureCurve.cpp:11-14 | logic | 设了 Maximum=50% 后压力仍能到 100%；Minimum 同理失效 | 极端输入提前 `return 0/1`，跳过 min/max 映射 | 先做 min/max 映射（`out=min+(max-min)*t`）再处理端点 |
| 06-60 | P2 | brushes/BrushPreset.cpp:38-48 | logic | 手工编辑/损坏的 .vbrush 可写入 size<0、spacing=0 等，导致 1px 笔刷或除零/死循环 | `settingsFromJson` 无 clamp，BrushSettings 也没有校验 | 读入后统一 clamp 到 UI 范围 |
| 06-61 | P2 | brushes/AbrParser.cpp:99-111,152-174,189-206 | logic | 解析失败时错误信息为空；真实 ABR v6 的字段顺序/长度语义与注释实现可能不符 [待确认] | 多处 `return false` 不设 err；头部按注释而非抓包验证；tag 同时接受两种字节序 | 用真实 .abr 样本做 round-trip 测试并补齐错误信息 |
| 06-62 | P2 | brushes/AbrParser.cpp:136-217 | design | 整个解析器是死代码（无调用方） | 见 06-31 | 同 06-31 |
| 06-63 | P2 | tools/Brush.cpp:127-179,238-242；imagewindow.cpp:242-243（跨模块） | memory/perf | 每一笔全图 clone ×3，且每次落 stamp 都 `renderToView`（全图转 QImage+Pixmap）；24MP 图连画 200 笔约 28GB undo 内存 | 笔触备份/命令快照都是整图；预览刷新粒度过粗 | 笔画只在受影响 tile 上快照；预览走 dirtyRect + 局部合成 |
| 06-64 | P2 | graphicstextitem.cpp:152-178,350-392 | ui-visual | 文字缩到 30% 时 8 个手柄也缩到 30%，几乎点不到 | 手柄在 item local 坐标绘制，非 cosmetic | 用 `ItemIgnoresTransformations` 子 item 或 `p->setWorldMatrixEnabled(false)` |
| 06-65 | P2 | graphicstextitem.h:96-97；imagewindow.cpp:135-216 | logic | 文字缩放手柄拖完不能撤销；删除后 undo 恢复的文字丢失缩放/字体/颜色 | 命令只存 text+pos+rot；无 Resize op | 命令序列化完整几何+样式（`GraphicsTextItem::state()`/`applyState()`） |
| 06-66 | P2 | imageworker/layers/LayerStack.cpp:1389-1412（跨模块） | state-sync | 画布上的文字位置/旋转/缩放在图层栅格化时完全不生效（文字固定在画布中央偏上） | 栅格化重新用 `QFont+drawText` 画，未使用 GraphicsTextItem 的几何 | 文字图层栅格化直接渲染 QGraphicsTextItem 或共享同一份 layout 数据 |
| 06-67 | P3 | imageprocessor.cpp:157-168,214-288,455-518,681-706 | design | 大量 public 算法（直方图/统计/像素查询/drawText/toGray/toBinary）在 src 中无调用方 | 只有 tests/其他分支使用；产品面板缺失 | 要么接 UI（直方图面板/信息面板），要么标注 internal |
| 06-68 | P3 | imageprocessor.cpp:73-74；SelectionStrategy.h:13；TransformBox.cpp:13 | design | 注释与实现不符（"3 passes 实际 2"、"ColorRange 是 stub 但已实现"、"P0-6.3 完整实装"等） | 迭代未回写注释 | 统一清理注释，禁止留"未来实现"式描述 |
| 06-69 | P3 | brushes/BrushKind/names；BrushPreset.cpp:366-530 | i18n | 内置画笔名是中文硬编码，description 是英文 | 未走 tr()，无 .ts 条目 | 用 `QCoreApplication::translate` + 稳定 id |
| 06-70 | P3 | brushes/BrushEngine.cpp:405-430 | perf/ui | 生成 64×64 缩略图要逐像素 `setPixelColor`（最大 2000²=400 万次） | 无缓存、API 选型低效 | 直接把 stamp 转 QImage(Grayscale8)+色表，缩放一次 |

### 06-01 qTransformToAffine 把 2×3 矩阵转置，旋转/斜切方向反 (P0)

- **位置**：`src/media/imageprocessor.cpp:375-388`（消费端 `tools/TransformTool.cpp:247-259`）
- **证据**：

```cpp
Q_ASSERT(t.isAffine());
cv::Mat M(2, 3, CV_64F);
M.at<double>(0, 0) = t.m11();
M.at<double>(0, 1) = t.m12();   // ← 应为 m21
M.at<double>(0, 2) = t.dx();
M.at<double>(1, 0) = t.m21();   // ← 应为 m12
```

- **现象 / 复现路径**：Ctrl+T → Rotate 模式拖旋转手柄 → 框按顺时针转、松手后图像反着转（且角度与框差 2θ）；
  Skew 模式斜切出来的方向与框显示的相反。Scale 模式因 m12=m21=0 不受影响，所以"只有旋转/斜切坏"。
- **根因**：QTransform 的行向量映射是 `x' = m11*x + m21*y + dx; y' = m12*x + m22*y + dy`；
  而 `cv::warpAffine` 的 2×3 是 `u = a11*x + a12*y + b1; v = a21*x + a22*y + b2`。代码按"行优先"抄了 m12/m21，
  等价于对线性部分转置（旋转矩阵转置 = 反向旋转，剪切矩阵转置 = 另一方向剪切）。
- **修复建议**：`M = [m11, m21, dx; m12, m22, dy]`。补单测：对 rotate(30°)/shear(0.3,0)/复合变换，
  用 `QTransform::map(p)` 与 `cv::transform` 对 4 个角点逐点比对（容差 1e-9）。
- **关联**：TransformTool（tools 模块）、06-19/06-20。

### 06-02 matToQImage 的 4 通道分支格式选错 (P1)

- **位置**：`src/media/imageprocessor.cpp:126-135`；调用方 `imagewindow.cpp:1905`（每帧显示）、
  `CloneTool.cpp:133,153`、`RedEyeTool.cpp:57`、`TextOverlayController.cpp:179`
- **证据**：

```cpp
case 4:
    cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGBA);   // 输出 4 字节/像素
    break;
...
return QImage(rgb.data, rgb.cols, rgb.rows,
              static_cast<int>(rgb.step),
              QImage::Format_RGB888).copy();       // ← 声明 3 字节/像素
```

- **现象 / 复现路径**：任何 `CV_8UC4` 的 `m_current`/图层一旦进入此函数（`currentImageAsQImage` 已专门处理 4 通道，
  说明该类型在管线中真实存在，见 `imagewindow.cpp:787-791`、`mainwindow.cpp:2580`、`LayerMaskRenderer.h:13`），
  画面从第 2 个像素起颜色逐像素错位（B 通道吃到下一个像素的 alpha），整体呈斜向彩色条纹。
- **根因**：`Format_RGB888` 固定 3Bpp，Qt 按 `x*3` 取址，而数据是 RGBA 的 `x*4`；同时 alpha 被静默丢弃，
  半透明内容显示成全不透明。
- **修复建议**：4 通道返回 `QImage(..., Format_RGBA8888)`；1 通道直接 `Format_Grayscale8`（同时省 3 倍内存）；
  三处重复实现统一到 `ImageBridge` 一处（见 §5.1）。

### 06-03 invert() 把 4 通道 alpha 清零 (P1)

- **位置**：`src/media/imageprocessor.cpp:170-175`
- **证据**：

```cpp
cv::Mat mask = cv::Mat(in.size(), in.type(),
    in.channels() == 1 ? cv::Scalar::all(255) : cv::Scalar(255, 255, 255));
cv::subtract(mask, in, out);   // 4ch: 0 - alpha → 饱和为 0
```

- **现象**：对带 alpha 的图执行"反相"，图像变全透明（画面上什么都没有）。
- **根因**：`cv::Scalar(255,255,255)` 的第四个分量省略 = 0，`in.type()` 是 4 通道时 mask 的 A=0。
- **修复建议**：4 通道分支用 `cv::Scalar(255,255,255,255)`；注意语义上反相不应改 alpha。
- **关联**：当前 src 内无调用方（P3 直方图 API 同样无调用方），但属于会在接入时立即爆的雷。

### 06-04 4 个 LUT 是退化实现，调整面板已接线 (P1)

- **位置**：`src/media/imageprocessor.cpp:828-950`；UI 接线 `AdjustmentPanel.cpp:657,660,668`
- **证据**：

```cpp
cv::Mat ImageProcessor::buildHueSatLUT(const QVector<QPair<int,int>> &hueSatPairs, int lightness)
{
    Q_UNUSED(hueSatPairs);                     // HSL 的 Hue/Sat 全丢
    ...
}
const double scale = avg / 100.0;              // B&W：6 色平均只当亮度缩放
lut.at<uchar>(i) = cv::saturate_cast<uchar>(static_cast<double>(i) * scale);
```

- **现象 / 复现路径**：图像 → 调整 → HSL 页拖 Hue/Saturation 滑条：画面只有整体明暗变化；黑白页拖 6 色滑条：
  图不会变黑白，只是整体变亮/变暗；通道混合器拖 R/G/B：只有整体红偏移。
- **根因**：这三类调整本质是 per-pixel 的 3D/2D 映射（HSL 需要先转 HSL、B&W 需要三通道加权、ChannelMixer 需要 3×3 矩阵），
  被硬塞进"1D 256 项 LUT"接口，只能近似成亮度平移。`buildVibranceLUT` 更是直接 identity。
- **修复建议**：`AdjustmentPanel` 对 HSL/B&W/ChannelMixer 走 cv::Mat 全图路径（`cvtColor` + 逐像素/矩阵），
  不要再经 `applyLut`；保留 LUT 仅给 Curves/Levels/PhotoFilter。

### 06-05 hueShift 不做环形回绕 (P1)

- **位置**：`src/media/imageprocessor.cpp:406-420`
- **证据**：

```cpp
const int shift = int(degrees / 2.0) % 180;   // 2° 量化
ch[0] += shift;                               // uchar 饱和，不取模
```

- **现象**：色相滑条向负方向拖时，红色/蓝色区域变成暗色块而不是循环换色（H 越过 0 或 179 被截断）。
- **根因**：HSV 的 H 通道是 0..179 的环形量，`ch[0] += shift` 对 8U 是饱和加法。
- **修复建议**：`lutH[h] = (h + shift + 180) % 180` 后用 `cv::LUT` 只作用 H 通道；角度保留浮点（或把 `/2` 的余数并入）。

### 06-06 extract: grow/shrink/smooth 的 memcpy 忽略 stride (P1)

- **位置**：`src/media/selection/SelectionModel.cpp:311`、`:348`、`:386`
- **证据**：

```cpp
std::memcpy(m_mask.bits(), newMask.data(), static_cast<size_t>(W) * H);
```

- **现象 / 复现路径**：一旦这 3 个 P3.6 接口被接到菜单（当前无调用方，见 06-47），任何宽度不是 4 的倍数的图像
  （例如 1000px 宽 OK，1001px 宽坏）执行选区扩展/收缩/平滑后，蒙版整体呈斜向错位，选区变成一条斜带。
- **根因**：`QImage::Format_Alpha8` 的 `bytesPerLine()` 按 4 字节对齐（`ceil(W/4)*4`），
  而 `newMask` 是 W×H 紧排。按 W*H 一次性拷入后每行错位 `stride-W` 字节。
- **修复建议**：按行 `memcpy(dst + y*stride, src + y*W, W)`（feather 里已经是对的写法，直接照抄）；
  更好的做法是这类形态学操作直接换成 `cv::Mat`/`cv::dilate`/`cv::erode`。

### 06-07 "羽化"被阈值重新二值化，且 O(W·H·r) (P1)

- **位置**：`src/media/selection/SelectionModel.cpp:223-276`（关键 `:269-270`）
- **证据**：

```cpp
const float v = sum / count;
dst[y * stride + x] = (v >= 128.0f) ? 255 : 0;   // 羽化结果又被二值化
```

- **现象**：对选区执行羽化后，边缘没有任何软过渡，只是整体胀/缩 1~2px；半径 50 时 24MP 图界面冻结数秒。
- **根因**：①阈值把 box blur 的中间值全部消掉，等于"模糊后二值化≈形态学"；②H/V 两遍 box blur 每像素
  `2r+1` 次累加，复杂度 O(W·H·r)（注释写"3 passes 近似 Gaussian"，实际只做 2 遍）。
- **修复建议**：羽化结果保留 0..255 软值（全链路配合，见 §5.2）；实现改积分图 O(1) 或
  `cv::GaussianBlur`；半径>20 时异步执行并给进度/取消。

### 06-08 魔棒只用亮度容差 (P1)

- **位置**：`src/media/selection/SelectionStrategy.cpp:185`、`:200`、`:225`
- **证据**：

```cpp
const int seedGray = qGray(image.pixel(sx, sy));
...
const int g = qGray(image.pixel(x, y));
const int dr = g - seedGray;
if (dr * dr > tol2) continue;
```

- **现象**：容差 0 点纯红 (87) 会同时选中 (0,175,0)（qGray 也是 87）等大量无关颜色；纯色块边界选择反而漏选。
- **根因**：PS 魔棒的 "Tolerance" 是 RGB/HSB 逐通道距离，这里把三维颜色压成一维亮度，丢失色相/饱和度信息。
- **修复建议**：一次 `cv::cvtColor(BGR2HSV)` 或直接逐通道 `dr²+dg²+db²` 与 `tol²*(3*255²/255²)` 比较；
  同时补 "Sample All Layers / Anti-alias" 选项。

### 06-09 魔棒逐像素 QImage::pixel()，24MP 几十秒 (P1)

- **位置**：`src/media/selection/SelectionStrategy.cpp:200`、`:225`（BFS 与全图扫描两条路径）
- **证据**：

```cpp
while (!q.empty()) {
    QPoint pt = q.front(); q.pop();
    const int g = qGray(image.pixel(x, y));   // 每像素一次格式转换
```

- **现象**：4000×3000 图点一下魔棒，UI 冻结 10 秒以上且无取消；任务管理器单核 100%。
- **根因**：`QImage::pixel()` 是虚函数 + 每像素颜色转换；`std::queue<QPoint>` 每个元素还要堆分配。
  应改为 `constScanLine` + `QRgb*` 连续扫描，或直接 `cv::floodFill`。
- **修复建议**：先整图转 `std::vector<uchar>` 亮度（或 HSV）再做 BFS，队列用预分配 `std::vector<int>` ring；
  放到后台线程并支持取消。

### 06-10 套索羽化积分图 int 溢出（>8.4MP 必现） (P1)

- **位置**：`src/media/selection/SelectionStrategy.cpp:109-117`；同样的复制在 `mainwindow.cpp:1016-1042`
- **证据**：

```cpp
std::vector<int> sum((W + 1) * (H + 1), 0);   // 累加 255*W*H
...
sum[(y + 1) * (W + 1) + (x + 1)] = sum[y * (W + 1) + (x + 1)] + rowSum;
```

- **现象**：套索选项里把 Feather 调大（>0）后在 12MP~24MP 相机图上松开鼠标，得到雪花状随机选区；小图正常。
- **根因**：积分图最大值 = 255×W×H，W×H > 8.4M 时超过 `INT_MAX`（2.1e9），有符号溢出是 UB，
  后续差分平均得到垃圾值。
- **修复建议**：累加器改 `int64_t`（或 float）；更稳的做法是用 `cv::integral(src, sum, CV_32S)` 时先缩放
  或直接用 `cv::blur`/`boxFilter`。

### 06-11 蚂蚁线只画选区外接矩形 (P1)

- **位置**：`src/media/imagewindow/ImageCanvas.cpp:328-338`（MarchingAnts 消费端）
- **证据**：

```cpp
const QRect bbox = m_sel->boundingRect();
...
p->setPen(penBlack);
p->drawRect(sceneBbox);        // ← 永远是一个矩形
```

- **现象**：用套索画一个圆形/星形选区，虚线仍是一个矩形框；魔棒选出的不规则区域同理。注释里的
  "scan each row, find runs of selected pixels, draw rects" 并未实现。
- **根因**：P0 阶段用 bbox 占位，`MarchingAnts` 本身只提供 200ms 相位，没有路径生成。
- **修复建议**：用 `QRegion(QBitmap::fromImage(mask.createAlphaMask()))` 的 `boundaries()` 或
  `cv::findContours` 生成 `QPainterPath`，缓存路径，phase 只改 `setDashOffset`。

### 06-12 蒙版叠加层每帧重建两张全图 ARGB32 (P1)

- **位置**：`src/media/imagewindow/ImageCanvas.cpp:303-326`
- **证据**：

```cpp
QImage tintImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
QImage maskedTint(mask.size(), QImage::Format_ARGB32_Premultiplied);
for (int y = 0; y < mask.height(); ++y) {
    const uchar* mrow = mask.constScanLine(y);
    QRgb* drow = reinterpret_cast<QRgb*>(maskedTint.scanLine(y));
```

- **现象**：打开"显示蒙版叠加"(P2.1) 后，24MP 图每 200ms（蚂蚁线 tick 触发 `viewport()->update()`）
  重新分配 2×96MB ARGB32 并做 2400 万次循环，界面直接卡死；长时间挂机会 OOM。
- **根因**：在 `paintEvent` 路径里分配全图缓存，且没有"仅 mask 变化时重建"的脏标记。
- **修复建议**：`maskedTint` 作为成员按需重建（接 `SelectionModel::changed`），或直接把 Alpha8 蒙版
  作为 `QImage` 的 alpha 通道画（`QPainter::drawImage` + `CompositionMode_SourceOver` 配蓝色底），
  并把 update 限定到被蚂蚁线经过的 bbox 区域。

### 06-13 手势选区不可撤销 (P1)

- **位置**：`src/media/selection/SelectionCommand.h:9-12`；`tools/Lasso.cpp:62-74`、`MagicWand.cpp:63-75`、`RectSelect.cpp:59-66`
- **证据**：

```cpp
// F-N: RectSelect/Lasso/MagicWand gestures do NOT push this command
...
sel->setMask(mask, mode);   // 直接改模型，无 undo
```

- **现象**：画一个套索选区 → Ctrl+Z，选区的变化不会回退（Ctrl+Z 作用到更早的图像操作上，可能删掉刚画的一笔）。
- **根因**：只有 MainWindow 的三个菜单动作 push 了 `SelectionCommand`；工具路径绕过了撤销栈。
- **修复建议**：工具 release 时 push `SelectionCommand(model, beforeMask, afterMask, mode)`；
  `redo` 必须真正恢复 after（当前 `SelectionCommand::redo` 的 `m_firstRedo` 分支是空转，`m_after` 从未被使用）。

### 06-14 选区命令的整图蒙版内存 ×200 步 (P1)

- **位置**：`src/media/selection/SelectionCommand.cpp:15,37`；`imagewindow.cpp:564`（`setUndoLimit(200)`）
- **证据**：

```cpp
m_before = m_model->mask();       // 24MP = 24MB
...
m_after = m_model->mask();        // 再 24MB（且永远不读）
```

- **现象**：24MP 图反复全选/反相 200 次，进程内存增加约 9.6GB。
- **根因**：每条命令存两份全图（after 是死数据），undoStack 只限条数不限字节。
- **修复建议**：只存 before，`redo()` 重新执行 op（SelectAll/Deselect/Invert 都是可重放的）；
  或按 bbox 裁剪存储；给 undoStack 设内存预算（如 512MB）并在超限时丢弃最旧命令。

### 06-15 ColorRange 每像素一次 cvtColor（24MP 冻结数分钟） (P1)

- **位置**：`src/media/masks/ColorRange.cpp:18-27`（readHsv）、`:91-101`（双重循环）
- **证据**：

```cpp
Hsv readHsv(const cv::Mat& bgr, int x, int y) {
    const cv::Vec3b px = bgr.at<cv::Vec3b>(y, x);
    cv::Mat3b rgb(1, 1);
    rgb(0, 0) = cv::Vec3b(px[2], px[1], px[0]);
    cv::cvtColor(rgb, hsv, cv::COLOR_RGB2HSV);   // 每像素一个 1×1 Mat + 一次 cvtColor
```

- **现象 / 复现路径**：图像 → 颜色范围（`mainwindow.cpp:2777` 对整层调用）在 24MP 图层上执行，
  界面冻结数分钟且无进度、无取消，只能强杀。
- **根因**：2400 万次 `cv::Mat` 构造 + 2400 万次 `cvtColor`（每次还有 SIMD 启动/分支开销），
  复杂度虽是 O(N·S)（S=采样数）但常数极大。
- **修复建议**：`cv::Mat hsv; cv::cvtColor(bgr, hsv, COLOR_BGR2HSV);` 一次搞定，逐像素只做
  `h,s,v` 的加减与 min 比较；再把内层采样循环向量化/查表；放后台线程。

### 06-16 RefineEdge 的 Shift 在二值蒙版上毁掉蒙版 (P1)

- **位置**：`src/media/masks/RefineEdge.cpp:66-78`；调用点 `mainwindow.cpp:2700-2713`
- **证据**：

```cpp
const int bias = qBound(-100, shift, 100) * 2;   // ±200
for (int i = 0; i < N; ++i) {
    const int v = int(src[i]) + bias;
    dst[i] = uchar(std::clamp(v, 0, 255));
}
```

- **现象 / 复现路径**：图层有像素蒙版（0/255）→ 调整边缘 → Shift = +10 → 蒙版所有像素变成 20（>0），
  整层被显示；Shift = -100 → 选中区 55、未选区 0，蒙版变成一块"半透明填充"，轮廓丢失。
- **根因**：PS 的 "Shift Edge" 是沿边缘梯度做距离位移（需要对羽化带或距离场操作），
  这里用常数偏置近似；对二值输入，任何正偏置都让 0 变成"非零=选中"。
- **修复建议**：先 `cv::distanceTransform` 得到到选区边界的距离场，按 shift 像素重阈值化
  （或用 `cv::erode/dilate` 实现 ±N px 位移），并明确 shift 的语义是像素而非 0..100 百分比。

### 06-17 TransformBox 的 handle 不认 Distort 角点与旋转 (P1)

- **位置**：`src/media/transform/TransformBox.cpp:45-89`
- **证据**：

```cpp
QPointF TransformBox::handlePos(Handle h) const
{
    const QRectF r = m_rect;          // ← 永远用 rect
    ...
    case Handle::TopLeft: return QPointF(l, t);
```

- **现象**：Distort 模式拖某个角后，屏幕上手柄/框线不动（渲染也读 handlePos，见 `ImageCanvas.cpp:359-363`），
  用户看不到自己拖到哪里；第二次想抓同一个角，命中区仍在原位置。Rotate 之后框与手柄也不跟着内容转，
  用户无法判断"当前框对应画面的哪个区域"（渲染与命中共用同一函数，所以二者一致、但都相对内容错位）。
- **根因**：几何点有第二真相（`m_corners`、`m_rotationDeg`），但 handle 几何只读 `m_rect`。
- **修复建议**：`handlePos` 按 mode 分支：Distort 用 `m_corners[i]`；非 Distort 用
  `T(center)·R(rot)·p` 变换 rect 上的 8 点；`hitTest` 同步。渲染与命中必须共用同一函数（已共用，只需修它）。

### 06-18 Distort 模式 Center 拖动抹掉 4 角 (P1)

- **位置**：`src/media/transform/TransformBox.cpp:223-229`
- **证据**：

```cpp
case Handle::Center: {
    const QPointF d = newPos - center();
    m_rect.translate(d);
    syncCornersFromRect();   // ← 用 rect 四角覆盖掉用户拖好的 m_corners
    return;
}
```

- **现象**：Distort 拖出梯形 → 点中心拖动整块 → 梯形立刻变回矩形（再松手 commit 出的是矩形变换）。
- **根因**：平移路径复用了"从 rect 同步角点"的辅助函数，而 Distort 中 rect 与 corners 已经解耦。
- **修复建议**：Center 分支只做 `for (i) m_corners[i] += d; m_rect.translate(d);`，不要调用 `syncCornersFromRect()`。

### 06-19 旋转与缩放/斜切不复合 (P1)

- **位置**：`src/media/transform/TransformMath.cpp:28-56`
- **证据**：

```cpp
case TransformBox::Mode::Scale:    return scaleMatrix(box.rect(), handle, newPos, shift);
case TransformBox::Mode::Rotate:   return rotateMatrix(box.rect(), box.rotation());
// 每个 mode 只返回自己的矩阵，rotation 在 Scale/Skew 路径里完全没出现
```

- **现象**：Rotate 转 30° 提交后，像素已经转过，但 `m_rect` 仍是原矩形：下一轮拖角点时按未旋转的框算
  sx/sy，非等比缩放会沿图像轴拉伸已旋转的内容（物体被拉斜）；下一轮 Rotate 又是"绝对角度"再叠一次。
  同一轮拖动也只能是一种变换，PS 里"边转边缩"的连续自由变换无法表达。
- **根因**：变换被建模为 4 个互斥 mode 的单矩阵，而不是"累积变换 + 本次增量"；`commitTransform` 之后
  box 与已烘焙像素不同步。PS 的 free transform 是一个合成矩阵，mode 只决定拖拽如何更新增量。
- **修复建议**：`TransformBox` 存 `QTransform m_accum`（或分解参数 rect+rot+shear+perspective），
  `computeTransform` 返回 `m_accum * deltaMatrix(mode)`；每次 commit 后把 delta 合入 m_accum 并同步 rect。

### 06-20 Shift+Skew 恒为 no-op (P1)

- **位置**：`src/media/transform/TransformMath.cpp:208-221`
- **证据**：

```cpp
case TransformBox::Handle::Top:    return QPointF(newPos.x(), origRect.top());   // y 被固定
...
case TransformBox::Handle::Top: {
    const qreal dy = constrained.y() - rect.top();   // 恒 0
    skewY = dy / std::max(rect.width(), 1.0);        // 恒 0 → identity
```

- **现象**：Skew 模式下按住 Shift 拖动任何边中点，框完全不动（松手也不产生命令）。
- **根因**：约束函数"固定了驱动量本身"：Top/Bottom 的斜切由 dy 驱动，Shift 却把 y 固定；Left/Right 同理。
  且驱动轴的选择本身可疑（水平拖动 Top 手柄在 PS 里才是水平斜切）。
- **修复建议**：Shift 应约束**另一轴**（例如 Top 手柄：x 自由、y 锁到原 top 仅用于水平斜切），
  或直接约束"斜切量按 15° 步进"；驱动量改为沿边缘方向的位移。

### 06-21 TransformCommand 每次双份全图快照 (P1)

- **位置**：`src/media/transform/TransformCommand.cpp:18-19`、`:32-33`；`imagewindow.cpp:564`
- **证据**：

```cpp
, m_beforeMat(beforeMat.clone())
, m_afterMat(afterMat.clone())
```

- **现象**：24MP 图做 100 次自由变换，undo 栈占约 14GB；`TransformTool::commitTransform` 自己还持有
  一份 `before`，瞬时峰值 3 份（~216MB）。
- **根因**：像素级快照命令 + 无内存上限的 undo 栈（`setUndoLimit(200)` 只限条数）。
- **修复建议**：变换是"图层 + 矩阵"的可重放操作，命令只存矩阵与反向矩阵；非要做像素快照时只存 after，
  undo 用 after 逆变换或按脏矩形 tile 存。

### 06-22 TransformBox::toTransform() 是误导性残桩 (P1)

- **位置**：`src/media/transform/TransformBox.cpp:249-266`
- **证据**：

```cpp
QTransform t;
if (m_mode == Mode::Distort) {
    const QPointF c0 = m_corners[0];
    t.translate(c0.x(), c0.y());   // "简化: Distort 4 角平均成 scale + translate"
    return t;
}
t.translate(m_rect.left(), m_rect.top());
return t;                          // 无 scale / rotate / shear
```

- **现象**：任何调用者（目前无）拿到的都是"只平移"的矩阵；头注释却写着"3x3 矩阵计算（P0-6.3 完整实装）"。
- **根因**：P0-6.1 占位实现未删，真实数学在 `TransformMath`。
- **修复建议**：删除该函数（改为 `TransformMath::computeTransform(*this, Handle::None, {}, false)`）或直接内联。

### 06-23 Ctrl+T 初始框恒为 800×600 (P1，跨模块)

- **位置**：`src/media/tools/TransformTool.cpp:36-49`
- **证据**：

```cpp
if (auto* sel = host->selectionModel()) {
    if (!sel && !sel->isEmpty()) {     // sel 非空时第一个条件为假；sel 为空时会解引用空指针
        initRect = sel->boundingRect();
    }
}
if (initRect.isEmpty()) { initRect = QRectF(0, 0, 800, 600); }
```

- **现象**：无论图像多大、有没有选区，Ctrl+T 的框永远在 (0,0)-(800,600)。对 4000×3000 图拖右下角 → 按
  800×600 计算 sx≈1.1，整图被放大和平移；对小图（如 400×300）旋转 → 绕画布外的中心转，结果大面积黑边。
- **根因**：条件写反（`!sel && ...`），且 fallback 用硬编码尺寸而非图像矩形。
- **修复建议**：`if (sel && !sel->isEmpty()) initRect = sel->boundingRect(); else if (host) initRect = QRectF(0,0,img.cols,img.rows);`
  需要 tools 审查员在最终报告里合并（该文件不在我清单内）。

### 06-24 自由变换作用于整幅合成图 (P1)

- **位置**：`src/media/tools/TransformTool.cpp:214-259`；`ImageProcessor::warpAffine` `imageprocessor.cpp:348-354`
- **证据**：

```cpp
if (!host->currentImage().empty()) before = host->currentImage().clone();
...
ImageProcessor::warpAffine(before, after, M, before.size());
```

- **现象**：旋转 30° 一张 4000×3000 图 → 四角被 `BORDER_CONSTANT` 填成黑色透明、其他图层内容一起被 warp；
  对选区做变换时选区外的像素也被改。
- **根因**：把"自由变换"实现成"对扁平化显示图做一次 warp"，而 PS 的语义是"对选中图层/选区施加变换，
  变换结果作为图层内容，画布尺寸不变、其余内容不变"。
- **修复建议**：变换走图层：`Layer::image` + `offset/QTransform`，渲染时再栅格化；选区则先取 mask 内
  ROI，warp 后按 mask 合成回图层。需要与 layers/tools 审查员一起定方案。

### 06-25 13 种 BrushKind 只是标签 (P1)

- **位置**：`src/media/brushes/BrushKind.h:26-43`；`BrushPreset.cpp:364-531`；落笔 `tools/Brush.cpp:242`
- **证据**：

```cpp
// 13 kinds 只被用于 kindName()/kindToString()/kindNeedsGpu()（后者亦无调用方）
brushes::BrushEngine::applyStamp(target, cv::Point(ix, iy), stamp,
                                  m_paintColor, opacity, flow, 0, m_blendMode);
//                                                          ↑ mode 恒 0 = Paint
```

- **现象**：选择"涂抹/橡皮/水彩"画笔，画出来的仍是普通不透明前景色笔触；13 个预设之间只有 size/hardness/spacing
  的差别（Smudge 与 Oil 几乎一样）。
- **根因**：kind 不参与引擎决策；`applyStamp` 的 mode=1(Erase)/2(Smudge) 分支虽然写了，但没有任何调用方传 1/2；
  各 kind 的 dynamics 字段（roundnessJitter、airbrush、scatter）也没有被 `resolve()` 消费。
- **修复建议**：建 `kindSignature(kind) -> {shape, dynamics, mode}` 表；工具层按 kind 传 mode
  （Eraser→1、Smudge→2 并实现取色混合），`resolve()` 消费全部 dynamics。

### 06-26 Multiply/Screen 三通道全用蓝色分量 (P1)

- **位置**：`src/media/brushes/BrushEngine.cpp:249-252`
- **证据**：

```cpp
} else if (blendMode == 2) {  // Screen
    for (int c = 0; c < 3; ++c)
        p[c] = clamp(int(p[c]*(1-w) + (255 - ((255-p[c])*(255-(int)b_color)/255))*w), 0, 255);
```

- **现象**：Multiply 模式下用纯红 (255,0,0) 画笔 → 画出纯黑；Screen 模式红笔 → 画出近白/青色，颜色完全不对。
- **根因**：`r_color/g_color` 已经算出来了，但两个分支的循环都错误地用了 `b_color`（复制粘贴 bug）。
- **修复建议**：`const int col[3] = {b_color, g_color, r_color};` 后用 `col[c]`；顺带把 `useColor`（用 alpha 判断）
  改为显式 `hasColor` 参数。

### 06-27 100% 硬度仍是软边且有效直径偏小 (P1)

- **位置**：`src/media/brushes/BrushEngine.cpp:77-111`
- **证据**：

```cpp
const double GAUSSIAN_BIAS = 1.4 + 0.6 * effective_h;
...
const double intensity = std::pow(1.0 - ratio, GAUSSIAN_BIAS);
```

- **现象**：设 100% 硬度的圆笔刷 100px，画出来是一条中间也不饱和、边缘从中心就开始衰减的软带，
  视觉有效直径约 60px；与 PS "Hard Round 100% 是一只几乎实心的圆" 差异巨大，用户会觉得笔刷"变小、变淡"。
- **根因**：falloff 用 `(1-d/r)^k` 覆盖整个半径，k 越大边缘越陡但整体覆盖越低；PS 的形状是
  "d < r·hardness 时 1.0，之后过渡到 0"。注释里"h=0 → (1-d/r)^0=1"与代码（h=0 时 k=1.4）也矛盾。
- **修复建议**：`if (d <= r*h) v=1; else v = smoothstep(1,0,(d-r*h)/(r-r*h+eps));`（h=hardness/100），
  外加 1~2px AA 过渡；用查表避免 pow。

### 06-28 压感完全没接通 (P1)

- **位置**：`src/media/tools/Brush.cpp:144,152-153,173`；`Brush.cpp:42`（`PressureCurve` 只 new/delete）
- **证据**：

```cpp
paintSingleStamp(host, scenePos, 1.0);          // press
paintSingleStamp(host, p, 1.0);                // move 插值
...
m_pressureCurve(new brushes::PressureCurve())  // 从未 evaluate()
```

- **现象**：Wacom/数位屏上画线，线条粗细与力度无关；`Size Jitter / Transfer / Shape Dynamics` 面板全无效。
- **根因**：ToolState 只处理 `QMouseEvent`，没有 `QTabletEvent` 分支；pressure 参数被写死 1.0；
  `BrushSettings`/`resolve` 也没有"pressure→size/opacity"的开关。
- **修复建议**：`ToolState::tabletEvent` 读 `pressure()`→`PressureCurve::evaluate`→传入 Brush；
  `resolve()` 增加 sizeControl/opacityControl 判断（默认 Off 时不缩放 size，见 06-27 的 `s.size * p` 无条件缩放）。

### 06-29 无笔触累积缓冲，Opacity≈Flow 混同 (P1)

- **位置**：`src/media/brushes/BrushEngine.cpp:210`、`:225`
- **证据**：

```cpp
const double alpha = std::clamp(opacity,0,1) * std::clamp(flow,0,1);
...
const double w = (sval / 255.0) * alpha;
p[c] = int(p[c]*(1-w) + color*w);     // 每个 stamp 独立前混
```

- **现象**：Opacity=20%、spacing=25% 的笔刷，一笔之内因 stamp 重叠迅速变成 100% 不透明；同一笔内无法实现
  PS 的"半透明一笔"。降低 Flow 只是让每个 stamp 更淡，但依然会叠满。
- **根因**：缺少"每笔一张 alpha 累积层，按 max(累积, stamp) 或 source-over 累积，笔末以 opacity 一次性合成"的结构；
  flow 和 opacity 在数学上也完全等价（都只是乘 alpha）。
- **修复建议**：引入 per-stroke 缓冲：`strokeAlpha[i] = min(1, strokeAlpha[i] + flow*stamp)`，落笔时
  `result = color*(opacity*strokeAlpha)`；Airbrush 模式下再按时间重复注入 flow。

### 06-30 每个 stamp 重新生成 falloff/椭圆/噪声 (P1)

- **位置**：`src/media/brushes/BrushEngine.cpp:25-75`、`:77-183`
- **证据**：

```cpp
cv::Mat falloff = generateFalloff(diameter, s.hardness, shape.type);     // O(d²) 含 pow
cv::Mat elliptical = generateElliptical(diameter, s.roundness, s.angle, falloff);
applyShapeNoise(elliptical, shape.type);
```

- **现象**：300px 以上笔刷拖动掉帧到个位数；笔刷越大越卡（每帧可能 10~50 个 stamp × 90k~4M 像素 × pow/sqrt）。
- **根因**：stamp 完全无缓存，且 `std::pow`/`std::sqrt` 在最内层循环；`tools/Brush.cpp` 还会为每个 stamp
  重建一次 `BrushSettings` 副本。
- **修复建议**：`QCache<StampKey, cv::Mat>`（key = size/hardness/roundness/angle/type/quantized pressure），
  falloff 用预计算的 1D LUT（`intensity[dist]`），椭圆用最近邻采样即可；另见 tools 侧 `renderToView` 每 stamp 全图刷新（06-63）。

### 06-31 ABR 导入不可用（解析器无调用方 + 拒绝 v9/v10） (P1)

- **位置**：`src/media/brushes/AbrParser.cpp:136-217`、`:171-174`、`:200-202`；`BrushPickerDialog.cpp:126-135`
- **证据**：

```cpp
if (version == 9) {
    res.errorMessage = QStringLiteral("ABRv9 parsing not implemented (P1.1 follow-up); use ABRv6 files");
    return res;
}
...
QMessageBox::information(this, tr("Load ABR - P1.1 follow-up"),
                         tr("ABR parser will be implemented in P1.1 follow-up.\nSelected: %1").arg(path));
```

- **现象**：用户从 PS 下载的 .abr（现在基本都是 v9/v10，CC 2018+）无法导入；连 v6 也无法导入，因为
  Picker 根本没调用 `AbrParser`；采样笔刷（'t'）被直接跳过。
- **根因**：解析器写完未接线（全仓库 0 处 `AbrParser::parseFile` 调用）；v9/v10 的描述符格式未实现。
- **修复建议**：Picker → parseFile → 逐个 `BrushPresetManager::addUserPreset` + `generateThumbnail`；
  补 v9/v10（u32 大端 tag + 变长 length + 描述符 key/value）与 't' 采样笔刷 PNG 解码。

### 06-32 预设管理器/浏览器/缩略图全部无调用方 (P1)

- **位置**：`src/media/brushes/BrushPreset.cpp:537-668`；`BrushPickerDialog.cpp:75-100`；`BrushEngine.cpp:405-430`
- **证据**：

```cpp
void BrushPresetManager::seedBuiltIns() {
    // 5 built-ins are returned by builtIns(); they are not stored in m_userPresets.
    // (No action needed, factory methods are static.)   ← 空函数 + 过期注释
}
...
m_model = new QStringListModel(this);   // 只有文字，无缩略图
```

- **现象**：UI 中没有"画笔预设"面板；即使打开 Picker 也没有 PS 缩略图，只有 "名字 (px)" 列表；用户自建预设
  无处保存/读取（`saveUserPresets` 无人调用）。
- **根因**：P1.1 只做了类，没接 UI。`generateThumbnail` 也因此成为死代码。
- **修复建议**：画笔选项栏接预设面板（QListWidget IconMode + `generateThumbnail` 缓存），
  启动时 `loadUserPresets`，增删改后 `saveUserPresets`。

### 06-33 文字旋转绕错圆心导致公转漂移 (P1)

- **位置**：`src/media/graphicstextitem.cpp:58-76`、`:253-264`
- **证据**：

```cpp
// applyTransform: m_transform = T(c)·R(rot)·S·T(-c), setPos(m_pos)
const QPointF v0 = m_dragStartScenePos - m_dragStartItemPos;   // 以 m_pos 为圆心
const QPointF v1 = scenePos - m_dragStartItemPos;
```

- **现象**：旋转文字时文字不仅转，还会绕着"文字左上角左上方"某点摆动；转 90° 后位置偏出半个字宽/字高
  （旋转中心实际是 `m_pos + localCenter`，代码按 `m_pos` 算角）。
- **根因**：`m_pos` 被注释为"scene 空间中心"，但 `setPos(m_pos)` 设置的是 item origin；
  经 `T(c)R S T(-c)` 后本地中心 c 映射到 `m_pos + c`。角度按 `m_pos` 计算 → 圆心偏了 c。
- **修复建议**：`const QPointF centerScene = mapToScene(localRect().center());` 用它算角度；
  或者把 `m_pos` 语义改为视觉中心：`setPos(m_pos - c)`（并同步 resize 锚点公式）。

### 06-34 texteditorframe.cpp 是孤儿文件 (P1)

- **位置**：`src/media/texteditorframe.cpp`（260 行）；`src/media/CMakeLists.txt`
- **证据**：

```cpp
// 注: TextEditorFrame 类直接定义在 imagewindow.h, 本文件只提供实现
#include "imagewindow.h"   // TextEditorFrame 类定义在这里
```

- **现象**：`imagewindow.h` 里没有 `TextEditorFrame` 类声明（全仓库仅本文件出现该标识符），`media/CMakeLists.txt`
  未列出该文件 → 从未编译。若有人把它加进 CMake，会立即编译失败；即便补上声明，内部还有坐标混用 bug
  （`:210-216`：`m_startCenter = m_startRect.center()` 是 item-local 坐标，却拿去和 `e->scenePos()` 相减，
  只要 frame 的 `pos()` 不是 (0,0)，旋转手柄就会乱飞）。
- **根因**：某轮重构把类挪走/删除，留下实现文件；注释未更新。
- **修复建议**：删除该文件；把其中正确做法（`ItemIgnoresTransformations` 的手柄子 item）移植回
  `GraphicsTextItem::paint`（正好修 06-64）。

### 06-35 "颜色范围"用 3 个硬编码采样点 (P1)

- **位置**：`src/app/mainwindow.cpp:2753-2778`
- **证据**：

```cpp
QVector<QPoint> samples;
samples.append(QPoint(w / 4,     h / 4));
samples.append(QPoint(w / 2,     h / 2));
samples.append(QPoint(3 * w / 4, 3 * h / 4));
```

- **现象**：颜色范围永远按图像 1/4、1/2、3/4 处的三个点取色，用户无法点选目标颜色 → 结果基本不可用。
- **根因**：P1.3.7 的简化入口（`QInputDialog`）没有做吸管/预览 UI；`ColorRangeSelectionStrategy`
  的多点采样路径同样没有 UI 接入口。
- **修复建议**：做非模态对话框：吸管在画布取样（可多选）+ Fuzziness/Invert + 实时预览（复用
  `ColorRangeSelectionStrategy`），取消则回滚。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 选区表示 | 8-bit 灰度蒙版，可抗锯齿、可存为 Alpha 通道 | Format_Alpha8，主体二进制；羽化/变换会产生半透明值但下游多数按 >0 判断 | 部分 | P1 |
| 选区布尔模式 | 新/加/减/交 + Shift/Alt/Ctrl | 4 模式齐全（`SelectionModel::setMask`），Shift/Alt/Shift+Alt 映射正确 | 无 | — |
| 选区羽化/扩展/收缩/平滑 | Select>Modify 5 项 + 实时预览 | API 有（P3.6），UI 无入口；MainWindow 另有一份独立羽化 | 部分 | P1 |
| 选区反相/全选/取消 | 全部可撤销 | 仅这 3 个可撤销，手势选区不可撤销 | 有但错 | P1 |
| 蚂蚁线 | 沿真实选区轮廓流动 | 只画选区外接矩形 | 有但错 | P1 |
| 保存/载入选区 | 保留完整蒙版（可命名、可存通道） | 只保存 bbox 矩形（imagewindow.cpp:2563-2570） | 有但错 | P1 |
| 套索多边形/磁性套索/多边形套索 | 3 种 | 只有自由套索（点流），无磁性/多边形 | 部分 | P2 |
| 魔棒（容差/连续/取样全部图层/消除锯齿） | RGB/HSB 容差、AA、All Layers | 只按亮度容差，无 AA、无 All Layers | 部分 | P1 |
| 颜色范围 | 吸管取样 + 预览 + Localized Clusters + 可调范围 | HSV 距离实现可用但极慢；UI 用 3 个硬编码点 | 部分 | P1 |
| 调整边缘 | Radius/Edge Detect/Decontaminate/Output To、智能半径 | 4 个一维算子（smooth/feather/contrast/shift），Shift 会毁蒙版 | 有但错 | P1 |
| 自由变换 | Scale/Rotate/Skew/Distort/Perspective/Warp，实时预览，作用于图层/选区 | 4 mode 但矩阵不复合、Distort 手柄不更新、无预览、作用整图、矩阵转置 | 有但错 | P0 |
| 变换撤销 | 精确恢复 | 整图双快照（内存爆炸），box 与图像部分同步 | 有但错 | P1 |
| 画笔种类 | 20+ 种预设 × 形状/动态/纹理/双重画笔/传递/湿边/喷枪 | 13 个"kind"标签 + 参数微调，引擎无 kind 分支 | 部分 | P1 |
| 画笔压感/倾斜/笔轮 | Size/Angle/Roundness/Transfer 全支持 | 压感恒 1，其余 control 全未实现 | 缺失 | P1 |
| 画笔混合模式 | 27 种 | 3 种（Normal/Multiply/Screen），且两种实现有 bug | 部分 | P2 |
| 笔触透明度/流量语义 | Opacity 封顶 + Flow 累积 + 无笔触累积缓冲 | 每个 stamp 直接前混，无累积缓冲 | 有但错 | P1 |
| 画笔预设/ABR 导入 | 面板 + PSD/ABR 生态 | 管理器/对话框/解析器全部无调用方；ABR v9/v10 不支持 | 缺失 | P1 |
| 文字图元 8+1 手柄 | 自由变换、字体度量、路径文字、变形文字 | 8+1 手柄可用但随缩放变形、旋转绕错点、不可撤销缩放、栅格化忽略变换 | 部分 | P1 |
| 文字编辑框（texteditorframe） | 原位编辑 + 框选缩放 | 文件是孤儿（不编译） | 缺失 | P2 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| cv::Mat↔QImage 共享零拷贝 | QT-NATIVE | `QImage(uchar*,w,h,bytesPerLine,format)` + `cv::Mat(...)` 包装即可零拷贝；本项目四处各写一套且 4 通道选错格式 | 收敛到一处 `imagebridge.h`：按 `(depth,channels)` 映射 Format，并用 `QImage::cleanup` 托管 Mat 生命周期 |
| 蚂蚁线沿轮廓 | QT-NATIVE | `QPainterPath` 支持任意路径；轮廓可由 `QRegion(maskBitmap).boundaries()` 或 cv::findContours 生成 | QRegion + `QPainterPath::addRegion`，phase 只改 dashOffset |
| 蒙版叠加层 | QT-NATIVE | QPainter `CompositionMode_SourceAtop` + 预乘 tint 已够用，问题是每帧重建 | 缓存 `QImage` tint，mask 变化时重建；用 dirtyRect 局部 update |
| 选区抗锯齿/8-bit 蒙版 | QT-EFFORT | QImage 本身就支持 8bpp 灰度；真正工作量在于全链路（合成、显示、命令）都接受软值 | 1~2 人天：定义 `Mask` 类型（CV_8UC1），渲染/操作统一按软值语义 |
| 压感/倾斜 | QT-NATIVE | `QTabletEvent` 提供 pressure/xTilt/yTilt/rotation/tangentialPressure | 事件层直传，无需第三方 |
| 平滑笔触/稳定器 | QT-EFFORT | 现有 StrokeSmoother 是滑动均值，可用；PS 级平滑需重采样+去抖（1~3 人天） | 保持自研，改用 Catmull-Rom 插值 + 速度自适应间距 |
| 画笔 stamp 生成加速 | QT-3RD | 单线程逐像素 pow/sqrt 是瓶颈。可引入 OpenCV（已有）做矢量化的距离场/高斯，或用 Skia 的 shader | 优先 OpenCV：`cv::distanceTransform` + LUT；根治需 GPU（Qt RHI/QOpenGL） |
| GPU 画笔/合成 | QT-3RD | Qt 6 的 QRhi 可做，但需要自研 shader；轻量方案是 OpenGL + FBO 笔触累积缓冲 | 2~4 周引入 QRhi 后端（`BrushKind::kindNeedsGpu` 已预留接口）；或先做 CPU 累积缓冲 |
| 透视变换/Distort | QT-NATIVE | `QTransform::quadToQuad` 可算 3×3；但落像素必须 OpenCV `warpPerspective`（QTransform 只有 2×3 语义） | 保持现状，注意 commit 时区分 affine/perspective 两条路径（当前已区分，但矩阵转换错） |
| ABR/v9/v10 解析 | QT-EFFORT | 纯二进制解析，Qt 无能力，需自研（QDataStream 可当工具） | 2~3 人天补 v9/v10 描述符；或用第三方 `libabr`（GPL，许可需评估） |
| 文字图层渲染与图元统一 | QT-EFFORT | QGraphicsTextItem 的布局/变换可复用，但需要一份"文字图层 = 图元状态"的单一数据源 | 1~2 人天：Layer::Text 存 {text,font,color,pos,rot,scale}，渲染与图元都读它 |
| 直方图/统计面板 | QT-NATIVE | QImage/QPainter 画直方图足够；算法已在 ImageProcessor | 0.5 人天接面板 |

## 5. 设计问题

1. **转换层无单一真相**：`ImageProcessor::matToQImage`（4ch 错）、`ImageWindow::currentImageAsQImage`（4ch 对）、
   `ImageWindow::layerStackAsQImage`（对但丢 alpha）、`masks::ColorRange::qImageToBgr`、`SelectionStrategy` 内联
   `memcpy`、`SelectionModel::setMask` 的 memcpy —— 六套 cv↔QImage/蒙版搬运代码，行为各不相同。任何一处的 stride/format
   修正都要改六处。→ 建议抽 `ImageBridge`（toQImage/toMat/maskToQImage），并写参数化测试（1/3/4 通道 × 宽度 %4）。
2. **二进制蒙版 + 软边路径并存**：`LassoSelectionStrategy` 羽化后产生 1..254 的软值，但 `SelectionModel::grow/shrink/smooth/feather`
   全部按 128 阈值二值化、`invert` 直接归零、`contains/pixelCount` 按 !=0 统计、`ImageEditCommand` 之外的像素操作只认 bbox。
   同一份数据被三种语义消费，用户看到的和最终生效的不一致。
3. **"算法 API 已实现"与"产品里能用"之间有大片真空**：直方图/统计/pixelAt/toGray/toBinary/drawText、SelectionModel 的 5 个
   P3.6 接口、BrushPresetManager/BrushPickerDialog/AbrParser/generateThumbnail、TransformBox::toTransform —— 合计约 1,500 行
   无调用方代码。它们多数带注释宣称"已完整实装"，对后续维护是误导。
4. **撤销命令一律存整图像素快照**：SelectionCommand（2 张 Alpha8）、TransformCommand（2 张全图）、ImageEditCommand（2 张全图，
   Brush/马赛克/克隆共用），而 `QUndoStack::setUndoLimit(200)` 只限条数不限字节。24MP 图下每个像素类操作 ≈144~216MB，
   长会话必然 OOM。→ 必须按"操作-重放"或"脏矩形 tile 快照"设计，并给 undoStack 加内存上限。
5. **选区只被当作"矩形提示"用**：Save/Load Selection、截图、Transform 初始框、像素操作全部只读 `boundingRect()`，
   真实蒙版只用于显示。这使套索/魔棒在功能上退化为"圈一个框"。根因是 SelectionModel 未提供 mask 序列化/裁剪 API，
   调用方图省事拿 bbox。
6. **坐标基准混乱**：`TransformBox` 头注释说 scene 坐标，`TransformTool::commitTransform` 直接拿去当 image 坐标给
   `cv::warpAffine`（当前因为 pixmap 在 scene 原点 1:1 才没暴露）；`TextEditorFrame` 把 item-local center 与 `e->scenePos()`
   混算；`GraphicsTextItem` 的 `m_pos` 名义上是"中心"实际是 item origin（差 localCenter）。一旦引入画布偏移/缩放，全线漂移。
7. **Qt 所有权/生命周期混用**：`TransformCommand`/`SelectionCommand` 用 `QPointer`（正确），`TextItemCommand` 同时存
   `QPointer` + 裸 `GraphicsTextItem*` 并在 undo 里 `delete`（correctness 靠手工顺序）；`ImageCanvas` 用裸 `m_box`
   weak 指针指向 `ImageWindow` 持有的 box，二者生命周期无约束。
8. **线程模型**：`applyLutAsync`/`renderAsync` 都是"提交到后台池 + 回调切主线程"，但没有取消语义（只能靠 taskId 丢弃结果），
   且 future 版在任务被池丢弃时 promise 永不满足 → `future.get()` 阻塞 UI 线程（[待确认] 池 shutdown 行为）。
9. **注释/文档可信度**：本模块大量注释与实现相反（"3 passes"实际 2、"1:1 实装"实际 4 个退化、"完整实装"实际 P0-6.1 简化残留），
   甚至 `imagewindow.cpp:455` 一类"class layout shift 导致 QString d-pointer 被踩"的不可信根因。审计/维护成本被显著抬高。
10. **可测试性**：所有关键路径（画笔落笔、选区布尔、变换矩阵、LUT）都是静态函数/内联 UI 事件，没有可注入的
    `ImageOps`/`Mask`/`Transform` 接口；现有 tests 都只能测纯函数，UI 路径无覆盖。

## 6. 修复建议（按优先级）

### P0 立即修（1~2 天）
- 修正 `qTransformToAffine` 的转置（06-01），加单测：rotate(30°)/shear 后 `QTransform::map` 与 `cv::transform` 结果逐点一致（0.5h）。
- `matToQImage` 4 通道改 `Format_RGBA8888`，1 通道改 `Format_Grayscale8`（06-02），并把三处实现收敛成一处（0.5d）。
- 修 `TransformTool::onEnter` 的 `if (!sel && ...)`，初始框改用图像 rect/选区 bbox（06-23）（0.5h，与 tools 审查员协同）。

### P1 本轮修（3~5 天）
- 选区层：`grow/shrink/smooth` 按 stride 写（06-06）；羽化保留软值 + 可分离实现（06-07）；Lasso 积分图改 int64（06-10）；
  魔棒改 RGB 容差 + scanLine 读取（06-08/09）；手势选区推 undo 命令（06-13）。
- 蚂蚁线改成真实轮廓 + 缓存 tint 层（06-11/12）。
- 蒙版层：ColorRange 整图一次 cvtColor（06-15）；RefineEdge 的 Shift 改成真正的边缘位移（06-16）；Smooth 改保边滤波（06-48）。
- 变换层：handlePos/hitTest 感知 mode 与 rotation（06-17）；Distort Center 不覆盖 corners（06-18）；矩阵复合（06-19）；
  Shift+Skew（06-20）；命令只存必要快照 + 内存上限（06-21）。
- 画笔层：`applyStamp` 三通道颜色（06-26）；硬度曲线改 PS 形状（06-27）；接压感（06-28）；per-stroke 累积缓冲（06-29）；
  stamp 缓存（06-30）；13 kind → 行为映射（06-25）。
- 文字层：旋转圆心与 m_pos 语义（06-33）；撤销补全缩放/位置（06-65）。

### P2 后续（1~2 周）
- `invert`/`hueShift`/`saturation` 的 4 通道语义（06-03/05/39）；`sharpen` 改 USM（06-36）；`computeStats`/`pixelAt` 修正（06-37/38）；
  LUT 的 HSL/B&W/ChannelMixer 真实现（06-04）——这是"调整面板有没有用"的分水岭。
- 撤销命令重构为"操作重放/脏矩形快照"（06-14/21/63），加 `QUndoStack` 内存上限。
- 选区表示与全链路 8-bit 软蒙版统一（§5.2），Save/Load 存完整蒙版（06-13 关联）。
- ABR 接线 + v9/v10 解析 + 预设面板（06-31/32/58/61/62）。

### 架构级重构（2~4 周）
- 抽出 `ImageBridge`（cv↔QImage/蒙版）+ `Mask` 类型 + `ImageOps` 接口；所有像素路径（渲染、工具、命令）走同一桥（§5.1）。
- 撤销系统统一成"文档操作日志"：图像类操作存 tile 脏区，选区/蒙版存 RLE 或 bbox 裁剪，变换存矩阵+图层 offset（§5.4）。
- `BrushPreset` 全字段（真正消费 dynamics/texture/dual）+ GPU 分派（`kindNeedsGpu` 已有接口，见 §4）。

## 7. 需要 Lead 协调的跨模块问题

1. **06-23 / 06-24 / 06-54（transform 集成）**：`tools/TransformTool.cpp` 的初始框 bug 与"整图变换"语义需要与 tools 审查员合并结论；
   修复需同时改 `ImageWindow::applyTransformImage`（是否支持图层/选区变换）。
2. **06-11 / 06-12（蚂蚁线/蒙版叠加）**：实现落在 `imagewindow/ImageCanvas.cpp`（渲染模块），我这边只出结论与方案；
   需要 render 审查员确认 `ImageCanvas` 的 dirtyRect 机制能否承载局部重绘。
3. **06-14 / 06-21 / 06-63（撤销内存）**：需要 layers 审查员一起定"命令存什么"的规范（`LayerCommand`/`ImageEditCommand`/`TransformCommand`/`SelectionCommand`
   目前四种风格）；建议由 Lead 出一份 UndoCommand 设计约束。
4. **06-41 与 layers 审查员的 LYR-15（蒙版全部无效）冲突**：`RefineEdge/ColorRange` 改的是 `LayerMask::pixel`，而 layers 报告指出
   `render()` 只消费旧字段 `layerMask/maskEnabled` —— 即用户改完蒙版**看不到效果**。这是两个模块共同导致的，需一起修。
5. **06-63 画笔预览链路**：`tools/Brush.cpp` 每次落 stamp 调 `renderToViewPublic()`（整图 matToQImage + QPixmap），需要 tools 审查员
   确认是否能改成局部 update；同时 06-02 的转换修复会直接影响此路径。
6. **06-66 文字栅格化**：`LayerStack::rasterize`（layers 模块）完全忽略 `GraphicsTextItem` 的几何/旋转，需与 layers 审查员统一
   "文字图层 = 图元状态"的数据源。
7. **06-40（TIFF/PNG 压缩参数）**：[待确认] Qt 6.11.1 各 image plugin 的 `setCompression` 语义（尤其 TIFF 的 LZW/Deflate 枚举），
   需要 Lead 决定是否引入 libtiff/libpng 直连（QT-3RD）。
8. **孤儿文件 `texteditorframe.cpp`（06-34）**：`src/media/CMakeLists.txt` 未收录，若产品侧仍期望"文字编辑框"功能，需要
   owner 决定删除还是补 class + 接 CMake；建议删除并把该文件里正确的做法（`ItemIgnoresTransformations` 手柄）移植回
   `GraphicsTextItem`（正好修 06-64）。
9. **`ImageEditCommand::redo` 对图像快照型命令是 no-op**（`imagewindow.cpp:257-273` 注释自认）：这意味着画笔/马赛克/克隆
   undo 后无法 redo，属于跨全部像素工具的 P1，需要 Lead 统一排期（我这边只在 06-63 关联提到）。
