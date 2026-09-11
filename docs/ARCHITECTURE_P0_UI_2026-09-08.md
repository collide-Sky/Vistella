# P0 图片模块 UI 架构重写方案 v2

> 起草: 2026-09-08 (下午, 第二版)
> 状态: **user 拍板锁定 PS 完整布局, 阶段 B 暂停, 重写方案 + 重新设计阶段划分**
> 上下文: 阶段 1 W4 后, P0-1~P0-3.3 已落地; 整体布局按 PS 完整版重写, 引入设计模式 + 跨模块数据流

---

## 0. 用户拍板决策 (2026-09-08 下午, 锁定 v2)

| 决策 | 选择 | 备注 |
|------|------|------|
| 阶段 B 暂停 | **是** | 先做 UI 完整重写, AdjustDialogBase 推迟 |
| 整体布局 | **PS 完整版** | 紧凑合理, 主流商业软件标杆 |
| 6 主菜单 (标题栏中间) | **加: 文件/编辑/图像/图层/文字/选择/滤镜/视图** | 8 个 (含文件/编辑), user 2026-09-08 拍板; 具体功能后续阶段填 |
| 8 基础工具 (PS 同款) | **加: 移动/矩形选框/套索/魔棒/裁剪/吸管/文字/画笔** | user 2026-09-08 拍板; F-L 阶段补 |
| 4 workspace 预设 | **PS 同款: 基本功能/图形和 Web/绘画/摄影** | F-F 阶段实 |
| 二级工具栏 (40px) | **QStackedWidget, 每工具一 page** | 点击工具按钮切到对应 page |
| 二级工具栏左侧 | **主页按钮 + 不同功能的 stack 窗口** | user 2026-09-08 拍板; 主页按钮替代 tab 区的 home 按钮 |
| 二级工具栏右侧 | **只保 "工作区属性" 按钮** | PS 同款 workspace 切换 |
| 左侧工具栏 | **默认两列图标 (未展开), 可展开为单列** | PS 同款, 节省空间 |
| 文档 tab 区域 (主窗口 tabWidget) | **去掉 home 按钮 + 去掉 pin 按钮, 只保留关闭按钮** | user 2026-09-08 拍板; 主页入口在二级工具栏, pin 简化 |
| 标题栏左侧 | **程序图标 + 程序名称** (不变) | - |
| 标题栏右侧 | **最大化/最小化 + 登录 + 切换主题** (不变) | - |
| 标题栏中间 | **6 大系统主菜单** (新加) | 见上 |
| 右侧 3 dock 上下排 | **是, 每个 dock 内多个 tab** | workspace 切换 dock 内容 |
| Mediator 拆分 | **拆 3 个: ToolMediator / WorkspaceMediator / DialogMediator** | user 2026-09-08 拍板; 一个类只负责一个方向, 避免上帝类 |
| 强约束 | **任何修改都不能影响主窗口 maximize/normalize 状态机** | user 多次强调 |
| 设计原则 | **低耦合 + 高性能 + 多种设计模式结合** | 商业软件标准做法 |

---

## 1. 终极布局 (PS 完整版, user 拍板)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ┌─[标题栏 32px, 主窗口状态机管, 不能动]───────────────────────────────┐  │
│ │ 文件(F) 编辑(E) 图像(I) 图层(L) 文字(M) 选择(S) 滤镜(T) 视图(V)   │  │ ← 6 主菜单
│ │                                            增效工具 窗口(W) 帮助(H) │  │
│ └──────────────────────────────────────────────────────────────────┘  │
│ ┌─[文档 tab]──────────────────────────────────────────────────────┐  │
│ │ 主页 │ 微信图片_2026-05-28.. │  彩色..png                            │  │
│ └──────────────────────────────────────────────────────────────────┘  │
│ ┌─[二级工具栏 40px, QStackedWidget]────────────────────────────────┐  │
│ │ [工具 1 page] 取样大小 样式 宽度 ...       [🔔] [☀] [🗂] [🏠]    │  │ ← 左侧工具
│ │ [工具 2 page] 模式 强度 流量 ...                                 │  │   不同 page 切
│ │ [工具 3 page] 抗锯齿 容差 ...        [工作区 ▼]                  │  │   只保工作区按钮
│ └──────────────────────────────────────────────────────────────────┘  │
│ ┌──┬─────────────────────────────────────────┬─────────────────────┐  │
│ │  │                                          │ ╭─── Dock 1 ───╮ │  │
│ │✎ │                                          │ │ 颜色  色板  渐变│ │  │
│ │✂ │                                          │ │ 图案  (4 tabs) │ │  │
│ │  │                                          │ ├─────────────────┤ │  │
│ │  │                                          │ │  (拖拽 dock 边缘│ │  │
│ │↑ │           中央画布 (纯 QGraphicsView)     │ │   调整上下高度) │ │  │
│ │  │                                          │ ├─────────────────┤ │  │
│ │左│                                          │ │ ╭─── Dock 2 ───╮│ │  │
│ │  │                                          │ │ │ 属性  调整    ││ │  │
│ │侧│                                          │ │ │ (2 tabs)     ││ │  │
│ │  │                                          │ │ ╰──────────────╯│ │  │
│ │工│                                          │ ├─────────────────┤ │  │
│ │具│                                          │ │ ╭─── Dock 3 ───╮│ │  │
│ │  │                                          │ │ │ 图层  通道  路径││ │  │
│ │↓ │                                          │ │ │ (3 tabs)     ││ │  │
│ │  │                                          │ │ ╰──────────────╯│ │  │
│ └──┴──────────────────────────────────────────┴─────────────────────┘  │
│ ┌─[状态栏 24px]────────────────────────────────────────────────────┐  │
│ │ 100%  1920×1080  RGB/8  文档: 微信图片..                          │  │
│ └──────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

### 1.1 关键变化 vs 当前 (2026-09-08 user 给图)

| 区域 | 当前 | 重写后 |
|------|------|--------|
| 标题栏 (中间) | 文件/编辑/视图/工具/帮助 | **8 主菜单: 文件/编辑/图像/图层/文字/选择/滤镜/视图** + 增效工具/窗口/帮助 (P0-3.3 user 拍板) |
| 标题栏 (左侧/右侧) | 不动 | 程序图标 + 名称 / 窗控 + 登录 + 主题 (user 拍板: 不变) |
| 二级工具栏 | 不存在 | **40px 高, 左侧 主页按钮 + stack, 右侧 工作区按钮 (user 拍板)** |
| 文档 tab 区域 | home 按钮 + pin 按钮 + 关闭按钮 | **去掉 home 按钮 (去二级工具栏) + 去掉 pin 按钮, 只保留关闭按钮 (user 拍板)** |
| 左侧工具栏 | 不存在 | **两列图标 (默认) / 单列图标 (展开) — PS 同款** |
| 右侧 | Adjustment dock + info dock | **3 dock 上下排: 颜色/属性/图层, 每 dock 多 tab** |
| 中央画布 | ImageCanvas + 各种 panel | **纯 QGraphicsView, 不放任何 panel** |
| 顶部菜单触发 | 散在工具栏按钮 | **主菜单 → 子菜单 → 弹非模态 dialog (PS 同款)** |

### 1.2 工作区 (Workspace) 概念 (PS 同款, 图 2)

> "基本功能 / 图形和 Web / 绘画 / 摄影 / 复位基本功能 / 新建工作区 / 删除工作区"

- 工作区 = **3 dock 的内容预设**
- 例: "基本功能" 工作区 = 颜色/属性/图层 3 dock 全开
- 例: "绘画" 工作区 = 颜色 + 画笔设置 dock (没有图层 dock)
- 例: "摄影" 工作区 = 直方图 + 颜色 + 属性 (没有图层 dock)
- 工作区切换由二级工具栏右侧 "工作区 ▼" 按钮触发
- 持久化到 QSettings (跟 PS 一样)

### 1.3 跨模块数据联动关键点

**这是 user 重点强调的"低耦合" + "高性能"**:
- **低耦合**: 各工具/各 dock/各 dialog 之间不直接互相调用, 通过 **Mediator** 转发
- **高性能**: 工具切换响应时间 < 50ms; 非模态 dialog 实时刷新不卡 UI; 大图操作走 EngineContext (P0-2 已落地)

---

## 2. 设计模式选择 (按模块, user 要求)

商业软件 (PS/AE/Sketch) 都是**多种设计模式结合**, 不只用一种。我们也按模块选最合适的。

### 2.1 工具切换 / 状态机 → **State 模式**

**问题**: 左侧工具栏点击切工具时, 画布 cursor / eventFilter / 工具栏 page 都要同步变, 散在多处会乱

**State 模式应用**:
```cpp
class ToolState {
public:
    virtual void onEnter(ImageWindow* host) = 0;     // 工具激活: 装 eventFilter / 切 page / 改 cursor
    virtual void onExit(ImageWindow* host)  = 0;     // 工具退出: 卸 eventFilter
    virtual void onMouseEvent(QMouseEvent* e) = 0;   // 工具行为 (moveTool / selectTool / ...)
    virtual QCursor cursor() const = 0;
};

class ToolContext {
    ToolState* m_current = nullptr;
    void setState(ToolState* s) {
        if (m_current) m_current->onExit(m_host);
        m_current = s;
        if (m_current) m_current->onEnter(m_host);
    }
};
```

**状态实例** (每工具一个): `MoveTool / RectSelectTool / LassoTool / MagicWandTool / CropTool / TextTool / BrushTool / EyedropperTool` (8 基础)

### 2.2 工具切换 / 配置加载 → **Strategy 模式**

**问题**: 同样点 "矩形选框", 用户配的 "羽化半径" 不同, 行为不同

**Strategy 模式应用**:
```cpp
class SelectionStrategy {
public:
    virtual QRect computeBounds(const QPointF& a, const QPointF& b) = 0;
    virtual cv::Mat applyMask(const cv::Mat& img) = 0;
};
// 实例: RectSelection / LassoSelection / MagicWandSelection / ColorRangeSelection
```

**好处**: 切换 Strategy 不用改 ToolState 代码

### 2.3 工具 ↔ 二级工具栏 / 画布 ↔ Dock / 菜单 ↔ Dialog → **Mediator 模式 (拆分 3 个, user 2026-09-08 拍板)**

**问题**: user 强调"低耦合", **user 2026-09-08 拍板: 拆 3 个 Mediator, 一个类只负责一个方向, 避免上帝类**

**Mediator 拆分** (避免 god object):
```cpp
// 1) ToolMediator - 工具切换 + 工具配置变更
class ToolMediator : public QObject {
    Q_OBJECT
public:
    void switchTool(ToolId id);
    void toolConfigChanged(ToolId id, const QVariantMap& cfg);
signals:
    void toolSwitched(ToolId id);
    void toolConfigApplied(ToolId id, const QVariantMap& cfg);
};

// 2) WorkspaceMediator - 工作区切换 + dock 布局
class WorkspaceMediator : public QObject {
    Q_OBJECT
public:
    void switchWorkspace(const QString& name);
    void dockVisibilityChanged(int dockIndex, bool visible);
signals:
    void workspaceChanged(const QString& name);
    void dockContentRequested(int dockIndex, const QString& contentId);
};

// 3) DialogMediator - 非模态 dialog 调度
class DialogMediator : public QObject {
    Q_OBJECT
public:
    void showDialog(const QString& dialogId, const QVariantMap& args);
    void closeDialog(const QString& dialogId);
    void dialogResult(const QString& dialogId, const QVariantMap& result);
signals:
    void dialogShowRequested(const QString& dialogId, const QVariantMap& args);
    void dialogCloseRequested(const QString& dialogId);
};

// 各方只跟自己负责的 Mediator 通信
class LeftToolBar : public QWidget {
    ToolMediator* m_toolMed;       // 只持 ToolMediator
    void onToolClicked() { m_toolMed->switchTool(m_toolId); }
};
class ImageOptionBar : public QWidget {
    ToolMediator* m_toolMed;
    void onToolSwitched(ToolId id) { m_stackedWidget->setCurrentIndex(id); }
};
class RightPanelStack : public QWidget {
    WorkspaceMediator* m_workspaceMed;  // 只持 WorkspaceMediator
    void onWorkspaceChanged(const QString& name) { m_dockStack->rebuild(name); }
};
class HSLDialog : public QDialog {
    DialogMediator* m_dialogMed;
    void onApplyClicked() { m_dialogMed->dialogResult("HSL", m_params); }
};
```

**好处**: 加新工具/新 dock/新 dialog 只动对应 Mediator, 互不干扰, 编译期依赖也清楚 (谁持谁)

### 2.4 撤销栈 / 任何操作 → **Command 模式**

**问题**: 所有操作 (工具动作 / 菜单动作 / 滑块调整 / dialog 编辑) 都要 undo/redo

**Command 模式应用** (P0-3.3 已部分落地, 升级):
```cpp
class IImageCommand : public QUndoCommand {
public:
    virtual void execute() = 0;       // 跟 redo 区别: execute 第一次跑, redo 重跑
    virtual void undo() override = 0;
    virtual void redo() override = 0;
};

// 实例:
//   MoveLayerCommand / AddLayerCommand / SetAdjustmentLutCommand
//   ApplyFilterCommand / TransformCommand / DrawStrokeCommand
```

**关键**: **所有**操作走 QUndoStack, 包括工具栏按钮触发的 dialog 操作 (P0-3.3 已实 SetAdjustmentLut)

### 2.5 跨模块状态同步 (layer 改 → dock 改 → canvas 重画) → **Observer 模式**

**问题**: layer 改属性, dock panel 和 canvas 都要更新; 高频 layer 改不能多次通知

**Observer 模式应用**:
```cpp
class Layer : public QObject {
    Q_OBJECT
signals:
    void propertyChanged(int index, Layer* l);  // 一次性发, dock 自己判断要不要重画
};

class LayerPanel : public QWidget {
    void onLayerPropertyChanged(int i, Layer* l) {
        // 只更新对应 index 的 row, 不刷新整列表
        m_listView->update(m_proxyModel->mapFromSource(m_model->index(i, 0)));
    }
};
```

**关键**: **批量合并** — 高频操作 (slider drag) 期间不每帧 emit, 节流到 200ms 一次 (跟 P0-3.3 debounce 思路一致)

### 2.6 dock / panel 内容切换 → **Factory 模式**

**问题**: 工作区切换时, 3 dock 的内容是动态拼的 (哪个 dock 放哪些 tab 是 workspace 配置决定)

**Factory 模式应用**:
```cpp
class WorkspaceFactory {
public:
    static Workspace buildBasic();           // 颜色/属性/图层 三 dock
    static Workspace buildPhoto();           // 直方图/颜色/属性 (无图层)
    static Workspace buildPaint();           // 颜色/画笔设置
    static Workspace loadFromSettings(const QString& name);
};

class Workspace {
    QList<DockConfig> docks;   // 每个 dock 名字 + tabs + 默认 width
};
```

### 2.7 设计模式总结

| 模块 | 模式 | 主要作用 |
|------|------|---------|
| 工具切换 + 行为 | **State** | 状态机管理工具切换, 避免散在 if/else |
| 工具配置 (羽化/容差) | **Strategy** | 切换策略不改 ToolState 代码 |
| 工具 ↔ 工具栏 ↔ dock ↔ dialog | **Mediator 拆 3 个** (Tool/Workspace/Dialog) | 完全解耦, 避免 god class |
| 所有操作 (操作/菜单/dialog) | **Command** | 统一走 QUndoStack, undo/redo 一致 |
| Layer/参数变化通知 | **Observer** + 节流 | 避免高频重复通知, 性能 |
| 工作区预设 | **Factory** | 3 dock 内容动态拼装 |
| 5 tab 参数 + 调整 | **P0-3.3 已实** | debounce + LayerCommand |

---

## 3. 跨模块数据流 (低耦合 + 高性能)

### 3.1 数据流图

```
        [标题栏菜单]
            │
            ▼
      [Mediator 转发]
       /     |     \
      ▼      ▼      ▼
  [工具栏] [Dock] [Dialog]
      │      │      │
      └──────┼──────┘
             ▼
        [ImageWindow]
        /     |     \
       ▼      ▼      ▼
    [EngineContext]  [LayerStack]  [QUndoStack]
       (P0-2)         (P0-2.5)      (P0-3.3 debounce)
```

### 3.2 低耦合原则 (4 条)

1. **任何 UI 组件不能直接持有另一 UI 组件指针** — 全部通过 Mediator 信号槽
2. **任何 UI 组件不能直接调 ImageWindow 的 mutator** — 调 Mediator 转发
3. **任何后台操作 (OpenCV) 只能走 EngineContext** — 已经在 P0-2 实装, 不再允许直接调 cv::imread/blur 等
4. **任何 undo 只能走 QUndoStack** — 不能在工具/dialog 内部维护私有 "上次值"

### 3.3 高性能原则 (5 条)

1. **后台操作必走 EngineContext.background() pool** — 主线程不阻塞
2. **高频 slider 拖动用 debounce 200ms** — P0-3.3 已在 AdjustmentPanel 实装, 推广到所有 slider
3. **Observer 用节流 (200ms 一次) 避免重画风暴** — 比如 layer opacity 拖动期间
4. **大图 (>1024x1024) 走 LayerStack::renderAsync** — P0-2.5 已实
5. **非模态 dialog 关闭 = 隐藏, 状态保留** — 再次打开省初始化的开销

### 3.4 必须注意的 5 个数据流场景

| 场景 | 数据流 | 性能要求 |
|------|--------|---------|
| 1. 工具切换 (左侧点击) | Mediator.toolSwitched → ImageOptionBar.stack 切 page + ImageCanvas 装 eventFilter + cursor 变 | < 50ms |
| 2. 菜单触发 dialog (图层面板 → 图层属性) | Mediator 转发 → dialog 显, 用户编辑, 提交时 Mediator 发信号 | 显 dialog < 100ms |
| 3. dialog 实时改 (HSL slider 拖) | dialog → Mediator.dirtyEdit (节流 200ms) → ImageProcessorAsync → EngineContext 跑 → LayerStack 改 LUT → 画布重画 | 实时 (60fps 跟手) |
| 4. layer 改 (改名/不透明度) | LayerPanel 改 → Layer emit propertyChanged → LayerPanel 内部 update + ImageCanvas render | < 50ms |
| 5. undo/redo | QUndoStack 调度 → Command::undo/redo → 通过 Mediator 通知各方刷新 | < 100ms |

---

## 4. 模块清单 (新增 + 拆解, 全部按 §2 模式)

### 4.1 新增模块

| 模块 | 工作量 | 模式 | 依赖 |
|------|--------|------|------|
| `ImageMediator` (拆 3 个: ToolMediator / WorkspaceMediator / DialogMediator) | 2h | Mediator 拆 3 个, 避免 god class | P0-1~P0-3 已实 |
| `ToolState` 抽象类 + 8 实例 (Move/RectSelect/Lasso/MagicWand/Crop/Text/Brush/Eyedropper) | 4h | State + Strategy | ImageMediator |
| `LeftToolBar` (两列图标, 8 工具) | 2h | Mediator 订阅 | ToolState |
| `ImageOptionBar` (QStackedWidget 二级工具栏) | 2h | Mediator 订阅 + 8 page | ToolState |
| `WorkspaceFactory` + 4 默认工作区 (Basic/Photo/Paint/Web) | 1.5h | Factory | - |
| `RightPanelStack` (3 dock 上下排 + tab 切换) | 3h | Mediator + Factory | Workspace |
| `Dock 1 内容` (ColorPanel / SwatchPanel / GradientPanel / PatternPanel) | 3h | - | - |
| `Dock 2 内容` (PropertyPanel / AdjustmentLauncherPanel) | 1.5h | - | - |
| `Dock 3 内容` (LayerPanel 升级 + ChannelPanel + PathPanel) | 3h | Observer | Layer |
| 6 主菜单 + 子菜单 + 14 action | 1.5h | - | - |
| 6 dialog 入口 (HSL/Curves/Levels/B&W/ChannelMixer/Transform) | (P0 阶段 C 拆) | 已在 doc 上文 | - |

**总新增**: ~23h

### 4.2 拆解/删除

| 模块 | 动作 | 备注 |
|------|------|------|
| `imagewindow.ui` scrollLeft / leftPanel | **删** | 旧 5 toggle + 10 slider 全部消失 |
| `ImageAdjustmentPanel` (5 toggle + 10 slider) | **删** | 旧 m_adjustment 组件整体移除 |
| `AdjustmentPanel` (P0-3.2 5 tab) | **删** | 5 dialog 替代 (阶段 C 拆) |
| `infoDock` (灰度直方图/RGB 直方图/统计信息) | **删** | 移到状态栏, 默认折叠 |
| 旧 title bar 5 菜单 (文件/编辑/视图/工具/帮助) | **改** | 加 6 主菜单 |

### 4.3 CMakeLists 加新源

每个新 .h/.cpp 必须显式列(用户硬性规则, 见 `user.md` `CMakeLists 必须列所有源文件`)。

---

## 5. 重新设计阶段 (P0 现状 + 后续)

### 5.1 P0 现状盘点 (2026-09-08)

| P0 编号 | 状态 | 备注 |
|--------|------|------|
| P0-1 拆 ImageWindow 5 组件 | ✅ 完成 | ImageCanvas/ImageAdjustmentPanel/MosaicTool/TextOverlayController/ImageIOController |
| P0-2 EngineContext + 异步 | ✅ 完成 | 5 池预设, 异步 applyCurrentParams |
| P0-2.5 LayerStack::renderAsync | ✅ 完成 | 大图走 background |
| P0-3.1 LUT 函数 + applyLut | ✅ 完成 | 8 个 LUT 函数 |
| P0-3.2 AdjustmentPanel 5 tab | ✅ 完成 (将被拆) | 5 tab → 5 dialog (阶段 C) |
| P0-3.3 撤销栈 SetAdjustmentLut + debounce | ✅ 完成 (2026-09-08) | host overload + 200ms debounce |
| P0-4 选区系统 | 🟡 待 P0 UI 重写后 | 5 大选区 = 5 ToolState + SelectionStrategy |
| P0-5 滤镜系统 | 🟡 待 P0 UI 重写后 | 20 滤镜 = FilterDialog (非模态) + ApplyFilterCommand |
| P0-6 变换系统 | 🟡 待 P0 UI 重写后 | 自由变换 = TransformDialog + SelectionHandle |
| P0-7 文字图层 4 mode | 🟡 待 P0 UI 重写后 | 文字工具 = TextToolState + TextStrategy |
| P0-8 历史/导出/色彩空间 | 🟡 待 P0 UI 重写后 | ExportDialog + 3 栈合一 |

### 5.2 P0 之后还要做 (image 模块地基)

| 编号 | 任务 | 工作量 | 依赖 |
|------|------|--------|------|
| **F-1** | 3 个 Mediator (ToolMediator / WorkspaceMediator / DialogMediator) (Mediator 模式) | 2h | P0-3.3 |
| **F-2** | ToolState 抽象 + 8 工具实例 (State + Strategy) | 4h | F-1 |
| **F-3** | LeftToolBar (两列图标) | 2h | F-2 |
| **F-4** | ImageOptionBar (QStackedWidget 8 page) | 2h | F-2 |
| **F-5** | WorkspaceFactory + 4 默认工作区 (Factory) | 1.5h | F-1 |
| **F-6** | RightPanelStack 3 dock 框架 (Mediator) | 3h | F-5 |
| **F-7** | Dock 1 颜色/色板/渐变/图案 | 3h | F-6 |
| **F-8** | Dock 2 属性/调整入口 | 1.5h | F-6 |
| **F-9** | Dock 3 LayerPanel 升级 + 通道 + 路径 | 3h | F-6 + Layer |
| **F-10** | 6 主菜单 + 子菜单 (PS 同款) | 1.5h | - |
| **F-11** | 旧 leftPanel/infoDock/titleBar 5 菜单 清理 | 2h | F-3~F-10 |
| **F-12** | 5 dialog 入口 (HSL/Curves/Levels/B&W/ChannelMixer) | 4-5h | F-1 (Mediator) |
| **F-13** | AdjustDialogBase 基类 (合并 5 dialog 公共逻辑) | 2h | F-12 |
| **F-14** | 8 工具的 eventFilter + cursor 适配 | 2h | F-2 |
| **F-15** | 高频操作节流 (Observer debounce 200ms) | 1h | F-1 |

**总: 约 30-32h** (image 模块 UI 重写, 之前 doc 估 20-28h 偏乐观)

### 5.3 重新定义的阶段 (跟之前不一样)

| 阶段 | 内容 | 工作量 | 依赖 |
|------|------|--------|------|
| **F-A** | 清理 (拆旧 leftPanel/infoDock/旧 5 菜单, 6 主菜单 空架子, tab 去 home/pin) | 3h | P0-3.3 ✅ |
| **F-B1** | ToolMediator (工具切换 + 配置) | 0.7h | F-A |
| **F-B2** | WorkspaceMediator (工作区切换) | 0.7h | F-A |
| **F-B3** | DialogMediator (非模态 dialog 调度) | 0.6h | F-A |
| **F-C** | ToolState 抽象 + 2 工具实例 (Move + Eyedropper) (F-2 部分) | 1.5h | F-B1 |
| **F-D** | LeftToolBar 2 工具 (F-3 部分) | 1h | F-C |
| **F-E** | ImageOptionBar QStackedWidget 2 page (左侧主页按钮 + stack) | 1.5h | F-C |
| **F-F** | WorkspaceFactory + Basic workspace (F-5 部分) | 1.5h | F-B2 |
| **F-G** | RightPanelStack 3 dock 框架 + Layer 升级 (F-6 + F-9) | 5h | F-B2 |
| **F-H** | Dock 1 颜色/色板 (F-7 部分) | 2h | F-G |
| **F-I** | Dock 2 属性 (F-8) | 1.5h | F-G |
| **F-J** | 6 主菜单 + 子菜单 (F-10) | 1.5h | F-A |
| **F-K** | 5 dialog 拆 + AdjustDialogBase (F-12 + F-13) | 6-7h | F-B3 |
| **F-L** | 补齐剩余 6 工具 (RectSelect/Lasso/MagicWand/Crop/Text/Brush) (F-2 完) | 2.5h | F-C |
| **F-M** | Dock 1 渐变/图案 + Dock 3 通道/路径 (F-7 完 + F-9 完) | 2h | F-G |
| **F-N** | 8 工具 eventFilter + cursor (F-14) | 2h | F-L |
| **F-O** | Observer 节流 (F-15) | 1h | F-B1 |
| **F-P** | 旧 dock 全部清理 (F-11 完) | 1h | F-G 完 |

**总: 30-32h** 跟原 doc 估 20-28h 差不多, 但分阶段更细, 每段可独立测试

**总: ~30-32h** 跟原 doc 估 20-28h 差不多, 但分阶段更细, 每段可独立测试

### 5.4 阶段启动顺序 (依赖图)

```
F-A 清理 ──→ F-B Mediator ──→ F-C ToolState ──→ F-D LeftToolBar (2 工具)
                                 │                  │
                                 │                  ▼
                                 ├─→ F-E ImageOptionBar (2 page) ←┘
                                 │
                                 ├─→ F-F WorkspaceFactory
                                 │       │
                                 │       ▼
                                 ├─→ F-G RightPanelStack 3 dock ──→ F-H Dock 1 颜色/色板
                                 │       │                            │
                                 │       │                            F-I Dock 2 属性
                                 │       │
                                 │       └─→ F-M Dock 1 渐变/图案 + Dock 3 通道/路径
                                 │
                                 ├─→ F-J 6 主菜单
                                 │
                                 ├─→ F-K 5 dialog + AdjustDialogBase
                                 │
                                 ├─→ F-L 补 6 工具
                                 │
                                 ├─→ F-N 8 工具 eventFilter + cursor
                                 │
                                 ├─→ F-O Observer 节流
                                 │
                                 └─→ F-P 旧 dock 清理
```

---

## 6. 低耦合 + 高性能实施细则 (跟 §3 对齐)

### 6.1 低耦合 — 必须做的 5 件事

1. **创建 `ImageMediator` 单例** (per ImageWindow, 不是全局单例) — 各方只跟它通信
2. **禁止跨组件直接调方法** — 加编译期检查 (Doxygen 注释 + 团队规范)
3. **Mediator 信号设计** — 用 enum/specific signal 而不是 `void*` payload
4. **每个 ToolState 持 Mediator 引用** — 不持 ImageWindow 引用
5. **每个 Dock 内容持 Mediator 引用** — 不持 ImageWindow 引用

### 6.2 高性能 — 必须做的 5 件事

1. **EngineContext.background() 跑 OpenCV** — 主线程 0 阻塞
2. **所有 slider 加 QTimer debounce 200ms** — 跟 P0-3.3 一致
3. **Observer emit 节流 200ms** — 高频操作不每帧 emit
4. **大图走 LayerStack::renderAsync** — 已 P0-2.5
5. **非模态 dialog close=hide** — 状态保留, 避免重初始化

### 6.3 性能预算 (PS 同级别)

| 操作 | 预算 | 实测方法 |
|------|------|----------|
| 工具切换 | < 50ms | 高精度计时器 |
| 菜单弹子菜单 | < 30ms | 计时 |
| 非模态 dialog 显 | < 100ms | 计时 |
| HSL slider 跟手 | 60fps | 帧率统计 |
| 大图渲染 (4096x4096) | < 500ms (1 帧) | renderAsync 完成回调 |
| Undo/Redo | < 100ms | Command 计时 |

---

## 7. 风险 + 缓解

| 风险 | 等级 | 缓解 |
|------|------|------|
| Mediator 信号过多变 god object | 中 | 按主题分多个 Mediator (ToolMediator / WorkspaceMediator / DialogMediator) |
| State 模式 8 工具调试复杂 | 中 | 单元测试覆盖每个 ToolState 切换 |
| 旧 dock/panel 删不净, 启动 crash | 高 | F-P 阶段先 list 出所有 "old" widget 路径, 全部 unparent + 显式 delete |
| 设计模式 over-engineering | 中 | 必要场景才用, 不为模式而模式 |
| 性能不达标 | 中 | §6.3 性能预算 + 每个阶段末基准测试 |
| 主窗口状态机被误改 | **高 (user 强约束)** | F-A/F-P 阶段明确"不动 setMode/toggleMode/m_normalSize", 加 Doxygen 警告 |

---

## 8. 需要 user 拍板

1. **阶段 B 暂停确认** ✅ (已拍板)
2. **6 主菜单确认**: 文件/编辑/图像/图层/文字/选择/滤镜/视图 — 有要加减的吗?
3. **8 基础工具顺序**: PS 同款 (移动/矩形选框/套索/魔棒/裁剪/吸管/文字/画笔) — 还是先 5 工具?
4. **workspace 预设**: 基本功能/图形和 Web/绘画/摄影 — 还有别的?
5. **性能预算** (表 §6.3) 合理吗? 不合理就调
6. **Mediator 是否拆分** (ToolMediator / WorkspaceMediator / DialogMediator) — 拆还是单?
7. **阶段启动顺序** (图 §5.4) — 跟 user 节奏一致吗?

---

## 9. 当前 P0 阶段剩下的 (5.x 之外)

按 user 意思"重新分析 P0 还要做哪些":
- P0-1~P0-3.3 全做完 ✅
- P0-4~P0-8 是 image 模块的"专业功能", 不是"地基"
- 真正"地基" = §5.2 的 F-1~F-15 (30-32h), 即 image 模块 UI 重写
- F-1~F-15 完, image 模块 UI 跟 PS 同级别, 这时再做 P0-4~P0-8 (选区/滤镜/变换/文字/历史) 就是"填功能", 不是"打地基"

所以**新阶段划分优先级**:
1. F-A 清理 (3h) - 立即可做, 不依赖
2. F-B Mediator (1.5h) - 立即可做
3. F-C~F-E 工具栏基础 (4h) - 跟 F-B 串联
4. F-F~F-G Workspace + Dock 框架 (6.5h) - 跟 F-B 串联
5. F-H~F-I Dock 1+2 内容 (3.5h) - 跟 F-G
6. F-J 主菜单 (1.5h) - 跟 F-A
7. F-K dialog (6-7h) - 跟 F-B (依赖 Mediator)
8. F-L~F-P 收尾 (8.5h)

**总: ~30-32h, 3-4 周 (按 user 节奏)**
