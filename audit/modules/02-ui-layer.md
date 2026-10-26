# 02 · UI 层 / 自研 Dock — 审计报告

> 范围：`src/ui/**`（docwindow、FileTreeItem、FileTreeModel、FileTreeNodeData、homepage + .ui、infotreedock、logindialog、settingsdialog + .ui、settingsgpupanel、themegallerydialog、CMakeLists）与 `src/ui/dock/**`（VCompositeStrategy、VDraggableTabWidget、VDragManager、VDropIndicator、VPages、VRenderPath、VTabPageBase、CMakeLists）
> 规模：37 文件 / 约 5,036 行（ui 25 文件 3,546 行 + dock 12 文件 1,490 行）
> 审查者：audit-ui

## 1. 模块概览

UI 层是"外壳 + 面板容器"：`MainWindow` 持有 `HomePage`（启动页：新建/打开/最近文件）、`InfoTreeDock`（自研 Model/View 文件树，双击/回车后 emit `openFileRequested` 交给 `MainWindow::onHomeOpenPathRequested` 分发）、`SettingsDialog`（导航 + QStackedWidget，嵌入 `SettingsGpuPanel`）、`LoginDialog`、`ThemeGalleryDialog`（后两者为阶段 0 骨架）。`DocWindow` 是默认文本文档页，被 `MainWindow::addDocTab` 装进 QTabWidget；`RecentListModel`（models 层）桥接 `RecentManager`。`src/ui/dock` 是自研 PS 式面板系统：`VDraggableTabWidget`（QTabBar + QStackedWidget + 可浮动 QDockWidget wrapper）被 `src/media/docks/RightPanelDock` 用来托管 3 组共 9 个 `VTabPageBase` 适配页（颜色/属性/图层），`VDragManager` 是进程级拖拽注册表，`VDragIndicator` 画插入线。依赖方向：ui → core（ThemeManager/LanguageManager/RecentManager/FileExtensionRegistry）、models（RecentListModel）、media（GPU/BackendRegistry，`src/ui/dock` 还托管 media 的 9 个 dock 页，属反向耦合）。

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| UI-01 | P0 | VDraggableTabWidget.cpp:377 | crash | 跨 ImageWindow 把面板页拖到另一窗口后，任一窗口销毁 → 另一侧裸指针悬垂，交互即 UAF | `movePageTo` 只搬 widget，不同步宿主绑定（`AdjustmentPanel::m_host`/`RightPanelDock` 成员）且无 `pageMoved` 消费方 | 禁止跨顶层窗口移动，或移动时 rebind host + 宿主侧改用 `QPointer` 并订阅 page 信号 |
| UI-02 | P1 | VDraggableTabWidget.cpp:564 | logic | 拖拽插入/指示线全失效：跨组拖放永远 `insertIdx=-1`，drop 被静默吞掉 | `drop/dragEnter/dragMove` 把 `e->pos()`（局部坐标）当全局坐标传给 `mapFromGlobal` | 统一坐标空间：`mapToGlobal(e->position().toPoint())` 后再命中，或 helper 改收局部坐标 |
| UI-03 | P1 | VDraggableTabWidget.cpp:522 | crash | 拖拽中按 Ctrl+W 关闭当前文档 → 源 widget 在 `QDrag::exec` 嵌套事件循环内被删除，返回后 UAF | 在 eventFilter 里跑嵌套事件循环，无自保 `QPointer`、无"拖拽中禁止关窗" | 用 `QPointer<this>` 自检 + 拖拽期间禁用关闭动作，或把拖拽启动 defer 到队列 |
| UI-04 | P1 | VDraggableTabWidget.cpp:83 | state-sync | 有浮动页时切 tab 显示空白、改名改错 tab、`removePage` 删错 tab | `m_pages`（含 floating）与 QTabBar（仅 docked）两套索引被当作同一索引使用 | tab 内用 `QTabBar::setTabData` 存 page 指针，按 tabData 取页；删除/改名全部走同一映射函数 |
| UI-05 | P1 | VDraggableTabWidget.cpp:274 | logic | "脱出浮动"面板浮不起来：QDockWidget 作为子 widget 显示在面板内部且按父坐标 move | `new QDockWidget(this)`（flags=0 → 子 widget），`isFloating()==isWindow()` 恒 false | 无 parent 创建并 `setFloating(true)`；`move(globalPos)`；关闭判定改用 closeEvent/destroyed |
| UI-06 | P1 | VDraggableTabWidget.cpp:299 | crash | `removePage(floatingPage)` 后调用方仍持有 page 指针，事件循环一跑就 UAF | `setWidget(nullptr)` 只把 page 从 layout 移除、不改 parent；wrapper `deleteLater` 时 page 作为子对象被销毁 | detach 后显式 `page->setParent(nullptr)` 并文档化所有权；或明确 removePage=delete 并清空外部缓存 |
| UI-07 | P1 | FileTreeModel.cpp:98 | logic | 空目录/无权限目录永久显示展开箭头，点开永远空白且无任何错误提示 | `hasChildren` 对所有 dir 恒 true；`scanChildren` 失败静默 return 仍被 `setLazyLoaded(true)` | 记录"已加载且 0 子项"使 `hasChildren=false`；失败时保留可重试状态并通过 tooltip/状态栏报错 |
| UI-08 | P1 | FileTreeModel.cpp:183 | perf | 万级目录 expand 卡 UI（本地 ~1s+，网络盘可 10s+），滚动也卡 | UI 线程同步两次 `entryInfoList` + 每文件 `size()/lastModified()` stat + 一次性插入全部行；图标每帧重建 | 换 `QFileSystemModel`，或后台线程分批 `insertRows`（200/批）+ 懒取 size/mtime + 图标缓存 |
| UI-09 | P1 | settingsdialog.cpp:12 | logic | "自动保存""主题设置（系统/浅/深）"完全是假控件，勾选/切换无效 | 构造函数只接了 recentCount/语言/日志；`autoSave`、`themeCombo` 全工程零引用 | 接 QSettings + MainWindow 自动保存定时器；主题接 `ThemeManager::setTheme/save`；统一 Apply 语义 |
| UI-10 | P1 | LanguageManager.cpp:68 | i18n | 语言切到 English/日本語后界面仍全中文；"修改后立即生效"提示为假 | 3 份 .ts 各 839 条消息全部 `unfinished`；无 LanguageChange/retranslate 覆盖；MainWindow 还重复安装 translator（mainwindow.cpp:199-206 vs main.cpp:134） | 删重复 translator；实现全局 retranslate；补齐 .ts 翻译后再开放语言页 |
| UI-11 | P1 | BackendCapabilities.cpp:55 | state-sync | "强制 GPU"永远落到 CPU；"探测 OpenGL"只改一行文字，不影响渲染 | 启动时从不 probe，`m_gpuAvailable` 恒 false；Probe 结果不回写 capabilities；RenderMode 不持久化，`modeApplied` 无人接收 | 启动/Probe 后 `setForced(...)` 回写；QSettings 持久化 mode；状态栏监听 modeApplied |
| UI-12 | P1 | mainwindow.cpp:3161 | logic | 打开 .mp3/.mp4/未知二进制 → 先 `cv::imread` 失败，再落 `DocWindow` 当文本读 → 满屏乱码，大文件卡死/OOM | 打开路径无扩展名路由（registry 有 4 模块清单却不用），fallback 是无条件文本读 | 按 `moduleIdForExtension` 路由；未实现的模块明确拒绝；文本页加大小/二进制护栏 |
| UI-13 | P1 | docwindow.cpp:55 | data-loss | 打开 GBK/ANSI/带 BOM 文本再保存 → 非 UTF-8 字节被替换成 U+FFFD 写回，文件损坏；保存中途崩溃则原文件被截断 | 只用 UTF-8 `QStringConverter`，无 BOM/编码探测；`QIODevice::Truncate` 直接覆写原文件 | 探测编码并保持原编码保存；用 `QSaveFile` 原子替换；显示当前编码 |
| UI-14 | P2 | homepage.cpp:154 | ux | 最近记录单击就打开（PS 是单击选中）；双击会重复触发 openPathRequested（clicked + doubleClicked）；每次操作 model 全量 reset 丢选择/滚动 | 同时连 `clicked` 与 `doubleClicked` 到同一打开逻辑；RecentListModel 用 begin/endResetModel | 只保留一种：单击选中、双击打开；改增量 dataChanged 并恢复选择 |
| UI-15 | P1 | recentmanager.cpp:69 | state-sync | 已"固定"的最近文件被再次打开后会掉 pin（列表跳位） | `touchOpen` 先 erase 旧条目再新建 `e.pinned=false`，未保留旧 pinned | 先查旧条目保存 pinned，再重建/更新条目 |
| UI-16 | P1 | mainwindow.cpp:2465 | ux | 主页 4 个模块按钮（图像/音频/视频/视觉）点击无任何反应也无提示 | `onHomeModuleRequested` 仅 `LOG_INFO`；按钮无禁用态 | 未实现前禁用并加"阶段 1"tooltip，或从主页移除 |
| UI-17 | P2 | FileTreeModel.cpp:165 | logic | 文件系统外部增删不刷新；树里双击已删文件只弹"打开失败"；无刷新/右键菜单入口 | Model 无 watcher、无 refresh API，`InfoTreeDock` 也没有刷新按钮 | `QFileSystemWatcher` + debounce 重扫，或换 QFileSystemModel；补右键（打开/定位/刷新） |
| UI-18 | P2 | mainwindow.cpp:1445 | ux | 信息树默认隐藏；打开后无法关闭；"视图菜单有信息树入口"的注释与实现不符 | `buildInfoTree` 隐藏 dock 并去掉 Closable；`buildActions` 视图菜单无该 entry | 恢复 Closable + 加 `toggleViewAction()` 到视图菜单；默认显示或记住上次可见性 |
| UI-19 | P2 | FileTreeModel.cpp:254 | logic | 树里看不到 .txt/.md/无扩展名文件，但应用能新建和打开 txt；隐藏文件也不显示 | 过滤只查 `FileExtensionRegistry`（只含图像/音视频原始格式），且 `QDir::Files` 默认排除隐藏文件 | 过滤表补文本/项目扩展名；加"显示所有文件"开关 |
| UI-20 | P2 | FileTreeModel.cpp:68 | perf | 滚动时每行重新 `QFileIconProvider::icon(QFileInfo)`（Windows 走 shell 查询），大目录滚动掉帧 | DecorationRole 每次 paint 现取，无缓存；`QFileInfo` 现构造 | 用 `QHash<QString,QIcon>` 缓存或 `QFileIconProvider` + `QFileIconProvider::icon(type)` |
| UI-21 | P2 | FileTreeModel.cpp:25 | crash | 传入 stale/他 model 的 QModelIndex 会直接 `static_cast` internalPointer 解引用；未来加刷新/右键必踩 | `index/parent/rowCount/data/expand` 均无 `index.model()!=this` / `hasIndex` 校验；静态 `itemForIndex` 返回裸指针 | 每个入口加 model 身份与 `hasIndex` 校验；对外改返回 `QPersistentModelIndex` |
| UI-22 | P2 | FileTreeItem.cpp:26 | perf | 宽目录下 `parent()` 每次 O(n) 线性 `indexOf`，10k 同级节点布局 O(n²) | 节点只存 `QList`，靠线性查找求 row | 节点缓存自身 row 并在插入/删除时批量更新，或父节点存 `QHash<Item*,int>` |
| UI-23 | P2 | homepage.cpp:217 | ui-visual | 主页大按钮卡片样式从未生效；"最近""模块"两个分区标题不跟随主题色 | QSS 选择器 `QToolButton#bigAction/#tplCard` 与 .ui objectName（btnNew/modImage…）不匹配；三个 `sectionTitle` 重名被 uic 改成 `sectionTitle1/2` | 统一 objectName；`QLabel#sectionTitle1/2` 补规则，或改 `QFrame > QLabel[class="sectionTitle"]` |
| UI-24 | P2 | homepage.cpp:42 | ui-visual | 固定行背景硬编码浅绿、固定图标是剪刀 ✂、删除图标 ✖ 与关闭同形；4/5 列无选中态 | delegate 自绘绕过 base paint，颜色 `QColor(200,240,200)` 写死 | delegate 用 `opt.palette`/ThemeManager 颜色；图标换 QStyle 标准图标或 SVG |
| UI-25 | P2 | infotreedock.cpp:40 | ui-visual | 暗色主题下信息树选中行文字仍是 `#1a1a1a`（不可读），且切主题不重刷 | 树样式表写死 `#4caf80/#1a1a1a`，`InfoTreeDock` 未监听 themeChanged | 改由全局 QSS/QPalette 驱动，删除局部 setStyleSheet |
| UI-26 | P2 | settingsdialog.cpp:88 | state-sync | 日志设置"应用"后重启即恢复默认：模块名/目录/级别/大小都没持久化；级别匹配靠 `itemText.split(' ')` 前缀 | Logger 无 QSettings 读写；setupLogPage 里有死代码（itemData → toStdString 后丢弃） | Logger 增加 `load()/save()`；combo 用 userData 存 level 名；去掉前缀匹配 |
| UI-27 | P2 | settingsgpupanel.cpp:31 | i18n | GPU 页全部中文用 `QStringLiteral` 写死（切语言不翻译），状态文字色 `#333/#555` 暗色主题下看不清 | 面板自建控件不用 `tr()`，颜色写死 | 全部 `tr()`；颜色改 `palette()`；nav "GPU 设置"（settingsdialog.cpp:206）同样处理 |
| UI-28 | P2 | themegallerydialog.cpp:129 | logic | 主题画廊只是预览：应用后仅弹 QMessageBox，不改 ThemeManager、不持久化；色卡/亮暗模式与全局主题无联动 | 阶段 0 骨架；ThemeManager 也没有 accent 接口；预览色写死 #fff/#2b2b2b | 先接 `ThemeManager::setTheme`（已有）+ QSettings；扩展 setAccentColor 后接 8 色卡；否则从设置入口移除 |
| UI-29 | P2 | logindialog.cpp:60 | logic | 登录对话框 5 个按钮只 emit + accept，不调 AuthClient/UserManager，仅状态栏提示；`setDefaultProvider` 无任何消费 | 阶段 0 骨架（注释自述），但菜单入口照常开放 | 未接网络前入口禁用或标明"未实现"；`m_defaultProvider` 接进按钮默认焦点 |
| UI-30 | P2 | mainwindow.cpp:2110 | logic | 文本页 Ctrl+Z/Ctrl+Y 无效（弹"没有可撤销的操作"）；QPlainTextEdit 自己的撤销被菜单 QAction 抢走 | `m_actUndo` 用 `QKeySequence::Undo` 且 onUndo 只转发 ImageWindow | onUndo 先判 DocWindow → `editor()->undo()`；或对文本页移除 ApplicationShortcut |
| UI-31 | P2 | docwindow.cpp:19 | state-sync | 每次打开文件 tab 标题先闪一下 `*`，`updateWindowTitle` 被多触发一次 | `setPlainText` 触发 textChanged → markDirty → 随后 setClean | load 期间 `QSignalBlocker` 或 `document()->setModified` 前后暂停 markDirty |
| UI-32 | P2 | mainwindow.cpp:3117 | ux | 主页"打开文件夹"后停在主页，不切工作区；与"打开文件"行为不一致 | `onHomeOpenFolderRequested` 只 show dock + statusBar，无 `switchToWorkspace()` | 打开目录后切工作区（与 openFile 一致），或明确停留在主页并显示说明 |
| UI-33 | P2 | homepage.ui:174 | ui-visual | 大量写死最小尺寸/固定宽（首页 160×96、模块 130×88、设置 nav 120、输入框 180/240），高 DPI 或长译文会截断 | .ui 里硬编码 minimumSize/固定 nav 宽 | 改用 sizeHint + `setMinimumWidth(0)` + 布局 stretch；至少给按钮设置 `sizePolicy.Expanding` |
| UI-34 | P3 | VPages.h:46 | i18n | 面板 tab 标题写死英文 "Color/Layers/Adjustments…"（无 tr），中文界面里中英混排 | `pageTitle()` 返回 `QStringLiteral` | 改成 `tr()`/QCoreApplication::translate("VPages", …) |
| UI-35 | P3 | VTabPageBase.h:45 | design | `preferredRenderPath()/preferredCompositeStrategy()/contentWidget()/pageTitleChanged` 与 `setCompositeStrategy` 全工程无消费，注释却称"用户拍板/1 行接入" | 占位 API 被当成已完成能力 | 要么接进渲染路径，要么标 `[[deprecated]]/TODO` 并在文档说明 |
| UI-36 | P3 | VDragManager.cpp:39 | design | `tabWidgetAt/activeIndicator/indicatorOwner/attachTo` 死代码；`releaseIndicator` 忽略 owner；`endDrag` 的 accepted 恒为"source 活着" | 拖拽系统重构后未清理 | 删除死代码或补齐 drop 目标解析；`endDrag(bool accepted)` 显式传 |
| UI-37 | P3 | VDraggableTabWidget.cpp:483 | memory | 每次拖拽 new 一个 QDrag（parent=this）+ QMimeData，Qt 不在 exec 后删除，长会话缓慢累积；mime 里还携带 page 指针字符串 | `new QDrag(this)` 后未处理生命周期 | `drag->deleteLater()` 在 exec 返回后；payload 用 tabData 而非指针 |
| UI-38 | P3 | settingsdialog.cpp:206 | build | `vistella_ui` 直接 include/link media 的 GPU 符号（settingsgpupanel.h:6、cpp:3-5），但 ui/CMakeLists.txt:46-53 只链 core/models，靠 exe 的 link 顺序兜住 | 分层被破坏：ui 反向依赖 media 实现 | 把 GPU 设置页移到 app/media，或在 core 抽一个 `IRenderBackendStatus` 接口 |
| UI-39 | P3 | VDraggableTabWidget.cpp:664 | design | `onTabBarMousePress/Move/Release`、`m_pressLocalPos`、`m_dragSourceIndex`、`Shape::OnTab` 全是空/未用代码，误导后续维护 | 迭代残留 | 删除 |
| UI-40 | P3 | homepage.ui:90 | i18n | 主页副标题仍写"文本 / 图像 / 3D 模型一站搞定"，3D 模块早已删除，实际模块是 图像/音频/视频/视觉 | 文案未随功能演进更新 | 改成实际能力描述 |
| UI-41 | P3 | VDropIndicator.cpp:39 | design | `detach()` 把 indicator 变成无 parent 顶层窗口并 hide；下次 attach 时 setParent→hide/show，dragLeave 时全局 detach 会导致指示线闪烁 | 单例 overlay 用 setParent 切换宿主 | 保持一个隐藏顶层 overlay，用 `move/resize/raise` 定位，不反复换 parent |
| UI-42 | P3 | mainwindow.cpp:199 | state-sync | MainWindow 构造时又装一份 translator（与 LanguageManager 重复）；切回 zh_CN 后旧 translator 仍留在 app 上 | 历史遗留的双入口 | 只保留 LanguageManager 一处 |

> P0/P1 展开见下；P2/P3 的修复建议已在表中给到，正文不再重复。

### UI-01 跨窗口移动面板页后宿主与裸指针悬垂 (P0)

- **位置**：`src/ui/dock/VDraggableTabWidget.cpp:377-413`、`src/media/docks/RightPanelDock.cpp:182-200`、`src/media/imagewindow.cpp:393-394`、`src/media/imagewindow/AdjustmentPanel.h:196`
- **证据**：

```cpp
// VDraggableTabWidget.cpp:385-408
PageState s = m_pages.takeAt(srcIdx);
...
target->m_pages.insert(insertIndex, s);
target->rebuildIndexMap();
page->setParent(target);
target->m_tabBar->insertTab(insertIndex, s.title);
target->m_stack->insertWidget(insertIndex, page);
page->installEventFilter(target);
```

```cpp
// imagewindow.cpp:393-394
m_adjustmentPanel = m_rightDock->adjustmentPage()->inner();
m_adjustmentPanel->setHost(this);           // AdjustmentPanel::m_host 是裸 ImageWindow*
```

- **现象 / 复现路径**：拖一个"调整(Adjustments)"页到另一个 ImageWindow 的面板组 → 目标窗口里点"应用"会改到源图像；随后关闭任意一个窗口，另一窗口再刷新/操作该页时崩溃（UAF）。目前被 UI-02 的坐标 bug 掩盖，属"修复 drop 后必现"的 P0。
- **根因**：机制层三点叠加——(1) `VTabPageBase` 适配页在内层 widget 里缓存了创建者 ImageWindow（`AdjustmentPanel::m_host`、ColorTab/ChannelPathPanel 的 connect、HistoryDock 的 undoStack）；(2) `movePageTo` 只搬 widget，没有任何 rebind 逻辑，`RightPanelDock`/`ImageWindow` 里的 `m_colorPage/m_propertyPage/...`、`m_adjustmentPanel` 仍是裸指针且不监听 `pageAdded/pageMoved/pageRemoved`；(3) 任一窗口析构时只会删自己的 QObject 子树：页被移到对方窗口后，源窗口的指针变悬垂；页留在源窗口、目标窗口先死时同理。
- **修复建议**：短期在 `dropEvent` 里加约束 `if (this->window() != src->window()) { e->ignore(); return; }`（只允许同一 ImageWindow 的 3 个子面板互拖），并把 `RightPanelDock` 所有 page 成员改 `QPointer`；中期给 `VTabPageBase` 增加 `rebindHost(ImageWindow*)` 虚函数，在 `pageMoved/pageAdded` 里由 `RightPanelDock` 统一调用；`AdjustmentPanel::m_host` 等改 `QPointer<ImageWindow>`。另在拖拽期间禁用 Ctrl+W（见 UI-03）。
- **关联**：RightPanelDock / ImageWindow / 所有 V*Page 内层 dock；09-panels 审查员可能已报 `Deprecated accessor 返回 nullptr`，但本条的跨窗口 UAF 是拖拽系统特有。

### UI-02 drop 坐标空间混用导致拖拽全链路失效 (P1)

- **位置**：`src/ui/dock/VDraggableTabWidget.cpp:538-550`、`558-595`、`597-633`、`641-655`
- **证据**：

```cpp
void VDraggableTabWidget::dropEvent(QDropEvent* e)
{
    ...
    QPoint gpos = e->pos();                 // 接收者局部坐标
    int insertIdx = calcInsertIndexAt(gpos); // 期望全局坐标
```

```cpp
int VDraggableTabWidget::calcInsertIndexAt(const QPoint& globalPos) const
{
    QPoint local = m_tabBar->mapFromGlobal(globalPos);   // 局部值被当全局
    if (!QRect(0, 0, m_tabBar->width(), m_tabBar->height()).contains(local))
        return -1;
```

- **现象 / 复现路径**：从颜色组拖"A"标签到属性组标签栏 → 没有任何插入指示线；松手无反应。只有主窗口恰好位于屏幕 (0,0) 时才有可能命中（局部坐标≈全局坐标）。
- **根因**：`QDragEnterEvent/QDragMoveEvent/QDropEvent::pos()/position()` 是相对接收 widget 的坐标；`updateDropHint/calcInsertIndexAt/isOverEmptyArea` 的形参命名与实现都按 `mapFromGlobal` 处理。事件先被 tab bar 子控件忽略并冒泡到 `VDraggableTabWidget` 时坐标仍是该 widget 局部坐标，于是 `mapFromGlobal(local)` 得到大幅负值 → 恒 `-1` → drop 只 `acceptProposedAction()` 不做任何移动（拖拽"看起来生效但页面不动"）。同一错误也出现在 `dragEnterEvent/dragMoveEvent` 的 `updateDropHint(e->pos())`。
- **修复建议**：在事件入口统一换算 `const QPoint global = mapToGlobal(e->position().toPoint());` 再传 helper；或把 helper 全部改为接收"本 widget 局部坐标"（内部用 `m_tabBar->mapFrom(this, p)`）。加 `QTest`：把窗口 move 到非 (0,0)，构造 `QDropEvent` 断言插入索引，防止回归。
- **关联**：`VDragManager::tabWidgetAt` 同样基于全局坐标但当前无人调用，修坐标时一并处理。

### UI-03 QDrag::exec 嵌套事件循环内源 widget 可能被销毁 (P1)

- **位置**：`src/ui/dock/VDraggableTabWidget.cpp:476-532`（`maybeStartDrag` 由 `eventFilter` 的 MouseMove 调用，见 437-450）
- **证据**：

```cpp
    m_activeDrag = drag;
    VDragManager::instance().beginDrag(this, page);
    Qt::DropAction action = drag->exec(Qt::MoveAction | Qt::IgnoreAction, Qt::MoveAction);
    VDragManager::instance().endDrag();
    m_activeDrag.clear();
    return action == Qt::MoveAction;
```

- **现象 / 复现路径**：按住面板 tab 拖动，同时按 Ctrl+W（`m_actCloseCur` 用 `QKeySequence::Close`，ApplicationShortcut 在嵌套事件循环里仍会派发）→ 当前 ImageWindow 被关闭 → 其 QObject 子树（含本 `VDraggableTabWidget`）析构 → `drag->exec` 返回后继续访问 `this` 的 `m_activeDrag`、调 `VDragManager::endDrag()` → UAF。`QDrag` 的 parent 是 `this`，源 widget 被删时 QDrag 也在拖拽中析构，Qt 侧同样有风险。
- **根因**：拖拽启动发生在 `eventFilter` 里，直接进入 `QDrag::exec` 的嵌套事件循环；期间任意"可关闭文档/可退程序"的入口都能把拖拽源删掉，而成员函数没有存活自检（C++ 成员函数无法可靠检测 this 已释放）。
- **修复建议**：`VDragManager` 增加 `isDragging()`；MainWindow/ImageWindow 的关闭动作与 `onTabCloseRequested` 在拖拽中直接返回（或 `QAction::setEnabled(false)`）。启动拖拽改为 `QTimer::singleShot(0, this, [this]{...startDrag...})`，让 exec 不再位于 eventFilter 栈内；若无法避免，至少用 `QPointer<VDraggableTabWidget> self(this)` 包裹 exec 后续代码并 `if (!self) return true;`（只能降低概率，不能根治 delete-this 语义）。
- **关联**：MainWindow 关闭路径、ImageWindow 析构顺序；与 UI-01 同属拖拽生命周期问题。

### UI-04 m_pages 与 QTabBar 双索引失配 (P1)

- **位置**：`src/ui/dock/VDraggableTabWidget.cpp:83-97`、`163-176`、`225-233`、`312-335`、`337-363`
- **证据**：

```cpp
// currentChanged：idx 是 tabBar 索引，却拿 m_pages[idx]
connect(m_tabBar, &QTabBar::currentChanged, this,
        [this](int idx) {
            if (idx >= 0 && idx < m_pages.size()) {
                QWidget* page = m_pages[idx].page;
                if (page) m_stack->setCurrentWidget(page);
```

```cpp
void VDraggableTabWidget::undockPageFromStack(QWidget* page)  // 312
{
    int idx = indexOfPage(page);      // m_pages 索引
    ...
    m_stack->removeWidget(page);
    m_tabBar->removeTab(idx);         // 却被当成 tabBar 索引
```

- **现象 / 复现路径**：`undockPage(第 0 页)` 后 m_pages=[floating0,p1,p2]，tabBar=[p1,p2]。切到 tab 0 → lambda 用 `m_pages[0]`（floating page）→ `setCurrentWidget` 对不在 stack 的 widget 无效，内容区空白、`currentChanged` 信号携带错误 page；`removePage(p2)` 会 `removeTab(2)` 越界/删错；`setPageTitle` 会改名错误的 tab。测试 `undockPage_createsFloatingWrapper` 只查计数，未覆盖该失配。
- **根因**：`m_pages` 同时承担"全部页（含浮动）"与"tab 顺序"两个语义，而 QTabBar 只包含 docked 页；所有用 tab 索引的地方（currentChanged/tabCloseRequested/removeTab/setTabText/insertTab）都没有经过 docked 索引映射。`m_indexOf` 只解决反向查找，没解决正向映射。
- **修复建议**：把 page 指针写进 `QTabBar::setTabData(i, QVariant::fromValue(page))`，所有 tab 事件通过 `tabData` 取页；删 tab/改名/插入前统一 `int tabIdx = dockedIndexOf(page)`。`PageState` 拆成 `m_allPages` + `m_dockedOrder`（或直接让 QTabBar 成为 docked 顺序的唯一来源），并补"浮动+重排+redock"的映射测试。
- **关联**：`removePage`、`dockPageToStack`、`movePageTo` 的 insertIndex 语义都受影响。

### UI-05 "浮动" QDockWidget 实际浮不起来 (P1)

- **位置**：`src/ui/dock/VDraggableTabWidget.cpp:272-297`、`312-335`
- **证据**：

```cpp
QDockWidget* VDraggableTabWidget::createDockWrapper(QWidget* page)
{
    auto* dock = new QDockWidget(this);   // parent = this => 普通子 widget
    ...
    dock->setWidget(page);
    connect(dock, &QDockWidget::visibilityChanged, this,
            [this, dock, page](bool visible) {
                if (!visible) {
                    if (!dock->isFloating()) {      // 恒为 true
                        emit pageCloseRequested(page);
```

```cpp
// Qt 6.11.1 qtbase/src/widgets/widgets/qdockwidget.h:59
inline bool isFloating() const { return isWindow(); }
```

- **现象 / 复现路径**：调用 `undockPage()`（programmatic/未来拖出）后，页没有变成桌面浮动窗口，而是作为一个子 widget 出现在面板内部、位置还按父坐标偏；所有"隐藏"都会被误判为用户点了关闭（因为 `isFloating()` 恒 false，visibilityChanged(false) 一律 emit `pageCloseRequested`）。
- **根因**：QDockWidget 的 ctor 用 `QWidget(parent, flags)`，flags 默认 0 → 子 widget；`show()/move()` 都只在父窗口内生效；关闭信号用 `visibilityChanged` + `isFloating()` 判断本身就是错的（隐藏原因多种）。
- **修复建议**：`new QDockWidget()`（无 parent）+ `setFloating(true)` + `setAttribute(Qt::WA_DeleteOnClose, false)`，用 `move(QCursor::pos()-offset)` 定位；关闭用重写 `closeEvent` 或连接 `QDockWidget::topLevelChanged` + `close` 事件；`dock` 被销毁时从 `m_pages` 移除并通知宿主（`pageCloseRequested`）。
- **关联**：UI-06（wrapper 释放所有权）、RightPanelDock 的显隐逻辑（父 dock 隐藏会连带隐藏子 dock）。

### UI-06 removePage(floating) 不转移 parent，page 随后被 wrapper 删除 (P1)

- **位置**：`src/ui/dock/VDraggableTabWidget.cpp:163-176`、`299-310`
- **证据**：

```cpp
void VDraggableTabWidget::destroyDockWrapper(PageState& s)
{
    QDockWidget* dock = s.dockWrapper.data();
    s.dockWrapper.clear();
    s.isFloating = false;
    if (dock) {
        if (s.page) dock->setWidget(nullptr);   // 只从 layout 移除
        dock->deleteLater();                    // page 仍是 dock 的子对象
    }
}
```

- **现象 / 复现路径**：若调用方 `removePage(floatingPage)` 后继续用 `page`（例如把页从右侧面板移除但仍保存指针做后续 restore），下一轮事件循环 wrapper 被删时 page 一起被 delete → 调用方悬垂。Qt 源码 `QDockWidgetLayout::setWidgetForRole` 只做 `old->hide(); removeWidget(old);`，**不 reparent**。
- **根因**：`removePage` 的文档承诺与实际所有权相反（"page owned by us; destroyed with us"），浮动分支只处理 wrapper 不处理 page 的 parent/所有权，且未把 page 从外部缓存中摘除。
- **修复建议**：detach 后显式 `page->setParent(nullptr); page->hide();` 再 `deleteLater` wrapper，并在 API 文档明确"removePage 不销毁 page"；同时给 `pageRemoved` 信号一个消费方（RightPanelDock）清空裸指针。
- **关联**：UI-01/UI-05。

### UI-07 空目录/无权限目录永久假展开 + 错误被吞 (P1)

- **位置**：`src/ui/FileTreeModel.cpp:88-100`、`143-171`
- **证据**：

```cpp
bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    ...
    // 目录: 永远 hasChildren (lazy load 用占位 "..." 触发)
    if (p->data().isDir) return true;
    return false;
}
```

```cpp
void FileTreeModel::expand(const QModelIndex &index)
{
    ...
    scanChildren(item);        // 目录不可读/为空时直接 return
    item->setLazyLoaded(true); // 仍标记已加载 => 永不重试
}
```

- **现象 / 复现路径**：展开空目录（或 `C:\System Volume Information` 这类无权限目录）后，箭头仍显示可展开，再点永远无内容、无报错；文件被外部删除后目录变空也不会更新箭头。
- **根因**：`hasChildren` 用 `isDir` 代替"已知有子项"，且没有把"扫描完成且为空/失败"和"尚未扫描"区分开；`scanChildren` 早退路径不返回状态，`QDir::entryInfoList` 的失败也没有被检查（QDir 只返回空 list）。
- **修复建议**：给 `FileTreeItem` 加 `enum ScanState { Unknown, Loading, Loaded, Failed }`：`hasChildren` 在 `Loaded && childCount()==0` 时返回 false，`Failed` 时返回 false 并允许下次 expand 重试；失败时通过 `data(ToolTipRole)` 或 InfoTreeDock 状态栏显示"无权限/路径不存在"。删除未使用的占位写法或改成 `fetchMore` 语义。
- **关联**：`InfoTreeDock::onExpanded`（infotreedock.cpp:84-89）。

### UI-08 文件树同步全量扫描阻塞 UI (P1)

- **位置**：`src/ui/FileTreeModel.cpp:165-233`（由 `InfoTreeDock.cpp:50-51, 84-89` 的 `expanded` 同步驱动）
- **证据**：

```cpp
const QFileInfoList dList = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
for (const QFileInfo &fi : dList) {
    dirs.append({ fi.fileName(), fi.absoluteFilePath(), true, 0, fi.lastModified() });
}
const QFileInfoList fList = d.entryInfoList(QDir::Files, QDir::Name);
for (const QFileInfo &fi : fList) {
    if (!isFileSupported(fi.absoluteFilePath())) continue;
    files.append({ fi.fileName(), ..., fi.size(), fi.lastModified() });
}
...
beginInsertRows(parentIdx, 0, total - 1);   // 一次性插入全部
```

- **现象 / 复现路径**：在有数万文件的目录（下载目录、网络共享）展开节点时 UI 冻结；`QFileInfo::size()/lastModified()` 每项至少一次 stat，网络盘 RTT 进一步放大；展开后 QTreeView 对全部行做布局，滚动时又逐行重建 `QFileIconProvider`（UI-20）。
- **根因**：模型在 UI 线程同步做两次全目录枚举 + 逐项 stat + 一次性建全部 `FileTreeItem`；没有分批插入、没有取消、没有后台线程；`InfoTreeDock` 在 `expanded` 信号里直接调用。文件树本应使用 Qt 自带的异步 `QFileSystemModel`（`QFileInfoGatherer` 后台线程 + `fetchMore` 增量）。
- **修复建议**：优先换 `QFileSystemModel` + `QSortFilterProxyModel`（按扩展名/模块过滤），可直接获得异步、watcher 与图标缓存；若保留自研 model：后台线程 `QDirIterator` 收集 → `QMetaObject::invokeMethod` 回 UI 线程按 200 条/批 `beginInsertRows`，`size/mtime` 改为按 role 懒取，扫描期间显示 busy 光标并提供取消（`QFutureWatcher`）。
- **关联**：UI-17/UI-20/UI-22 同源。

### UI-09 设置页假控件：自动保存与主题设置零连接 (P1)

- **位置**：`src/ui/settingsdialog.cpp:12-46`（构造函数只见 recentCount/language/log/GPU）、`src/ui/settingsdialog.ui:183,284,311`
- **证据**：

```cpp
// settingsdialog.cpp 构造函数里没有任何 autoSave / themeCombo 的引用
ui->recentCount->setRange(0, 100);
connect(ui->recentCount, qOverload<int>(&QSpinBox::valueChanged), this,
        [](int v) { RecentManager::instance().setMaxUnpinned(v); });
```

```xml
<widget class="QCheckBox" name="autoSave"> ... </widget>
<widget class="QComboBox" name="themeCombo"> 系统默认 / 浅色 / 深色 </widget>
<widget class="QLabel" name="themeNote"> (主题设置需要重启生效) </widget>
```

- **现象 / 复现路径**：勾选"启用自动保存"、切换"主题 = 深色"并按"确定" → 什么都没发生；提示文案还写着"需要重启生效"，实际没有任何持久化。
- **根因**：设置页只做了布局，没有 SettingsStore；主题切换的真实入口是标题栏 ☀ 按钮（`ThemeManager::toggleTheme`），但设置页未复用；`btnOk` 只是 `accept()`，不存在"应用"语义。
- **修复建议**：抽 `SettingsStore`（QSettings 封装）统一读写；`themeCombo` → `ThemeManager::setTheme(Theme)` + `ThemeManager::save()`；`autoSave` → MainWindow 的 `QTimer`（间隔可由第二控件设置）并把开关持久化；`.ui` 去掉"需要重启"错误提示，统一 Apply/Cancel。
- **关联**：core/ThemeManager（已有 Light/Dark + save/load，属"Qt 原生就能实现的缺口"）。

### UI-10 语言切换三重失效：零翻译、双 translator、无 retranslate (P1)

- **位置**：`src/core/LanguageManager.cpp:68-77`、`src/app/mainwindow.cpp:199-206, 239-242, 3351-3360`、`translatefile/vistella_en_US.ts` / `vistella_ja_JP.ts`
- **证据**：

```cpp
// main.cpp:134 已经装过一次
LanguageManager::instance().installInitial();
```

```cpp
// mainwindow.cpp:199-206 又装一次同名 translator
const QString lang = s.value("App/language", "zh_CN").toString();
if (lang != "zh_CN") {
    auto *tr = new QTranslator(this);
    if (tr->load(":/translatefile/vistella_%1.qm".arg(lang)))
        QApplication::installTranslator(tr);
}
```

```
$ foreach ts: messages=839 unfinished=839 translated=0   // en_US / ja_JP 全部未翻译
```

- **现象 / 复现路径**：设置 → 语言切换 → 选 English → 弹"界面语言已切换…需要重新打开窗口" → 界面仍全中文；重启后仍中文（翻译为空）。若未来补上翻译，由于 MainWindow 的重复 translator 无法被 `LanguageManager::uninstall()` 移除，切回中文时旧语言仍生效。
- **根因**：(1) `.ts` 从未填写译文（CMake 的 lupdate/lrelease 流程是通的，只是内容为空）；(2) `LanguageManager::setLanguage` 明确不 retranslate（LanguageManager.h:32 注释），而只有 MainWindow 实现了 `changeEvent(LanguageChange)` 且只刷新信息树标题/tab 标题，HomePage、SettingsDialog、InfoTreeDock、LoginDialog、ThemeGalleryDialog、各 dock 都没有 retranslate；(3) uplink 有两个 translator 安装点；(4) `setLanguage` 返回 void，加载失败也提示"已切换"。
- **修复建议**：删除 mainwindow.cpp:199-206；`LanguageManager::setLanguage` 返回 bool；在 MainWindow::changeEvent(LanguageChange) 里向所有顶层 widget 递归 `QEvent::LanguageChange`（或逐个窗口重写 changeEvent 调 `ui->retranslateUi(this)` + 自建控件 setText）；补齐 en/ja 译文（839 条 ×2，可用 AI 辅助）；语言页加"需重启"真实语义或实现全量刷新。
- **关联**：跨模块（app/core/ui 都要动）；Lead 需要合并其他审查员对 i18n 的重复结论。

### UI-11 GPU 设置页与 BackendRegistry 不闭环 (P1)

- **位置**：`src/ui/settingsgpupanel.cpp:101-127`、`src/media/gpu/BackendCapabilities.cpp:55-82`、`src/media/gpu/BackendRegistry.cpp:39-60`、`src/app/mainwindow.cpp:3464-3476`
- **证据**：

```cpp
// settingsgpupanel.cpp:114-127 Probe 只显示，不回写 capability
const bool available = gpu::BackendCapabilities::probeOpenGL();
const QStringList summary = gpu::BackendCapabilities::instance().summary();
m_probeLabel->setText(...);
```

```cpp
// BackendCapabilities.h:33 / cpp：m_gpuAvailable 默认 false
bool isGpuAvailable() { return instance().m_gpuAvailable; }
// 全工程除 settings 面板外没有任何 setProvider()/probeOpenGL() 调用
```

- **现象 / 复现路径**：设置 → GPU 设置 → 选"强制 GPU"→ 应用 → 状态栏仍显示 `FORCE GPU | cpu`（ForceGpu 分支 `m_gpu->isAvailable()?gpu:cpu` 永远走 cpu）；点"探测 OpenGL"显示"可用"，但渲染仍全 CPU；重启后模式回到 Auto。
- **根因**：能力探测从未在启动时执行，`BackendCapabilities` 恒 false；Probe 结果没有写回单例；`RenderMode` 无 QSettings 持久化（settingsgpupanel.h:21-24 注释声称"main app 已自行保存"，实际没有）；`modeApplied` 信号无接收者，状态栏只在切 tab 时刷新。
- **修复建议**：启动时在后台 `QOpenGLContext` probe 一次并 `setProvider/ setForced`；Probe 按钮成功后调用 `BackendCapabilities::instance().setForced(true)`；`BackendRegistry::setMode` 内写 QSettings 并在启动时恢复；状态栏连 `SettingsGpuPanel::modeApplied`（需要让 MainWindow 能拿到面板，或把 setMode 抽成 app 级服务）；GPU 不可用时禁用 ForceGpu 单选并加 tooltip。
- **关联**：media/gpu（08-render 可能也报 GPU 后端占位）；UI 与渲染的联动。

### UI-12 "打开文件"路径无扩展名路由 → 音视频/二进制当文本读 (P1)

- **位置**：`src/app/mainwindow.cpp:3161-3185`、`src/media/imagewindow.cpp:848-855`、`src/ui/docwindow.cpp:55-72`
- **证据**：

```cpp
// mainwindow.cpp:3161-3184
QString err;
auto *img = new ImageWindow;
if (img->loadFile(path, &err)) { ... return; }
delete img;
auto *doc = new DocWindow;
if (!doc->loadFile(path, &err)) { QMessageBox::warning(this, tr("打开失败"), err); ... }
```

```cpp
// imagewindow.cpp:851-855
cv::Mat img = cv::imread(path.toLocal8Bit().toStdString(), cv::IMREAD_UNCHANGED);
if (img.empty()) { *err = ...; return false; }
```

- **现象 / 复现路径**：信息树里双击 `.mp3/.wav/.mp4`（`FileExtensionRegistry` 明确列了这些扩展名并显示在树里）→ ImageWindow 失败 → DocWindow 用 `QTextStream::readAll()` 读出二进制并显示乱码；双击一个 4GB 视频文件可能直接卡死/OOM。文件树与"打开文件"对话框都把这些扩展名当作"支持"。
- **根因**：`onHomeOpenPathRequested` 里注释写明"不再分流：优先 ImageWindow.loadFile，失败回退 DocWindow"；`FileExtensionRegistry` 已提供 `moduleIdForExtension` 与 4 个模块，但打开路径完全没用；DocWindow 无大小/二进制检测。
- **修复建议**：打开前用 `moduleIdForExtension(path)` 路由：imageWorker/visionWorker → ImageWindow；audio/video/vision 未实现 → 明确提示"该模块尚未实现"并拒绝；无扩展名/文本 → 读取前检查 `QFileInfo::size() < 上限` 且前 8KB 无 NUL 字节；另外信息树对未实现模块的文件加灰色/禁用态。
- **关联**：core/FileExtensionRegistry、InfoTreeDock 的过滤、HomePage 的模块按钮（UI-16）。

### UI-13 DocWindow 编码与保存：非 UTF-8 文件被永久改写 (P1)

- **位置**：`src/ui/docwindow.cpp:55-72`（loadFile）、`82-109`（saveAsFile）
- **证据**：

```cpp
QTextStream in(&f);
in.setEncoding(QStringConverter::Utf8);   // 无 BOM/GBK 探测
ui->editor->setPlainText(in.readAll());
```

```cpp
QFile f(target);
if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) ...
QTextStream out(&f);
out.setEncoding(QStringConverter::Utf8);
out << ui->editor->toPlainText();
```

- **现象 / 复现路径**：用"新建文本"之外的方式打开一个 GBK/ANSI 编码的中文 txt（Windows 上很常见）→ 打开已乱码；随便改一个字保存 → 原文件被 UTF-8 + U+FFFD 覆盖，原始字节永久丢失。保存过程中断电/崩溃 → `Truncate` 后原文件内容已丢。
- **根因**：读写两端都硬编码 UTF-8，没有 BOM 嗅探/严格解码回退，也没有"保持原编码"策略；保存直接覆写目标文件而非 tmp+rename；`QIODevice::Text` 还会做 CRLF 转换（混合行尾文件被规范化）。
- **修复建议**：`loadFile` 读入原始 QByteArray：BOM(UTF-8/UTF-16) → 否则 `QStringDecoder(Utf8, Flag::ConvertInvalidToNull)` 严格试解，失败回退 GB18030；记录 `m_encoding` 与 `m_lineEnding`，保存时按原编码/行尾写回；用 `QSaveFile`（具备原子替换与失败回滚）+ `commit()`。
- **关联**：SessionManager 的恢复路径同样走 DocWindow::loadFile；跨模块（core 可提供编码探测工具）。

### UI-14 最近文件列表交互与模型刷新问题 (P2，但直接影响主页手感)

- **位置**：`src/ui/homepage.cpp:149-161, 244-255`；`src/models/recentlistmodel.cpp:20-25`
- **证据**：

```cpp
connect(ui->tableView, &QTableView::doubleClicked, this, &HomePage::onRowDoubleClicked);
// ...
connect(ui->tableView, &QTableView::clicked, this, [this](const QModelIndex& proxyIdx){
    if (proxyIdx.column() >= 4) return;
    ... if (!path.isEmpty()) emit openPathRequested(path);
});
```

- **现象 / 复现路径**：单击一行立即打开文件并切到工作区（用户来不及看到选中态）；双击时 Qt 同一行会先发 `clicked`，再发 `doubleClicked`（第二次 release 通常还会再发一次 `clicked`），因此 `openPathRequested` 会被触发多次（`onHomeOpenPathRequested` 的"已打开则切 tab"去重救了重复 tab，但日志/状态栏/最近时间被重复刷新）。此外每次打开文件都会 `touchOpen → changed → beginResetModel/endResetModel`，列表选择与滚动位置全部丢失（在 `clicked` 处理期间发生 model reset，也属于危险的重入模式）。
- **根因**：把"选择"和"打开"合并到单击；model 用全量 reset 响应任一数据变化（pin/删除/打开/保存都触发）。
- **修复建议**：只连 `doubleClicked`（或单击选中 + 双击打开）；`RecentListModel` 改为 `dataChanged`/`beginInsertRows` 增量更新，或至少在 reset 前保存并恢复 `currentIndex`/`scrollValue`；在 `onHomeOpenPathRequested` 里用 `QTimer::singleShot(0, ...)` 避免在 view 事件处理中 reset model。
- **关联**：RecentManager（UI-15）；models 层审查员可能单独报。

### UI-15 RecentManager::touchOpen 丢失 pinned 状态 (P1)

- **位置**：`src/core/recentmanager.cpp:60-93`
- **证据**：

```cpp
    // 1) 先删除旧条目
    m_entries.erase(std::remove_if(...), m_entries.end());
    // 2) 构造新条目 (pinned 标志: ... 新条目默认 false)
    ...
    e.pinned       = false;
```

- **现象 / 复现路径**：在主页把某个最近文件点"固定"（行变绿、置顶）→ 双击打开该文件 → 列表刷新后它掉出固定段、回到普通段。pin 计数与排序全部失真。
- **根因**：`touchOpen` 先 erase 旧 entry 再新建，没有把旧 `pinned`（以及首次编辑时间等）带过来；注释里也承认"简单做法"。
- **修复建议**：在 erase 前 `findIndex` 保存旧 `pinned` 与 `lastEditTime`；或者改为"更新现有条目字段 + 重排"而不是删除重建。补 `togglePin → touchOpen → 仍 pinned` 的单测。
- **关联**：HomePage 的 pin/delete delegate（UI-14/UI-24），RecentListModel 排序。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 启动主页 | PS 2020+ 有 Home 屏：新建/打开/最近（Recent 可固定/删除/搜索） | 有 HomePage：新建文本/打开/打开文件夹/最近表/4 模块按钮；无搜索、无按模块过滤、模块按钮空壳 | 部分 | P2 |
| 最近文件交互 | 单击选中、双击打开、右键清除/固定 | 单击即打开；双击三重请求；pin 会被 touchOpen 清掉；删除不可删除固定项 | 有但错 | P1 |
| 主页模块入口 | PS 无音频/视频/视觉模块入口 | 4 个按钮仅写日志（mainwindow.cpp:2465） | 缺失 | P1 |
| 文件浏览器 | PS 无资源管理器（靠 Bridge/Finder）；可拖文件到画布置入 | 左侧 InfoTree：模型/视图自研、懒加载、扩展名校验、双击/回车打开；无刷新/右键/拖放打开/文件图标缓存 | 部分 | P2 |
| 文件树大目录性能 | 不适用（PS 无） | 同步双全扫 + 每项 stat + 一次性 insert；万级目录冻结 | 有但错 | P1 |
| 打开任意文件 | PS 由 Camera Raw/格式插件接管，失败给明确提示 | ImageWindow→DocWindow 无脑 fallback；音视频/二进制当文本打开乱码 | 有但错 | P1 |
| 首选项/设置 | PS 有 常规/界面/工作区/工具/性能/暂存盘/文件处理/单位与标尺/参考线/插件/增效工具 等，全部生效并持久化 | 设置窗 4+N 页：全局（仅最近文件上限生效）、主题（假）、语言（假）、日志（仅运行时生效、不持久化）、GPU（不闭环） | 部分 | P1 |
| 主题/外观 | PS 有 UI 亮度 4 档 + 强调色，需重启或即时生效 | 有 ThemeManager（Light/Dark + QSS + QSettings）；设置页未接；主题画廊是骨架；多处硬编码颜色破坏暗色 | 有但错 | P2 |
| 界面语言 | PS 多语言，切换需重启 | 语言页可切但译文为空、retranslate 缺失、双 translator | 有但错 | P1 |
| 面板 dock 拖拽 | PS 面板可拖出浮动、跨组合并、嵌套、保存工作区 | 自研 VDraggableTabWidget：拖拽命中坐标错误→跨组拖放/拖出浮动全部不可用；浮动 wrapper 无窗口标志；无布局持久化 | 缺失 | P1 |
| 面板 tab 右键菜单 | PS：关闭/关闭组/浮动/浮动全部/停靠到… | 无右键菜单；`tabCloseRequested` 无 tabsClosable 按钮，`pageCloseRequested` 无消费方 | 缺失 | P2 |
| 工作区布局持久化 | PS 保存/复位工作区；启动恢复上次布局 | MainWindow 无 saveState；"视图→重置布局"读一个从未写过的 `workspace_default.ini`（mainwindow.cpp:2263-2280） | 缺失 | P2 |
| 登录/账号 | PS 账号云同步（Adobe ID） | LoginDialog 纯骨架，仅状态栏提示；UserManager/AuthClient 已存在但未接 | 缺失 | P2 |
| 文本文档 | PS 无（靠置入图层文字） | DocWindow 纯文本：UTF-8 only、非原子保存、Ctrl+Z 被菜单截走；与"仿 PS"定位割裂 | 有但错 | P2 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 大目录/网络盘文件树（异步+增量+取消） | QT-NATIVE | `QFileSystemModel` 内置 `QFileInfoGatherer` 后台线程、`fetchMore` 增量、`QFileIconProvider` 缓存与 `QFileSystemWatcher`；自研 model 全部重写了且更差 | 换 `QFileSystemModel + QSortFilterProxyModel`（过滤扩展名）；若保留自研，用 `QtConcurrent` + 分批 `beginInsertRows` |
| 文件系统变更自动刷新 | QT-NATIVE | `QFileSystemModel` 自带 watcher；`QFileSystemWatcher` 也可直接用于自研 model | 用现成能力 + debounce；补充"手动刷新"兜底 |
| 递归监听网络盘/UNC/超大目录 | QT-LIMIT | Windows 后端是 `ReadDirectoryChangesW`，递归树/网络盘事件不保证、句柄与成本不可控，Qt 无法做到可靠全量监听 | 只监听当前展开的前缀目录 + 定时轮询/手动刷新；在 UI 上明确"外部修改可能需手动刷新" |
| 面板拖拽（浮动/合并/嵌套/tab 重排） | QT-NATIVE | `QMainWindow + QDockWidget` 原生支持 dock/undock/floating/tabify、`saveState/restoreState`；`QTabBar::setMovable` 原生重排。自研系统目前 0% 可用 | 优先回归 QDockWidget 方案；若必须自研，drop 侧用 `QAbstractItemView` 的 drop indicator 语义、坐标统一由 `mapToGlobal` 处理 |
| dock 布局持久化 | QT-NATIVE | `QMainWindow::saveState()/restoreState()` + `saveGeometry()`；当前完全未用（重置布局读不存在的文件） | 关窗保存、启动恢复；"重置布局"用代码内默认 state 而非外部 ini |
| 运行时切换语言 | QT-NATIVE | `QTranslator` + `QEvent::LanguageChange`；uic 生成的 `retranslateUi()` 可直接复用；DIY 的 `retranslateUiTexts` 只覆盖 2 处 | 每窗实现 changeEvent→retranslateUi+自建控件 setText；一次 `installTranslator` |
| 文本编码探测/保持 | QT-NATIVE | `QStringConverter/QStringDecoder`、BOM 探测、`QSaveFile` 原子写；Qt 6 已移除 QTextCodec 但 QStringDecoder 够用 | loadFile 探测 + 记录编码；QSaveFile 提交；必要时引 ICU（不需要） |
| 文件拖放打开 | QT-NATIVE | `setAcceptDrops/dragEnterEvent/dropEvent` + `QMimeData::urls()`；当前全工程只有面板自研拖拽，无任何"拖文件进窗口"实现 | MainWindow 接受 urls → `onHomeOpenPathRequested`；HomePage/InfoTree 也加 |
| emoji 当图标（主页/工具栏） | QT-NATIVE | 项目已有 `src/media/tools/icons/*.svg` 与 `QIcon::fromTheme`/QStyle 标准图标；emoji 依赖字体且不可主题化 | 用 SVG/标准图标替代 🏠📄📂🎵🎬👁✂✖ |
| 主题 token 化 | QT-NATIVE | ThemeManager 已有 Palette + QSS 占位符；问题只是散落的 `setStyleSheet("#4caf80")` 与自定义 delegate 写死颜色 | 全部改从 `palette()` 取色；delegate 用 `QStyleOptionViewItem::palette` |
| 完整 PS 式面板系统（动画/跨窗口合并/嵌套/多屏） | QT-EFFORT | 若坚持自研，还需重写命中测试/坐标模型/放置策略/持久化/关闭语义，估 5–8 人天；QMainWindow 原生方案可覆盖 ~80% | 用 QDockWidget/QTabWidget 原生方案，省下这部分工作量 |
| 翻译 839 条 × 2 语言 | QT-EFFORT | CMake 的 lupdate/lrelease 流程已通（`qt_add_translations` + `/translatefile` 资源），缺的是内容；AI 辅助约 1–2 人天/语言 | 补 en/ja 译文后开放语言切换 |
| 缩略图/大图预览文件树 | QT-EFFORT | 需自研缩略图生成（`QImageReader` scaled）+ LRU 缓存 + 后台池；`QFileIconProvider` 只能拿系统小图标 | 2–3 人天；或先不做，双击打开看大图 |

## 5. 设计问题

1. **分层被破坏（ui → media）**：`SettingsGpuPanel` 直接 include `media/gpu/BackendRegistry.h`、`media/tools/ToolContext.h`，`src/ui/dock/VPages.h` 直接 include 9 个 media dock 头文件；`src/ui/CMakeLists.txt` 却没声明 `vistella_media` 依赖（靠 exe 的 link 顺序兜底）。后果：`vistella_ui` 不再是"只放 QWidget 子类"的独立库，新增 exe/测试一旦用到 GPU 页就链接失败。建议在 core 抽 `IRenderStatus`/`IPanelPageFactory` 接口，或把 GPU 设置页移入 app。
2. **自研 dock 的双数据源**：`m_pages`（含 floating）与 QTabBar（仅 docked）没有单一真源，所有 tab 索引 API 都假设两者同序（UI-04）。后果是"浮一个页后整组行为错乱"，且现有测试只验计数、无法发现。建议以 `QTabBar::setTabData` 存 page 指针作为唯一映射，PageState 只保存浮动/标题等附加信息。
3. **所有权网络缺少弱引用**：`RightPanelDock` 的 9 个 `V*Page*`、`ImageWindow::m_adjustmentPanel`、`AdjustmentPanel::m_host` 全是裸指针，而页可以被拖到别的容器/窗口（UI-01）。任何"页面搬家"都会让这些缓存悬垂。建议统一 `QPointer` + `pageAdded/pageRemoved/pageMoved` 订阅，并把"页面归属哪个文档"变成显式契约。
4. **大量阶段 0 占位混在生产路径且注释失真**：`VRenderPath`/`VCompositeStrategy`/`setCompositeStrategy`/`preferredRenderPath` 被注释成"用户拍板/1 行接入"，实际全无消费（UI-35）；`ThemeGalleryDialog`/`LoginDialog` 自述骨架但菜单入口照常开放；GPU 面板注释称"main app 已持久化"实际没有（UI-11）。后果：后续维护者无法区分"已完成"与"占位"，审计/排期失真。建议用 `TODO(stage-x)` 统一标记 + 入口禁用态 + 文档标注完成度。
5. **i18n 基础设施可用但没闭环**：CMake/资源/LanguageManager 全就绪，缺译文、缺 retranslate、双 translator、运行时字符串用 `QStringLiteral`（GPU 页/面板标题）。这是"Qt 原生能力没用对"的典型，不是能力边界。
6. **主题化只覆盖全局 QSS**：局部 `setStyleSheet` 硬编码颜色（InfoTreeDock #4caf80/#1a1a1a、HomePage delegate 浅绿、SettingsGpuPanel #333/#555、ThemeGallery 预览、Vistella.qss 第 419 行 tab 选中 `#ffffff`、第 405-412 行 splitter 句柄 `#d0d0d0`）。暗色主题下多处不可读，且切主题时局部样式不重刷。建议颜色全部来自 `ThemeManager::palette()`，局部样式改为 objectName 规则集中进 Vistella.qss。
7. **UI 层承担业务**：HomePage 直接调 `RecentManager`（pin/delete），DocWindow 直接调 `RecentManager::touchEdit`，设置页直接改 Logger 单例。后果：无法注入/mock，主页的 pin/delete 交互无法单测（现有测试只测 RecentManager）。建议 `RecentListController` 或 model 层暴露 `togglePinRequested`。
8. **模型刷新策略过粗**：`RecentListModel::refresh()` 全量 `beginResetModel`；`FileTreeModel` 无刷新。前者导致每次打开/固定/删除都丢选择与滚动，后者导致数据陈旧。建议用 `dataChanged/insertRows` 增量更新，文件树用 watcher + 局部重扫。
9. **错误处理靠 QMessageBox 兜底**：InfoTree 打开失败、文件树扫描失败、设置应用失败全部只 `QMessageBox`/静默；无统一 Result/日志关联。建议文件树 model 暴露 error role，设置页显示"应用失败原因"，并在 Logger 打点（目前只有 Drop 的 -1 完全无日志）。
10. **.ui 硬编码布局**：固定 minimumSize（首页卡片、设置 nav 120、输入框 180/240、对话框 400×540/520×420），无 `sizePolicy` 弹性；英文/日文更长时必截断。建议布局全 stretch + `minimumWidth` 仅在必要时用。

## 6. 修复建议（按优先级排序）

**P0 立即修（0.5–1 人天）**
- 跨窗口 page 移动加同窗口约束（UI-01，0.5 人天；若已能拖通，这是唯一会崩的入口）。
- 拖拽期间禁止关闭文档/窗口（UI-03，0.5 人天），配合 QPointer 自保。

**P1 本轮修（约 6–9 人天）**
- 修 drop 坐标（UI-02，0.5 人天，加 1 个坐标回归测试）。
- 修双索引映射（UI-04，1 人天）+ 浮动 wrapper 正确 floating（UI-05，0.5 人天）+ detach 所有权（UI-06，0.5 人天）。若决定放弃自研拖拽改 QDockWidget，可整体省 2–3 人天。
- 文件树：换 `QFileSystemModel`（推荐，1.5 人天）或异步分批改造（2–3 人天），同时解决 UI-07/08/17/19/20/21/22。
- 打开路径按扩展名路由 + 文本页护栏（UI-12，0.5 人天）；DocWindow 编码/原子保存（UI-13，1 人天）。
- 设置页：主题/自动保存接线（UI-09，1 人天）；日志设置持久化（UI-26，0.5 人天）；GPU 闭环（UI-11，1 人天）。
- i18n：删双 translator + 全局 retranslate（UI-10，1 人天），译文补齐另算（2–4 人天）。
- RecentManager pinned 保留（UI-15，0.5 人天）；主页单击/双击与增量刷新（UI-14，0.5 人天）。

**P2 后续（约 3–5 人天）**
- 设置/主题画廊/登录补齐或入口禁用（UI-28/29，1–2 人天）。
- 信息树刷新/右键/视图菜单入口/拖放打开（UI-17/18/22，1–1.5 人天）。
- 主题 token 化清理硬编码色（UI-23/24/25/27，1 人天）；.ui 尺寸弹性（UI-33，0.5 人天）。
- DocWindow Ctrl+Z 路由、dirty 抖动、打开文件夹后切工作区（UI-30/31/32，0.5 人天）。

**架构级重构（约 8–12 人天，建议排期）**
- 放弃/收敛自研 VDraggableTabWidget：优先 QDockWidget+QTabBar 原生，保留 `V*Page` 适配层；若坚持自研，按钮式补齐主机事件、坐标模型、持久化、page 生命周期协议（5–8 人天）。
- 抽 SettingsStore + IRenderStatus + PanelPageHost 三个接口，恢复 ui→core→media 的依赖方向。

## 7. 需要 Lead 协调的跨模块问题

1. **MainWindow（01-app-shell）**：视图菜单缺"信息树"入口（与 mainwindow.cpp:466 注释不符）；`onUndo/onRedo` 未处理 DocWindow（UI-30）；`onHomeModuleRequested` 空壳（UI-16）；`onHomeOpenPathRequested` 的 fallback 路由（UI-12）；"重置布局"读从未保存的 `workspace_default.ini`（§3）。
2. **ImageWindow / RightPanelDock（09-panels）**：page 裸指针与 `AdjustmentPanel::m_host`（UI-01）需要 panels 侧配合改 `QPointer`/`pageMoved` 订阅；`Deprecated accessor 返回 nullptr` 的调用方是否还活着需 panels 审查员确认。
3. **core（03-core-infra）**：`RecentManager::touchOpen` 丢 pinned（UI-15，core 侧 bug）；`Logger` 无 QSettings 持久化（UI-26）；`LanguageManager::setLanguage` 无返回值/无 retranslate 钩子（UI-10）；`FileExtensionRegistry` 缺 .txt/.md（UI-19）需 core 决策。
4. **models**：`RecentListModel::refresh` 全量 reset（UI-14）建议 models 审查员统一改增量通知。
5. **app/styles（视觉）**：`Vistella.qss:419` 选中 tab 写死 `#ffffff`、`:405-412` splitter 写死 `#d0d0d0`，暗色主题下 VDraggableTabWidget/QSplitter 不可读，需要 app/styles 归属的审查员一起收敛。
6. **CMake/构建**：`vistella_ui` 依赖 media 的 GPU 符号但 CMake 未声明（UI-38）；若要拆分 GPU 面板到 app，需要 Lead 统一排期。
7. **翻译内容**：`translatefile/vistella_en_US.ts` / `vistella_ja_JP.ts` 各 839 条全空，属产品级内容工作，需 Lead 决定是否本轮补齐（否则语言页建议先隐藏/禁用）。
8. **测试缺口**：`tst_VDraggableTabWidget` 只覆盖 programmatic API 与计数，未覆盖 drop 坐标/浮动窗口/双索引（UI-02/04/05）；`tst_FileTreeModel` 未覆盖空目录 hasChildren/权限失败/大目录。建议 Lead 在测试负责人处补 3–4 个回归用例。

---

### 附：本次审查覆盖的文件与结论摘要

- `src/ui`：docwindow*（编码/原子保存/Ctrl+Z 路由）、FileTreeItem/Model/NodeData（懒加载正确但 hasChildren 恒真、同步全扫、索引无保护）、homepage*（单击即开、模块空壳、QSS 选择器失效）、infotreedock*（无 watcher/无刷新入口、硬编码色、扩展名过滤不含文本）、logindialog*（纯骨架）、settingsdialog*（主题/自动保存假控件、语言假生效、日志不持久化）、settingsgpupanel*（GPU 不闭环、硬编码 i18n/配色）、themegallerydialog*（纯骨架）、CMakeLists（ui→media 反向依赖）。
- `src/ui/dock`：VDraggableTabWidget（drop 坐标错误、双索引、浮动 wrapper 非窗口、drag exec 生命周期、QDrag 泄漏）为主缺陷源；VDragManager/VDropIndicator 大量死代码；VPages/VTabPageBase（英文标题、RenderPath/CompositeStrategy 占位 API 全无消费）；VRenderPath/VCompositeStrategy 仅声明。
- **模块健康度**：4 / 10 —— 骨架与分层设计思路清晰（Model/View、Page 适配层、ThemeManager/LanguageManager/RecentManager 单例化），但自研 dock 的拖拽主功能实际不可用、设置页过半控件是装饰、i18n 完全未闭环、文件树同步扫描与打开路径的 fallback 会造成冻结/乱码，且存在跨窗口 page 移动的悬垂指针隐患。修复面集中在 5 个文件（VDraggableTabWidget.cpp、FileTreeModel.cpp、settingsdialog.cpp、homepage.cpp、docwindow.cpp）。
