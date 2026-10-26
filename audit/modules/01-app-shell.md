# 01 · app 外壳层（入口 / MainWindow / 主题 QSS / 启动屏） — 审计报告

> 范围：`src/app/**`（`main.cpp` 317 行、`mainwindow.cpp` 3518 行、`mainwindow.h` 405 行、`mainwindow.ui` 57 行、`qml/splash_screen.qml` 286 行、`styles/Vistella.qss` 434 行、`CMakeLists.txt` 42 行）；共 7 文件 / 约 5,059 行
> 审查者：audit-app

## 1. 模块概览

`src/app` 是整个程序的装配层与外壳。`main.cpp` 是唯一入口：设置 DPI/字体/Fusion、初始化 `Logger`、`LanguageManager`、`ThemeManager::globalStyleSheet()`、`UserManager`、`AIManager`、`MediaDispatcher`（只注册 `ImageWorker`）、`EngineContext` 线程池，创建 QML `QQuickView` 启动屏，3 秒后构造 `MainWindow` 并按 `SessionManager` 的花名册决定恢复文件还是回主页。`MainWindow`（QMainWindow）承担 8 类职责：无边框自绘标题栏 + `WindowMode` 状态机、PS 式 12 个顶级菜单与全部 `QAction`、`QTabWidget` 文档管理（pin/close/dirty/右键/session）、HomePage↔Workspace 的 `QStackedWidget` 切换、左侧 4 个 dock 对 active `ImageWindow` 的 attach/detach、全局 QSS 主题应用、状态栏 GPU 按钮、以及直接操作 `ImageWindow`/`cv::Mat`/`LayerStack` 的一批业务命令。它向下调用 `src/core`（Session/Theme/Language/Recent）、`src/ui`（HomePage/DocWindow/InfoTreeDock/各类 Dialog）、`src/media`（ImageWindow/Filter/Layer/Mask/Selection/mediators），是典型"上帝对象"。

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| APP-01 | P0 | mainwindow.cpp:264-294 | data-loss | 退出程序时图片文档有改动→点"保存"→什么都没保存照样退出 | `closeEvent` 只实现了 `DocWindow` 的保存分支，`ImageWindow` 分支缺失 | 补 `ImageWindow` 的 save/saveAs 分支，或统一走 `onTabCloseRequested` 的保存逻辑 |
| APP-02 | P0 | mainwindow.cpp:1872-1884 / 3371-3418 | crash | 打开≥2 张图，Ctrl+W 关掉当前图，再点左侧工具按钮 → UAF 崩溃 | 关闭时 `blockSignals(true)` 抑制了 `currentChanged`，`bindLeftDockWidgets()` 未被调用，dock 内 `m_toolMed`/`m_ctx` 成为悬垂裸指针 | `removeTab` 之后显式调用 `onTabChanged(currentIndex())`（或 `bindLeftDockWidgets(activeImageWindow())`） |
| APP-03 | P1 | main.cpp:178, 221 | state-sync | 正常退出后下次启动仍弹"上次程序异常退出"；选"否"会清空上次会话 | 启动时先 `setLastExitClean(false)` 再 `isLastExitClean()`，把要读的信息提前覆盖成 false | 先读取到局部变量，再写 false |
| APP-04 | P1 | main.cpp:213-217, 260-264 | logic | 崩溃恢复选"是"后主窗口永不显示，进程只剩对话框消失后的空跑 | `MainWindow w; w.hide();` 之后只有 clean 分支用 `applyWindowMode()` 重新 show，restore 分支没有 show | restore 分支末尾补 `w.applyWindowMode()`（或干脆不在启动期 hide） |
| APP-05 | P1 | mainwindow.cpp:3388-3399 | state-sync | 反复切 tab 后一次点击触发 N 次切换工具、一次设置被重复应用；旧文档 mediator 仍连在 dock 上 | `bindLeftDockWidgets()` 直接 `attach()` 而不先 `detach()`，`attach()` 内部无条件 `connect` | `attach()` 首行先 `detach()`；或全程 `Qt::UniqueConnection` + `QPointer` |
| APP-06 | P1 | mainwindow.cpp:386/988、650/709 | logic | Ctrl+A 与 Ctrl+Alt+C 完全不生效（Qt 打印 Ambiguous shortcut） | 两个 QAction 注册了同一快捷键且都挂在窗口上 | 同一功能只保留一个 action，或给不同 `shortcutContext`/不同键 |
| APP-07 | P1 | mainwindow.cpp:340-1195 | state-sync | 主页/无图/空撤销栈时"撤消""保存""图层→…""滤镜→…"全部可点，点了只 showMessage 或静默 | 构建完 action 后没有任何 `updateActionStates()`，从未按 active doc/`canUndo` 改 enable | 增加集中式 `updateActionStates()`，接 `currentChanged`/`QUndoStack::canUndoChanged`/`dirtyChanged` |
| APP-08 | P1 | mainwindow.cpp:2126-2129 | logic | 图像文档里 Ctrl+X/C/V 无效（PS 核心操作全废） | `onCut/onCopy/onPaste` 只 `qobject_cast<DocWindow*>`，对 `ImageWindow` 直接落空 | 增加 `ImageWindow` 分支（选区/图层复制粘贴），或按 active doc 分派 |
| APP-09 | P1 | mainwindow.cpp:437,450,476,533,545,554,598,616,771,926,986,1053 | ui-visual | 顶级菜单顺序为 文件/编辑/视图/设置/增效工具/窗口/**帮助**/图像/图层/文字/选择/滤镜（12 个） | 后半段菜单在 `buildActions()` 末尾追加，忘了"帮助"应排最后、图像→滤镜应紧跟编辑 | 按 PS 顺序重建：文件/编辑/图像/图层/文字/选择/滤镜/视图/窗口/帮助 |
| APP-10 | P1 | Vistella.qss:413-424 | ui-visual | 暗色主题下选中的 tab 白底 + 近白字（不可读），主 tab 与 dock tab 全中招 | 文件末尾追加的 `QTabBar::tab:selected { background:#ffffff }` 覆盖了前面主题化规则，而 `color` 仍取 `{{TAB_TEXT_SEL}}`(暗色=#fff) | 删除 413-424 重复段，或改为 `background: {{TAB_SELECTED}}` |
| APP-11 | P1 | mainwindow.cpp:2011-2016, 2464 | ux | 主页点"打开"→在文件框点取消→界面却切到空工作区，且没有任何"回主页"入口 | `onHomeOpenRequested()` 无条件 `switchToWorkspace()`，`onOpenFile()` 提前 return 不告知调用方 | 让 `onOpenFile()` 返回 bool/路径，仅成功后切页；并在菜单补"返回主页" |
| APP-12 | P1 | mainwindow.cpp:229-244, 1359-1401 | state-sync | 系统级最大化/还原（Win+↑、任务栏菜单、Win+Shift+←）后点"□"按钮第一次无反应、退出后模式记忆错 | `m_mode` 只在 `setMode()` 里改；`changeEvent` 只同步 `m_isMaximized`，不同步 `m_mode`，两套状态并存 | 以 `windowState()` 为唯一真源：`changeEvent` 里 `m_mode = isMaximized()?Maximized:Normal`，或删掉 `m_mode` |
| APP-13 | P1 | mainwindow.cpp:122, 3279-3347 | ux | 窗口无法用鼠标拖边改变尺寸；无系统阴影/圆角/右键标题栏菜单/Win11 贴靠布局；只能最大化和"1280×800"两档 | `Qt::FramelessWindowHint` 去掉原生 `WS_THICKFRAME`，自绘代码只实现了拖动和双击，没有命中测试/边缘 resize | `nativeEvent` 处理 `WM_NCHITTEST` 返回 `HTLEFT…`，或对边缘改用 `QWindow::startSystemResize(Qt::Edges)` |
| APP-14 | P1 | mainwindow.cpp:328-331 | ui-visual | 切换亮/暗主题后大量面板仍是浅色/黑字，界面"半黑半白" | `applyTheme()` 只 `qApp->setStyleSheet()`；全项目 80+ 处 widget 级 `setStyleSheet`（`color: gray/#666/#333`、`#4A90E2` 边框）不会随主题刷新 | 删除 inline 样式，统一走 `ThemeManager::palette()`；或每次 themeChanged 广播后让各面板自行 `applyTheme()` |
| APP-15 | P1 | imagewindow.cpp:283-291, 2455-2477 | design | 每个 tab 底部各有一条自己的状态栏（叠在主状态栏上）；用过"屏幕模式"后每个 tab 顶部多一条空菜单栏；三档屏幕模式对窗口无效 | `ImageWindow` 是 QMainWindow 却被 `setWindowFlags(Qt::Widget)` 塞进 tab，`statusBar()/menuBar()` 仍会为它创建子控件，`showFullScreen()/showNormal()` 对非顶层 widget 无效 | `ImageWindow` 改继承 `QWidget`；右侧 dock/状态栏/屏幕模式统一由 `MainWindow` 承担 |
| APP-16 | P1 | mainwindow.cpp:3197-3214, 3224-3267 | data-loss | 崩溃后"恢复 5 个文件"只恢复磁盘版本，未保存的编辑全部无声丢失；pin 状态/tab 顺序也丢 | `saveSession()` 只存文件路径字符串，且只在 `closeEvent` 调用（崩溃时连路径都是上一次干净退出的） | 增加定时自动保存（`QTemporaryDir` + 原子写）+ 恢复时提示"已丢弃未保存修改" |
| APP-17 | P1 | mainwindow.cpp:1014-1044 | logic | 大图（>8.4MP）上"选择→羽化"结果错乱/出现块状 | 积分图用 `std::vector<int>`，12MP 图总和 12M×255≈3.06e9 超出 `INT_MAX`，累加 UB、`(d-b-c+a)` 溢出 | 换 `std::vector<qint64>`/`cv::integral(src,dst,CV_32S)`，并放到工作线程 + 进度 |
| APP-18 | P2 | mainwindow.cpp:3164-3192 | perf | 从主页打开 .mp4/.psd 等任意二进制文件会被当文本整块读进 `QPlainTextEdit`，大文件卡死 | `onHomeOpenPathRequested` 用"先试 ImageWindow、失败退 DocWindow"的启发式，没有走 `MediaDispatcher` 路由 | 用 `MediaDispatcher`/`FileExtensionRegistry` 选模块，未知类型拒绝并提示 |
| APP-19 | P2 | mainwindow.cpp:2263-2280 | logic | "视图→重置布局"永远失败 | 读取 `workspace_default.ini`，但全项目没有任何 `saveState()` 写这个文件 | 增加"保存当前布局"入口并 `saveState()` 写盘；无文件时用内置默认布局 |
| APP-20 | P2 | mainwindow.cpp:806-811 | logic | "合并可见图层"实际执行"向下合并" | action 文案与 `LayerOp::MergeDown` 不符 | 实现 `MergeVisible` 或把文案改成"向下合并" |
| APP-21 | P2 | mainwindow.cpp:2465-2469, 2426-2430 | ux | 主页 4 个"模块"按钮（音频/视频/视觉）点了只写日志；文件过滤器却列出音频/视频扩展名 | 功能未实装但 UI 已暴露；`MediaDispatcher` 只注册了 ImageWorker | 未实装模块按钮置灰 + 过滤器中移除未注册模块的扩展名 |
| APP-22 | P2 | Vistella.qss:25-48, 143-173, 184 | ui-visual | 大量 QSS 规则不生效（`#globalBar`、`QToolButton#themeBtn`、`QWidget#tabPin`、`QTabBar::close-button`）；`titlebar-close-icon: none` 值非法 | 控件从未创建或 objectName 不匹配；Qt 的 close button 已被自绘按钮替换 | 删除死规则；pin/close 按钮设 objectName 后走 QSS，别用 inline stylesheet |
| APP-23 | P2 | mainwindow.cpp:199-206, 239-242, 3351-3360 | i18n | 设置里切换语言后菜单、标题栏、按钮文字全不变，只有 tab 标题刷新 | `retranslateUiTexts()` 几乎空实现；`buildActions()` 里的 `tr()` 只在启动时执行一次；`MainWindow` 还重复装了一个 translator | 改成 action registry + `retranslate()`，或明确要求重启生效并提示 |
| APP-24 | P2 | mainwindow.cpp:305-311 vs 1410 | logic | 最大化状态永远不被记住（注释却写"下次冷启动会恢复 Maximized"） | `closeEvent` 把 `Maximized` 强制写成 `Normal`，`loadWindowState` 的 Maximized 分支永远走不到 | 按用户选择持久化，或明确产品决定并删除误导注释 |
| APP-25 | P2 | mainwindow.cpp:1438-1445, 3130-3133 | ux | 一旦用"打开文件夹"唤出信息树，就再也关不掉（无关闭按钮、无菜单项） | `setFeatures(features & ~DockWidgetClosable)` + 视图菜单里删掉了"信息树"入口 | 补"视图→信息树"可勾选 action（`toggleViewAction()`） |
| APP-26 | P2 | mainwindow.cpp:2098-2107 | logic | "关闭全部/关闭其他"中途取消保存后，剩余 tab 还开着但界面已切回主页 | 循环里 `onTabCloseRequested` 可被用户取消，循环后无条件 `switchToHomePage()` | 依据剩余 tab 数决定是否切页 |
| APP-27 | P2 | mainwindow.cpp:1456-1462, 1922-1947 | state-sync | 关闭 tab 后"窗口→文档"子菜单仍列出已关闭文档；点旧条目会切到错误 tab | 菜单只在 `currentChanged/tabCloseRequested` 重建，而关闭路径里 `currentChanged` 被 block；lambda 还捕获了位置索引 `i` | 改由 `tabInserted/tabRemoved`（自定义信号）驱动，lambda 捕获 widget 指针 |
| APP-28 | P2 | main.cpp:181-182, 217; qml/…:260-264 | build | 启动屏从 `applicationDirPath()/qml/…` 读盘（部署漏拷即黑屏跳过）；进度条 5s 但 3s 就关（60% 收尾） | QML 已用 `qt_add_resources` 打进 `:/qml/splash_screen.qml`，却没用 qrc 路径；定时器与动画时长两处硬编码 | `setSource(QUrl("qrc:/qml/splash_screen.qml"))`，用同一常量控制时长 |
| APP-29 | P2 | main.cpp:215-218; mainwindow.cpp:218 | ui-visual | 启动瞬间主窗口被 show 又被 hide（任务栏/窗口闪一下），随后才由 splash 回调重新显示 | `MainWindow` 构造函数里 `loadWindowState()→applyWindowMode()→showMaximized()/showNormal()` | 构造函数不 show；`showEvent`/启动回调里统一 `applyWindowMode()` |
| APP-30 | P2 | main.cpp:39-45 | crash | 未处理异常过滤器返回非法值，且对 C++ EH 返回 `CONTINUE_EXECUTION` 可能死循环；同时彻底放弃 crash dump | `EXCEPTION_UNWIND(6)` 不是 `SetUnhandledExceptionFilter` 合法返回值；注释对调试器 first-chance 机制理解有误 | 返回 `EXCEPTION_EXECUTE_HANDLER`，并在其中写 minidump/日志后退出 |
| APP-31 | P2 | main.cpp（全文） | design | 可同时启动多个实例，互相覆盖 QSettings/session、互相把对方的 lastExitClean 清零 | 没有任何单实例机制 | `QLocalServer/QLocalSocket`（Qt 原生）做单实例 + 激活已有窗口 |
| APP-32 | P2 | main.cpp:56-72 | perf | 每条 qDebug 同步写 `D:/vistella_qdebug.log` 并 flush；`D:` 不存在时静默失败；与 spdlog 双日志并存 | 调试期临时 hack 进了产品入口，未走 `vLogger` | 删除或改为编译期开关 + 走 `Logger` |
| APP-33 | P2 | mainwindow.cpp:1299-1301, 391 | ui-visual | 主题按钮 tooltip 写 "Ctrl+T"，实际快捷键是 Ctrl+Shift+T（Ctrl+T 是自由变换） | 快捷键改过，tooltip/注释没跟着改 | tooltip 用 `action->shortcut().toString()` 动态生成 |
| APP-34 | P3 | mainwindow.cpp:1606-1630, 3216-3222; mainwindow.h:218,227,364 | design | `pinButtonIndex()`/`onTabPinToggled()`/`restoreSession()`/`m_globalBar`/`buildGlobalBar()`/`buildMenuCorner()` 全是死代码；`if (-1 >= 0)`、`w == widgetAt(-1)`、`if (i == -1)` 多处 | 重构后未清理 | 删除 |
| APP-35 | P3 | mainwindow.cpp:1262-1263; main.cpp:82 | ui-visual | 标题栏版本号永远为空（显示 "Vistella "） | 只 `setApplicationName()`，从未 `setApplicationVersion()` | 启动时 `QApplication::setApplicationVersion(kAppVersion)` |
| APP-36 | P3 | qml:219 vs main.cpp:82 vs mainwindow.cpp:1255 | ui-visual | 品牌三套：启动屏 "MultiDoc/多模态文档工作台"、窗口 "Vistella"、logo 只有一个 "M" | 改名遗留 | 统一一个品牌常量 |
| APP-37 | P3 | mainwindow.cpp:1211; qss:305-324,344-354 | ui-visual | 标题栏固定 32px、按钮 40×32、QSS 用 `padding-top:9px` 手工居中，200% DPI 下会裁切/发糊 [待确认] | 固定 px + pt/px 混用 + PassThrough 分数缩放 | 高度由字号+`QFontMetrics` 计算，改用 logical px/`* devicePixelRatio` |
| APP-38 | P3 | src/app/CMakeLists.txt:41-42, 17-19 | build | `set(CMAKE_AUTOMOC/AUTOUIC)` 写在 `add_library` 之后（对目标无效）；app 目标 `PUBLIC` 透传 OpenCV include | 依赖 `qt_standard_project_setup()` 的父作用域恰好兜底；`mainwindow.h` 为了 QPointer 直接 include `imagewindow.h` | 把这两行移到文件顶部；`imagewindow.h` 只前置声明 + `.cpp` include |
| APP-39 | P3 | mainwindow.cpp:3441-3517 | state-sync | GPU/CPU 模式切了不写 QSettings，重启回 Auto；菜单勾选靠手写同步 | 未用 `QActionGroup` + 未持久化 | 用 `QActionGroup` + `QSettings` 持久化 |
| APP-40 | P3 | mainwindow.cpp:88, 1576, 1638 | design | `s_closing`/`s_themeConnOnce`/`s_inReorder` 三处函数级/文件级 static 全局状态 | 缺少实例成员/状态对象 | 收进 `MainWindow` 私有成员或独立 `TabManager` |
| APP-41 | P3 | mainwindow.cpp:2062-2065, 2082-2089 | ux | 另存为失败静默返回；"导出"对文本文档无任何反馈 | 分支未处理返回值 | 统一 `QMessageBox::warning` + 状态栏提示 |
| APP-42 | P3 | mainwindow.cpp:1763-1770 | crash | 若 `homePageContainer` 的 layout 不是 QVBoxLayout，走 else 分支时 `layout()` 可能为空 → 空指针解引用 | 防御性分支自身没防御 | 直接 `auto* l = new QVBoxLayout(...)` 或判空 |
| APP-43 | P3 | mainwindow.cpp:3301-3303 | ux | 拖动还原阈值写死 4px，没跟随系统 `startDragDistance` | 硬编码 | `QGuiApplication::styleHints()->startDragDistance()` |
| APP-44 | P3 | main.cpp:124-126, mainwindow.cpp:2265 | design | 存储路径三套并存：`AppLocalData`(logs/user.db) / `AppData`(workspace_default.ini, 漫游目录) / `QSettings`；分散且漫游目录没必要 | 路径字符串各处硬编码 | 抽 `AppPaths`，统一 `AppLocalDataLocation` |
| APP-45 | P2 | mainwindow.cpp:386 vs 988; 650 vs 709（同 APP-06） + 379,388,382 | logic | 用 `QKeySequence::Close/Replace/Redo` 标准键，Windows 上 Close 首绑定是 **Ctrl+F4**（Ctrl+W 只是备选，`QKeySequence(StandardKey)` 只取第一个），与注释"Ctrl+W"和 PS 习惯不符 | 误以为 `setShortcut(StandardKey)` 会把所有备选键都注册 | 用 `setShortcuts(QKeySequence::keyBindings(...))` 或直接写 `Ctrl+W`；参见 [Qt 文档 Standard Shortcuts](https://doc.qt.io/qt-6/qkeysequence.html#standard-shortcuts) |
| APP-46 | P2 | mainwindow.cpp:1436-1437 vs 161-166 | ui-visual | `m_infoDock`(300) 与 `m_leftDockContainer`(356) 争同一 LeftDockWidgetArea，`resizeDocks` 语义冲突，显示时挤压左侧面板 | 两个 dock 都加在左侧且分别 resize | 明确只保留一个左侧 dock，信息树改为 tabify 或浮动 |
| APP-47 | P2 | mainwindow.cpp:1792-1801 | ux | 切回工作区时若 tab 已全关，显示的是空白页面 | `switchToWorkspace` 只改 stack index，不校验 tab 数 | 无 tab 时不允许切换到 page 1 或自动回主页 |
| APP-48 | P3 | mainwindow.cpp:264-294 | ux | 退出时逐个文档弹窗，没有"全部保存/全部放弃" | 手写每文档循环 | 自定义 QMessageBox with "全部保存" |

### 2.1 八个重点问题逐项结论

1. **`setMode()` 唯一入口不变量**：**未守住**。代码自身确实只在 `setMode()` 里调 `showMaximized()/showNormal()`（`loadWindowState()` 里还手动改了 `m_mode`，见 mainwindow.cpp:1411，属于绕过）。但操作系统（Win+↑/任务栏菜单/贴靠/Win+Shift+←）可以绕过 Qt 直接改状态，`changeEvent` 只同步 `m_isMaximized` 不同步 `m_mode` → 双状态必然失联（APP-12）。此外 `mouseMoveEvent` 拖动还原确实走了 `setMode(Normal)`，这一点是正确的（3308）。
2. **菜单/action 体系**：12 个顶级菜单、顺序错乱（APP-09）；空壳项：图像>转换格式、设置>语言（同 onSettings）、增效工具管理器、窗口>堆叠/平铺、滤镜里 20+ 灰项、主题画廊/登录（只弹占位框）；无任何动态 enable 管理（APP-07）；快捷键冲突 2 组（APP-06）+ 标准键映射偏离 PS（APP-45）；`m_globalToolActions` 的 15 个快捷键与 `ToolId` 枚举基本一致（Move/RectSelect/Lasso/MagicWand/Crop/Text/Brush/Eyedropper/Shape/Pen/Clone/Heal 各 1 键，Patch/RedEye/MaskBrush 无键），但 `Transform=9` 无入口，且缺 PS 的 H(抓手)/Z(缩放) 工具；3D 菜单/ModelWindow/模式按钮已删干净（仅剩注释）。
3. **Tab 管理**：`m_tabStates` 用 **widget 指针**反查索引，插入/删除/重排后不会错位（设计正确）；真正的问题是关闭活动 tab 后 dock 不重绑（APP-02）、文档子菜单陈旧且捕获位置索引（APP-27）、pin 与顺序不持久化（APP-16）。
4. **Home↔Workspace 切换 / dock 绑定**：`bindLeftDockWidgets()` 重复 connect（APP-05）、关闭路径不重绑导致悬垂（APP-02）、取消打开却切页（APP-11）、空工作区无法回主页（APP-47）、信息树关不掉（APP-25）。
5. **QSS 主题**：占位符替换完整（`ThemeManager::resolveQss()` 里 25 处 `replace` 覆盖了 QSS 用到的全部占位符），但 QSS 结尾重复段 + 硬编码颜色破坏暗色主题（APP-10）；`qApp->setStyleSheet` 能自动 repolish 全局 QSS，问题在于 80+ 处 widget 级 inline stylesheet 和 C++ 硬编码颜色不会刷新（APP-14）；`QMainWindow, QWidget { background-color; color }` 是过宽选择器，全局污染（设计问题 5.3）。
6. **启动流程**：splash 3s/5s 不一致且读盘而非 qrc（APP-28）；崩溃标志写后读导致恢复弹窗永远误报（APP-03）；恢复路径窗口不显示（APP-04）；`try/catch` 包不住 Qt 事件循环里的异常（5.7）；无单实例（APP-31）；DPI 策略在 `QApplication` 之前设置正确；session 恢复语义不完整（APP-16）。
7. **ImageWindow 是 QMainWindow 却塞 tab**：导致每 tab 双状态栏/空菜单栏/屏幕模式失效（APP-15），并让 `MainWindow` 只能通过一堆 `savePublic()/onZoomIn()/currentImage()` 代理跨层调用（设计问题 5.1）。
8. **3244→3518 行职责膨胀**：可拆为 6 块（见 5.2）：TitleBar/WindowModeController、MenuActionRegistry、TabManager、DockBinder、SessionController、DocumentCommands（图像/图层/选区/滤镜命令应从 MainWindow 移到 media 层）。

### APP-01 退出时图片文档"保存"不保存，直接丢数据 (P0)

- **位置**：`src/app/mainwindow.cpp:252-313`（重点 264-294）
- **证据**：

```cpp
// mainwindow.cpp:278-293
if (qobject_cast<QPushButton *>(clicked) == btnSave) {
    if (auto *d = qobject_cast<DocWindow *>(w)) {
        QString err;
        if (d->filePath().isEmpty()) { ... d->saveAsFile(p, &err) ... }
        else if (!d->saveFile(&err)) { e->ignore(); return; }
    }
    // ← 没有 ImageWindow 分支
}
if(qobject_cast<QPushButton*>(clicked)==btnDiscard){
    // 空
}
```

- **现象 / 复现路径**：打开 PNG → 画笔涂几笔（tab 出现 `*`）→ 点窗口关闭 ✕ → 弹"文档 … 有未保存的修改，是否保存?" → 点"保存" → 程序直接退出，文件未变。若该图从未保存过（`filePath` 为空）同样没有任何另存为对话框。
- **根因**：`dirtyDocs` 收集同时包含 `DocWindow` 与 `ImageWindow`（259-261），但保存分支只处理了 `DocWindow`；`ImageWindow` 走完 if 后什么都不做，`closeEvent` 继续执行到 `SessionManager::setLastExitClean(true)` + `saveSession()` + `QMainWindow::closeEvent(e)`，把"已保存"当成事实写进会话和退出标志，下次启动也不会再提示恢复。
- **修复建议**：抽一个 `bool saveDocument(QWidget*, QString *err)` 统一处理两类文档（ImageWindow 用 `savePublic()/saveAsPublic()`，并用 `isDirty()` 复查是否真的保存成功），`closeEvent` 与 `onTabCloseRequested` 共用；失败或用户取消则 `e->ignore()`。顺便加"全部保存/全部放弃"。
- **关联**：`onTabCloseRequested` 的图片分支已经写对了（1848-1852），可直接复用。

### APP-02 关闭活动 tab 后左侧 dock 持有已销毁 ImageWindow 的裸指针 → UAF (P0)

- **位置**：`src/app/mainwindow.cpp:1816-1889`、`3371-3418`；`src/media/tools/LeftToolBar.cpp:212-247`
- **证据**：

```cpp
// mainwindow.cpp:1871-1884 —— 关闭时信号被屏蔽, 之后没有重新 bind
ui->tabWidget->blockSignals(true);
ui->tabWidget->removeTab(index);
ui->tabWidget->blockSignals(false);
...
w->setParent(nullptr);
w->deleteLater();
updateWindowTitle();          // ← 只更新标题, 没有 bindLeftDockWidgets
```

```cpp
// mainwindow.cpp:1803-1813 —— 只有 onTabChanged 才重绑
void MainWindow::onTabChanged(int index) { ...
    bindLeftDockWidgets(activeImageWindow());
```

```cpp
// LeftToolBar.cpp:227-233 —— 裸指针, 悬垂后直接解引用
connect(btn, &QToolButton::clicked, this, [this, id, btn](bool checked) {
    if (checked && m_toolMed) {
        m_toolMed->switchTool(id);   // m_toolMed 指向已 delete 的 ToolMediator
```

- **现象 / 复现路径**：打开 A.png、B.png（当前是 B）→ Ctrl+W 关闭 B（或点 tab 上的 ✕）→ 左侧工具列仍在（`m_leftDockContainer` 只在 tab 数为 0 时才隐藏）→ 点"画笔"→ 崩溃（或更糟：把命令发到已释放对象上）。
- **根因**：`QTabWidget::removeTab()` 内部会改 current 并 `emit currentChanged`，但这里用 `blockSignals(true)` 把它屏蔽了，因此 `onTabChanged()` 不会执行。`removeTab` 之后也没有任何显式重绑，于是常驻的 `LeftToolBar::m_toolMed/m_ctx`、`ModeSelectorBar::m_ctx`、`ImageOptionBar::m_ctx` 保留旧值；`ImageWindow` 的 `ToolMediator/ToolContext` 是它的 `unique_ptr` 成员（`imagewindow.cpp:467-473`），随 `deleteLater()` 一起销毁。Qt 只会自动断开"以旧对象为 sender"的连接，**不会**把接收方里的裸指针置空。
- **修复建议**：`removeTab` 之后（`blockSignals(false)` 之后）显式调用 `onTabChanged(ui->tabWidget->currentIndex())`；更彻底：`bindLeftDockWidgets` 内部改用 `QPointer<mediators::ToolMediator>`/`QPointer<ToolContext>`，并在 `OnTabChanged` 之外把 `QTabWidget::currentChanged` 的屏蔽改成"只屏蔽 `tabCloseRequested`"。同时 `~ImageWindow` 里应主动 `emit aboutToBeDestroyed()` 让常驻 dock detach。
- **关联**：同一根因导致 APP-27（文档子菜单陈旧）。

### APP-03 崩溃恢复标志"先写后读"，干净退出永远被当成异常退出 (P1)

- **位置**：`src/app/main.cpp:178`、`221-231`；`src/core/SessionManager.cpp:47-56`
- **证据**：

```cpp
// main.cpp:178
SessionManager::instance().setLastExitClean(false);   // 把"上次是否干净"覆盖成 false
...
// main.cpp:221
const bool clean = SessionManager::instance().isLastExitClean();  // 永远读到 false
if (clean) { ...直接显示... return; }
LOG_WARN("上次程序异常退出, 进入恢复流程");
```

- **现象 / 复现路径**：正常开 3 张图 → 正常关窗 → 下次启动弹"检测到上次未正常退出…是否恢复?"。用户点"否"→ 执行 `SessionManager::clear(); saveOpenFiles({}); saveActiveFile(...)`（266-271），**把上一次正常退出的会话记录清空**。
- **根因**：首次启动想把标志置为"运行中"，但读取上一次运行结果的代码在同一个函数里且在写之后；`SessionManager` 没有内存态缓存（每次都读 QSettings），所以本地也没有补救。
- **修复建议**：`const bool lastExitClean = SessionManager::instance().isLastExitClean();` 先读，再 `setLastExitClean(false)`；或者把"运行中"标记做成独立 key（`Session/running`），启动时读 `lastExitClean`、进程存活期写 `running=true`。
- **关联**：APP-04、APP-16 一起构成"崩溃恢复"链路全错。

### APP-04 崩溃恢复选"是"之后主窗口不显示（进程假死） (P1)

- **位置**：`src/app/main.cpp:213-217`、`260-264`
- **证据**：

```cpp
// main.cpp:215-216
MainWindow w;
w.hide();       // 先藏起来, 等 splash 结束再显示
QTimer::singleShot(3000, &splash, [&splash, &w]() {
...
    if (ret == QMessageBox::Yes) {
        const QString active = SessionManager::instance().restoreActiveFile();
        w.restoreSessionFiles(validFiles, active);   // ← 从头到尾没有 show/applyWindowMode
    } else {
        w.setMode(WindowMode::Normal);               // ← 只有 No 分支会显示
```

- **现象 / 复现路径**：按 APP-03 让程序进入恢复流程（现在每次都会进入）→ 弹窗点"是"→ 窗口消失、任务栏无窗口、进程仍在运行（日志继续写），用户只能任务管理器结束。
- **根因**：`restoreSessionFiles()` 只负责建 tab/切 `mainStack` 页（3224-3267），不负责显示窗口；clean 分支靠 `applyWindowMode()` 内部 `showNormal()/showMaximized()` 顺带 show，而 Yes 分支漏掉了这一步。`MainWindow` 构造期的 `show` 被 `w.hide()` 抵消（`main.cpp:216`）。
- **修复建议**：Yes 分支末尾统一 `w.applyWindowMode();`（内部就是 `setMode(m_mode)`）；更稳的做法是构造期不 show（见 APP-29），由启动回调统一 show 一次。
- **关联**：无单实例（APP-31）会让用户以为没启动而再点一次，叠加成两个进程抢 QSettings。

### APP-05 `bindLeftDockWidgets` 重复 attach → 连接累积 + 陈旧连接 (P1)

- **位置**：`src/app/mainwindow.cpp:3371-3418`；`src/media/tools/LeftToolBar.cpp:212-238`；`ImageOptionBar.cpp:68-99`；`ModeSelectorBar.cpp:186-201`
- **证据**：

```cpp
// mainwindow.cpp:3388-3399 —— 每次切 tab 都直接 attach, 不先 detach
if (leftToolBar && iw->toolMediator() && iw->toolContext()) {
    leftToolBar->attach(iw->toolMediator(), iw->toolContext());
}
if (modeSelectorBar && iw->toolContext()) modeSelectorBar->attach(iw->toolContext());
if (imageOptionBar && iw->toolContext()) imageOptionBar->attach(...);
```

```cpp
// LeftToolBar.cpp:218-237 —— attach 内部无条件 connect, 无 UniqueConnection
connect(m_toolMed, &mediators::ToolMediator::toolSwitched, this, &LeftToolBar::onToolMediatorSwitched);
for (auto it = m_buttons.begin(); ...) {
    connect(btn, &QToolButton::clicked, this, [this, id, btn](bool checked) { ... m_toolMed->switchTool(id); });
}
```

- **现象 / 复现路径**：A/B 两张图来回切 5 次 → 点一次"套索"→ `switchTool` 被调用 5 次（每次都重建 `ToolState`，正在进行的拖拽/半成品选区被反复 reset）；同一批 `btn→lambda` 连接也累积 5 份，且旧 mediator 的连接不清除。
- **根因**：`attach()` 被设计成"幂等"但实现是"追加"；`bindLeftDockWidgets(nullptr)` 会 `detach()`，所以 A→文本tab→B 这条路径不会累积，A→B 直接切就会。`LeftToolBar::detach()` 只断开当前 `m_toolMed`，历史 mediator 的连接靠对象析构自动断，但**按钮信号的重复 lambda 不会消失**。
- **修复建议**：`attach()` 第一行 `detach()`（或 `connect(..., Qt::UniqueConnection)`）；`m_toolMed/m_ctx` 改 `QPointer`；把"点击按钮"改成在构造时只连一次、槽里读 `m_toolMed`（`if (!m_toolMed) return;`）。
- **关联**：APP-02 的悬垂问题在本条修好后也不会自动消失（关闭路径仍不重绑）。

### APP-06 两组快捷键歧义，Ctrl+A / Ctrl+Alt+C 完全不生效 (P1)

- **位置**：`src/app/mainwindow.cpp:386` 与 `988`；`650` 与 `709`
- **证据**：

```cpp
// 编辑>全选 (386)
m_actSelAll->setShortcut(QKeySequence::SelectAll);        // Ctrl+A
// 选择>全部 (988)
aSelAll->setShortcut(QKeySequence(QStringLiteral("Ctrl+A")));  // 又是 Ctrl+A
// 图像>裁剪... (650)
aImgCrop->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+C")));
// 图像>画布大小... (709)
aImgCanvas->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+C")));
```

- **现象 / 复现路径**：按 Ctrl+A，Qt 在控制台打印 `Ambiguous shortcut overload: Ctrl+A`，**两个 action 都不触发**；Ctrl+Alt+C 同理（PS 里 Ctrl+A 是全选、Ctrl+Alt+C 是画布大小）。
- **根因**：`QShortcutMap` 在同一 `WindowShortcut` 上下文内发现两个 enabled、同键的 action 时按歧义处理，直接丢弃；`QAction` 不会因为后注册而覆盖前一个。
- **修复建议**：编辑>全选与选择>全部合并到一个 action（或一个转发槽按 active doc 分派）；画布大小改用 PS 的 `Ctrl+Alt+C` 并给"裁剪…"换一个键（PS 的裁剪工具主要靠 C 键 + 选项栏）。建议加一个启动期自检：遍历 `findChildren<QAction*>()` 收集 `shortcut()`，重复者 `qWarning`。
- **关联**：APP-45（标准键映射）同一类问题。

### APP-07 菜单/action 没有任何 enable 状态管理 (P1)

- **位置**：`src/app/mainwindow.cpp:340-1195`（构建期只对静态占位项 `setEnabled(false)`），运行时无更新
- **证据**：

```cpp
// mainwindow.cpp:2018-2022 —— 主页无 tab 时"保存"悄悄返回
void MainWindow::onSaveFile()
{
    auto *w = widgetAt(ui->tabWidget->currentIndex());
    if (!w || w == widgetAt(-1)) return;    // widgetAt(-1)==nullptr, 恒等于 w 为空
```

```cpp
// mainwindow.cpp:2110-2116 —— 空栈时 Undo 仍可点, 只弹状态栏
void MainWindow::onUndo() {
    if (auto *img = qobject_cast<ImageWindow *>(...)) img->undoPublic();
    else statusBar()->showMessage(tr("当前页面没有可撤销的操作"), 2000);
}
```

- **现象 / 复现路径**：启动在主页时"文件→保存/另存为/导出""编辑→撤消/剪切/复制/粘贴""图像/图层/文字/选择/滤镜"全部可点，绝大多数点了毫无反应；图像打开了但撤销栈为空时"撤消"仍高亮。
- **根因**：没有单一"命令状态"聚合点；`QUndoStack`（ImageWindow 里的 `m_undoStack`）的 `canUndoChanged/canRedoChanged` 从未被 `MainWindow` 连接；`currentChanged` 里也没有刷新 action 可用性。
- **修复建议**：`buildActions()` 末尾把 action 存进结构化表（如 `{QAction*, std::function<bool(Context)>, QString docType}`），在 `onTabChanged/updateWindowTitle` 调 `updateActionStates()`；`QUndoStack::canUndoChanged` → `m_actUndo->setEnabled`。这样同时能消灭 30 处重复的 `if (!iw) { showMessage(...); return; }` 模板（见 5.2）。
- **关联**：PS 的菜单"灰态"是用户判断"能不能做"的主要途径，缺失会显著误导。

### APP-08 剪切/复制/粘贴只对文本文档生效，图像文档完全无响应 (P1)

- **位置**：`src/app/mainwindow.cpp:2126-2129`
- **证据**：

```cpp
void MainWindow::onCut()     { if (auto *d = qobject_cast<DocWindow *>(widgetAt(ui->tabWidget->currentIndex()))) d->editor()->cut(); }
void MainWindow::onCopy()    { if (auto *d = qobject_cast<DocWindow *>(...)) d->editor()->copy(); }
void MainWindow::onPaste()   { if (auto *d = qobject_cast<DocWindow *>(...)) d->editor()->paste(); }
void MainWindow::onSelectAll(){ ... d->editor()->selectAll(); }
```

- **现象 / 复现路径**：打开图片 → Ctrl+C（或菜单）→ 没有任何反应，剪贴板也没内容；Ctrl+V 不能把剪贴板图片贴成新图层/新文档。
- **根因**：`ImageWindow` 没有实现 cut/copy/paste（无论合成图、选区还是图层），`MainWindow` 也没按文档类型分派；`QApplication::clipboard()` 完全没用过。
- **修复建议**：`ImageWindow` 增加 `copySelectionToClipboard()/pasteAsLayer()`（`QClipboard::setImage/ image()`），菜单按 `docTypeOf()` 分派；Ctrl+Shift+V 粘贴为新文档在 `MainWindow` 实现。
- **关联**：需要 media 模块配合（见第 7 节）。

### APP-09 顶级菜单顺序错乱（帮助插在中间，图像~滤镜在最后） (P1)

- **位置**：`src/app/mainwindow.cpp:437, 450, 476, 533, 545, 554, 598, 616, 771, 926, 986, 1053`
- **证据**：

```cpp
QMenu *mFile   = mb->addMenu(tr("文件"));      // 437
QMenu *mEdit   = mb->addMenu(tr("编辑"));      // 450
QMenu *mView   = mb->addMenu(tr("视图"));      // 476
QMenu *mSetup  = mb->addMenu(tr("设置"));      // 533
QMenu *mPlugins= mb->addMenu(tr("增效工具"));  // 545
QMenu *mWindow = mb->addMenu(tr("窗口"));      // 554
QMenu *mHelp   = mb->addMenu(tr("帮助"));      // 598  ← 帮助在这里
QMenu *mImage  = mb->addMenu(tr("图像"));      // 616  ← 之后才加图像/图层/…
QMenu *mLayer  = mb->addMenu(tr("图层"));      // 771
QMenu *mText   = mb->addMenu(tr("文字"));      // 926
QMenu *mSelect = mb->addMenu(tr("选择"));      // 986
QMenu *mFilter = mb->addMenu(tr("滤镜"));      // 1053
```

- **现象**：菜单栏实际渲染顺序 `文件 编辑 视图 设置 增效工具 窗口 帮助 图像 图层 文字 选择 滤镜`。PS 是 `文件 编辑 图像 图层 文字 选择 滤镜 3D 视图 窗口 帮助`。
- **根因**：`buildActions()` 是一段 850 行的巨型函数，前半段在 2026-10 的 P7.2 重构里先建了 7 个菜单，后半段 F-J 阶段的 5 个业务菜单直接追加到末尾，没有重排。`设置` 作为顶级菜单也不符合 PS（PS 在 编辑>首选项 / Ctrl+K）。
- **修复建议**：把菜单创建顺序改成 PS 顺序；`设置` 收进"编辑"，`增效工具` 移到帮助前（PS 里它在 `窗口` 之后）；每个菜单一个 `buildXxxMenu()` 函数（见 6 节）。
- **关联**：`QMenuBar` 被 `setMenuWidget()` 塞进自绘标题栏（1280），`Alt` 助记符是否仍能用需要单独验证 [待确认]。

### APP-10 暗色主题下选中 tab 白底白字（QSS 末尾重复段覆盖主题） (P1)

- **位置**：`src/app/styles/Vistella.qss:413-424`（与 122-142 重复冲突），另 391-397/405-411/425-433 硬编码颜色
- **证据**：

```css
/* 122-142 第一段: 主题化 */
QTabBar::tab:selected {
    background: transparent;
    color: {{TAB_TEXT_SEL}};              /* 暗色 = #ffffff */
    border-bottom: 2px solid {{ACCENT}};
}
...
/* 413-424 末尾又写一遍, 覆盖 background */
QTabBar::tab:selected {
    background: #ffffff;                  /* ← 硬编码 */
    border-bottom: 2px solid {{ACCENT}};
}
```

- **现象**：点标题栏 ☀ 切到暗色主题 → 主文档 tab、右侧 dock 的 tab 全部变成白底 + 白字（选中项文字消失），同时 `QSplitter::handle`(#d0d0d0)、`#btnGpuStatus`(#555)、`#titleBtnClose:hover`(#e81123) 仍按亮色渲染，界面花掉。
- **根因**：QSS 后写的规则在属性上覆盖先写的；末尾这段是 P7.11 "PS 风格样式"追加的，作者只测了亮色。`color` 没有在该段重设，于是继续用第一段的主题色。同一个文件里既有 `{{占位符}}` 又有 `#ffffff`，主题机制被局部绕过。
- **修复建议**：删掉 413-424 的重复 `QTabBar::tab` 两段，把新增样式合并进第一段并全部改用占位符（新增 `{{TAB_SELECTED}}` 已有；`#d0d0d0`→`{{BORDER}}`，`#555`→`{{TEXT_SUBTLE}}`，`#e81123` 保留但仅 hover 用）。加一条 CI 检查：qss 里除 `#e81123` 外不允许出现十六进制颜色。
- **关联**：APP-14。

### APP-11 主页"打开"取消后落到空工作区，无法返回主页 (P1)

- **位置**：`src/app/mainwindow.cpp:2463-2464`、`2006-2016`、`1778-1801`
- **证据**：

```cpp
// mainwindow.cpp:2463-2464
void MainWindow::onHomeNewRequested()    { onNewFile(); switchToWorkspace(); }
void MainWindow::onHomeOpenRequested()   { onOpenFile(); switchToWorkspace(); }  // ← 无条件切页

// mainwindow.cpp:2006-2016
void MainWindow::onOpenFile() {
    ...
    const QString path = QFileDialog::getOpenFileName(...);
    if (path.isEmpty()) return;      // 取消, 调用方不知道
```

- **现象 / 复现路径**：停在主页 → 点"打开文件" → 在文件对话框点取消 → 界面切到 page 1 = 空的 `tabWidget`：灰色空白区域，没有 tab、没有"返回主页"按钮/菜单项（P7.2 把 视图>主页 入口删了；唯一的"返回主页"在 tab 右键菜单里，但没有 tab 可右击）。
- **根因**：`onHomeOpenRequested` 把"调起打开流程"和"切换 workspace"当成原子操作，没检查打开是否成功；同时 HomePage 缺少常驻入口（标题栏/菜单）。
- **修复建议**：`onOpenFile()` 改成返回 `bool`/路径，或让 `onHomeOpenRequested` 里判断 `ui->tabWidget->count()`；另外在标题栏左侧 logo 或"文件"菜单加深色"主页"项，保证 page 0 永远可达。
- **关联**：APP-47。

### APP-12 `m_mode` 与 Qt 窗口状态失联（双状态不一致） (P1)

- **位置**：`src/app/mainwindow.cpp:229-244`、`1359-1401`（`m_isMaximized` 同步点 1397）
- **证据**：

```cpp
// 229-238: 外部状态变化只同步 m_isMaximized, 不同步 m_mode
void MainWindow::changeEvent(QEvent *e) {
    if (e->type() == QEvent::WindowStateChange) {
        m_isMaximized = isMaximized();
        if (m_btnMax) m_btnMax->setText(m_isMaximized ? "❐" : "▢");
    }
...
// 1391-1397: setMode 只在自己内部维护 m_mode
m_mode = m;
...
m_isMaximized = isMaximized();
```

- **现象 / 复现路径**：不点标题栏按钮，用 Win+↑ 把窗口最大化（或从任务栏右键选"最大化"）→ 窗口已最大化但 `m_mode` 仍是 Normal → 点"□"按钮调用 `onMaximizeRestore()` → `setMode(Maximized)`（因为 `m_mode != Maximized`）→ 视觉上没变化，用户需要点**两次**才能还原；退出时 `closeEvent` 按 `m_mode` 落盘 Normal。
- **根因**："唯一样本源"其实是 Qt 的 `windowState()`（`isMaximized()`），`m_mode` 只是影子；`setMode()` 的 INVARIANT 注释（mainwindow.h:63-82）假设"所有切换都经过我"，但 Windows 的窗口管理器不受此约束。此外 `setMode(Normal)` 里的 `m_isMaximized = isMaximized()` 在 `showMaximized()` 之后立刻读，Windows 上状态变更可能异步落地，也会短暂不一致 [待确认]。
- **修复建议**：删除 `m_mode`，统一用 `isMaximized()`；`setMode` 的语义变成 `toggleMaximized()`/`restoreNormal()`；`changeEvent` 里按 `windowState()` 刷新按钮并 `persistWindowState()`。若保留 `m_mode`，必须在 `changeEvent(WindowStateChange)` 里 `m_mode = isMaximized() ? Maximized : Normal`（注意排除 minimize/fullscreen）。
- **关联**：APP-13、APP-24。

### APP-13 无边框窗口不能拖边改大小、无系统菜单、无贴靠 (P1)

- **位置**：`src/app/mainwindow.cpp:122`、`3279-3347`
- **证据**：

```cpp
// 122
setWindowFlags(Qt::FramelessWindowHint);     // 去掉原生边框/WS_THICKFRAME

// 3279-3294: 只处理"标题栏拖动", 没有任何边缘命中测试或 resize 逻辑
void MainWindow::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && titleBarDragRect().contains(e->pos())) { ... }
    QMainWindow::mousePressEvent(e);
}
```

- **现象 / 复现路径**：鼠标移到窗口任意边/角 → 光标不变成缩放箭头，拖拽无效；最大化后不能拖标题栏贴靠到屏幕左右半屏；窗口没有投影/圆角；Alt+Space / 右键标题栏没有系统菜单。
- **根因**：`FramelessWindowHint` 会移除 `WS_THICKFRAME/WS_CAPTION`，原生 resize/贴靠/系统菜单全部消失；项目只补了"拖动 + 双击最大化"，漏了 resize 与系统菜单。另外 `m_normalSize` 固定 1280×800（`setMode(Normal)` 1378-1389）导致"还原"永远回到固定尺寸，即使能 resize 也记不住。
- **修复建议**（Qt 原生就能做）：
  1. `nativeEvent` 里处理 `WM_NCHITTEST`：命中客户区边缘 8px 返回 `HTLEFT/HTTOP/HTBOTTOMRIGHT…`，标题栏返回 `HTCAPTION`，最大化按钮区域返回 `HTMAXBUTTON`（顺带解锁 Win11 贴靠提示）；
  2. 或者纯 Qt：边缘 `mouseMoveEvent` 里调 `windowHandle()->startSystemResize(Qt::Edges)`，标题栏拖动改 `windowHandle()->startSystemMove()`（Qt 5.15+，能正确处理多屏 DPI/鼠标捕获）；
  3. 在用户 resize 结束后把 `size()` 写回 `m_normalSize` 并持久化；
  4. 补右键标题栏菜单（最小化/最大化/还原/关闭）。
- **关联**：Qt 能力边界表 4.1-4.5。

### APP-14 运行时主题切换只刷新全局 QSS，80+ 处 inline stylesheet 不跟着变 (P1)

- **位置**：`src/app/mainwindow.cpp:317-336`；`src/core/ThemeManager.cpp:37-49`；全项目 `setStyleSheet` 80+ 处
- **证据**：

```cpp
// mainwindow.cpp:328-331
void MainWindow::applyTheme() {
    qApp->setStyleSheet(ThemeManager::instance().globalStyleSheet());  // 只做这一件事
}
```

```cpp
// 其他模块的 widget 级硬编码 (示例)
src/media/docks/PropertiesDock.cpp:40   m_pathLabel->setStyleSheet("color: gray;");
src/media/imagewindow/AdjustmentPanel.cpp:308 info->setStyleSheet("color: #666; font-size: 9pt;");
src/ui/settingsgpupanel.cpp:61          m_statusLabel->setStyleSheet("color: #333; ...");
src/media/docks/PatternTab.cpp:140      cell->setStyleSheet("border: 2px solid #4A90E2; ...");
```

- **现象**：切暗色后，属性面板 label 仍是深灰（几乎看不见）、`#4A90E2` 蓝框刺眼；`MainWindow` 自己的 pin/close 按钮靠 `applyPinButtonStyle()` 单独重刷（1576-1585），其他模块没有等价逻辑。
- **根因**：`qApp->setStyleSheet()` 会让 Qt 对所有 widget 重新 polish（全局 QSS 是生效的，这点没问题），但 **widget 自己的 `setStyleSheet()` 优先级高于应用级 QSS**，且不会自动重新生成；项目里颜色是散落在 80+ 处 C++ 字符串里的，ThemeManager 的 palette 只覆盖了全局 QSS 这一层。
- **修复建议**：把 `setStyleSheet("color: gray")` 这类全部替换为 `QLabel[class="hint"]` 的 QSS 类选择器（`setProperty("class", "hint")`），颜色统一走 `{{TEXT_SUBTLE}}`；确实需要动态色的（颜色板按钮）注册到 ThemeManager 的 `themeChanged`。建议加 dev-only 检查：遍历 `findChildren<QWidget*>()`，非空 `styleSheet()` 就 `qWarning`。
- **关联**：QSS 主题化是跨模块工程（见第 7 节）。

### APP-15 ImageWindow 作为 QMainWindow 被塞进 tab 的后果 (P1)

- **位置**：`src/media/imagewindow.cpp:283-291`（构造）、`2455-2477`（屏幕模式）；`src/app/mainwindow.cpp:1706`（addTab）
- **证据**：

```cpp
// imagewindow.cpp:283-291
ImageWindow::ImageWindow(QWidget *parent) : QMainWindow(parent) {
    ui->setupUi(this);
    // 关键: 当作 tabWidget 的子 widget 时, 必须去掉 top-level 标志
    setWindowFlags(Qt::Widget);
```

```cpp
// imagewindow.cpp:2455-2477 —— 对非顶层 widget 调 showFullScreen/showNormal
void ImageWindow::applyScreenMode(int mode) {
    if (mode == tools::AppBar::ScreenStandard) {
        showNormal();                       // 文档: 只对 top-level 有效
        if (menuBar()) menuBar()->show();   // ← 懒创建一条空菜单栏
        if (statusBar()) statusBar()->show();
    } else if (mode == tools::AppBar::ScreenFullScreen) {
        if (menuBar()) menuBar()->hide();
        if (statusBar()) statusBar()->hide();
        showFullScreen();                   // ← 无效
```

- **现象**：① 每个图片 tab 底部多一条 `QStatusBar`（`ImageWindow` 里 100+ 处 `statusBar()->showMessage(...)` 懒创建），主窗口最下面还有 `MainWindow` 自己的状态栏 → 双状态栏，操作提示出现在里层那条，用户容易看不到；② 用过一次 AppBar 的屏幕模式后，每个 tab 顶部多出一条空白 `QMenuBar`（`menuBar()` 懒创建）；③ 三档屏幕模式除隐藏内层状态栏外没有任何效果，主窗口标题栏/菜单栏不受影响（`AppBar::screenModeSelected` 只转发给 active image，mainwindow.cpp:181-184）；④ 每个文档额外背一整套 QMainWindow 布局/停靠区，内存和 polish 成本翻倍。
- **根因**：用 QMainWindow 当文档视图。QMainWindow 的菜单栏/状态栏/停靠区语义是"顶层窗口"的，即使 `setWindowFlags(Qt::Widget)` 让它变成了子 widget，这些 API 仍会为它创建子控件并参与布局，只是"窗口级"操作（showNormal/showFullScreen/restoreState）失效。项目已经从 `ImageWindow` 搬走了左侧 dock（P7.6.x v3），但右侧 dock（`rightDockContainer`）和 statusBar 还在里面。
- **修复建议**：`ImageWindow` 改成 `class ImageWindow : public QWidget` + `QVBoxLayout`（canvas + 右侧 dock 用 QSplitter）；右侧 dock 按左侧同款方案搬到 `MainWindow`；所有 `statusBar()->showMessage` 改走 `MainWindow::statusBar()`（或注入一个 `IMessageSink*`）；屏幕模式在 `MainWindow` 实现：`showFullScreen()` + 隐藏 `m_titleBar` + 隐藏 statusBar + 隐藏两个 dock。
- **关联**：这条是架构级的，改动面覆盖 media/ui，必须跨模块协作。

### APP-16 会话只存路径且只在干净退出时保存，崩溃恢复名不副实 (P1)

- **位置**：`src/app/mainwindow.cpp:3197-3214`（`saveSession`）、`3224-3267`（`restoreSessionFiles`）、`298`（唯一调用点）
- **证据**：

```cpp
// 3197-3214: 只收集"有 filePath 的"文档, 未命名/未保存的文档直接丢
for (int i = 0; i < ui->tabWidget->count(); ++i) {
    ...
    if (!p.isEmpty()) { files << p; if (i == activeIdx) activeFile = p; }
}
SessionManager::instance().saveOpenFiles(files);
SessionManager::instance().saveActiveFile(activeFile);
```

- **现象**：新建图片 → 编辑 30 分钟 → 程序崩溃/断电 → 重启提示"仍有 N 个文件未关闭，是否恢复" → 点"是"只恢复上一次干净退出时的文件（这次新开的一个都没有）；未命名的文档永远不会被记录；pin 状态、tab 顺序、缩放/滚动位置全部丢失；恢复后还会静默丢弃未保存修改（磁盘版本覆盖）。
- **根因**：`saveSession()` 只在 `closeEvent` 调用（298），崩溃时不会执行；持久化粒度只有"路径字符串"，没有自动保存快照。这套机制本质上是"上次干净退出的文件列表"，被当成崩溃恢复来宣传。
- **修复建议**：① 每 N 秒/每次撤销栈 push 后做轻量自动保存（未命名文档存 `AppLocalData/session/<uuid>.png` + 元数据 json，命名文档存 sidecar 或直接快照）；② `saveSession()` 在 tab 增删/切页时也调用（`SessionManager` 增加 debounce）；③ 恢复时提示"已恢复到你最后一次自动保存（时间戳）"；④ 用 `QMainWindow::saveState()/restoreState()` 存窗口/布局/dock，`QTabBar` pin 状态存 json。
- **关联**：APP-03、APP-04；`QMainWindow::saveState()` 目前完全没用（`onResetLayout` 反而在 `restoreState` 一个从未写过的文件，APP-19）。

### APP-17 选区羽化：12MP 以上整型溢出 + UI 线程百万级循环 + 上百 MB 临时内存 (P1)

- **位置**：`src/app/mainwindow.cpp:1001-1045`（重点 1014-1042）
- **证据**：

```cpp
// 1014-1042: 手写积分图 + 手写 box blur, 全程在 UI 线程
const int W = mask.width();
const int H = mask.height();
std::vector<int> sum((W + 1) * (H + 1), 0);      // 12MP → 48MB; 24MP → 96MB
for (int y = 0; y < H; ++y) {
    const uchar* row = mask.constScanLine(y);
    int rowSum = 0;
    for (int x = 0; x < W; ++x) {
        rowSum += row[x];
        sum[(y + 1) * (W + 1) + (x + 1)] = sum[y * (W + 1) + (x + 1)] + rowSum;  // 总和上限 W*H*255
    }
}
...
dst[y * stride + x] = static_cast<uchar>((d - b - c + a) / area);
```

- **现象 / 复现路径**：打开 4000×3000 (12MP) 照片 → 全选 → 选择→羽化(半径 20) → 结果出现明显色块/错误边缘（尤其右下角），大图下界面卡住数秒；24MP 时可能直接卡死/OOM。
- **根因**：积分图用 `int` 累加，`W*H*255` 在 12MP 时 ≈ 3.06e9 > `INT_MAX`(2.147e9)，有符号溢出是 UB；`(d-b-c+a)` 同样溢出。算法本身还是 box blur（PS 的羽化是高斯），质量也不同。整个过程在主线程同步跑，`W*H` 次除法。
- **修复建议**：直接用 OpenCV：`cv::integral(mask, sum, CV_32S)` 只在 ≤8MP 安全，应改 `cv::blur`/`cv::GaussianBlur`（`cv::Mat` + `CV_8UC1`，边界 `BORDER_REPLICATE`），并在 `engine.background()` 上异步执行 + 完成回调；半径用 `cv::GaussianBlur` 的 σ 映射。这样同时解决正确性、性能和内存三个问题。
- **关联**：同样的积分图算法在 `Lasso` 羽化里可能重复（media 模块），需要一起收敛。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 顶级菜单 | 文件/编辑/图像/图层/文字/选择/滤镜/3D/视图/窗口/帮助（11 个，顺序固定） | 12 个且顺序错乱：文件/编辑/视图/设置/增效工具/窗口/帮助/图像/图层/文字/选择/滤镜 | 有但错 | P1 |
| 3D 菜单 | PS 2025 已弱化，部分版本仍有 | 完全删除，仅剩注释 | 无（可接受） | P3 |
| 文件→最近打开 | 子菜单列出最近 10+ 文件 | 无菜单项，`RecentManager` 只在打开对话框里取起始目录 | 缺失 | P2 |
| 文件→导入/导出/脚本/打印/文件简介 | 完整 | 无（只有 导出… 一项，内部是 ExportDialog） | 缺失 | P2 |
| 编辑→首选项 | 编辑>首选项(Ctrl+K) | 独立顶级"设置>首选项…" | 部分（位置错误） | P2 |
| 编辑→填充/描边/变换子菜单 | 完整 | 只有"自由变换"一项 | 缺失 | P2 |
| 编辑→剪切/复制/粘贴 | 对图像/图层/选区均可用 | 只对 DocWindow 文本有效，图像文档无响应 | 有但错 | P1 |
| 图像→调整（色阶/曲线/色相饱和度/亮度对比度…） | ~20 项核心菜单 | 整段缺失（曲线/色阶/黑白/通道混合器被塞进"滤镜"末尾） | 缺失 | P1 |
| 图像→模式 | RGB/CMYK/Lab/索引/位图/灰度/双色调 | 只有 RGB(伪)/灰度，且 RGB 实现是"转灰度再转回 BGR" | 有但错 | P2 |
| 图像→应用图像/计算/变量/陷印 | 有 | 无 | 缺失 | P3 |
| 图层→图层样式/编组/排列/对齐/图层复合/修边 | 有 | 全部缺失（只有 新建/复制/删除/上下移/合并/拼合/蒙版/智能对象） | 缺失 | P2 |
| 图层→合并可见图层 | 合并所有可见图层 | 实际调用 `MergeDown`（向下合并） | 有但错 | P2 |
| 文字→面板/创建工作路径/文字变形/更新所有文字图层 | 有 | 只有 字体/字号/颜色/粗体/斜体 | 部分 | P2 |
| 选择→色彩范围/修改(边界/平滑/扩展/收缩)/变换选区/存储载入选区 | 有 | 仅 全部/取消/反选/羽化 | 缺失 | P2 |
| 滤镜→滤镜库/液化/模糊/锐化/杂点/风格化 | 完整分组 | 有 6 个大滤镜 + 8 个子菜单，但 20+ 项是灰态占位；模糊被塞进"风格化"；"渲染>照亮边缘"映射到 DiffuseGlow | 部分 | P2 |
| 视图→标尺/参考线/网格/对齐/显示额外内容/实际像素/屏幕模式 | 完整 | 只有 放大/缩小/重置布局/适合屏幕 | 缺失 | P2 |
| 窗口→文档列表/工作区/平铺/堆叠 | 完整（MDI 可选） | 文档列表有；工作区只切 dock 可见性；平铺/堆叠灰态占位 | 部分 | P2 |
| 帮助→文档/快捷键/关于 | 有 | 只有"关于" | 缺失 | P3 |
| 菜单图标 | 几乎每项都有图标 | 全部无图标（qrc 里已有 16 个工具图标可用） | 缺失 | P3 |
| Tab 系统 | 文档可浮动/拖出成新窗口/平铺 | 只有 QTabWidget：可拖排序、pin、自绘关闭；不能拖出成新窗 | 部分 | P2 |
| 窗口模式记忆 | 记住最大化/尺寸/位置 | 强制 Normal 1280×800，不持久化尺寸和位置 | 有但错 | P2 |
| 崩溃恢复 | 自动保存恢复（"恢复"面板） | 只有路径列表，且逻辑双重错误（APP-03/04/16） | 有但错 | P1 |
| 多显示器/DPI | 原生支持，跨屏拖拽正确 | DPI 策略设置正确，但标题栏/按钮固定 px、窗口不 resize、还原不 clamp（APP-37） | 部分 | P2 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 无边框窗口拖边 resize | QT-NATIVE | Qt 5.15+/6 提供 `QWindow::startSystemResize(Qt::Edges)`；或 `nativeEvent` 处理 `WM_NCHITTEST` 返回 HTLEFT 等。项目完全没实现 | 在 `MainWindow` 加边缘命中测试，调 `startSystemResize`；或改 `nativeEvent` 方案 |
| 标题栏拖动 / 双击最大化 | QT-NATIVE | `QWindow::startSystemMove()` 由平台接管，能正确处理多屏 DPI、鼠标捕获、拖动时还原；项目手写 70 行 `move()` 逻辑 | 替换 `mouseMoveEvent` 手写逻辑；保留双击/按钮走 `setMode` |
| Win11 贴靠布局（悬停最大化按钮弹出布局） | QT-LIMIT | 需要窗口有 `WS_THICKFRAME` 且 `WM_NCHITTEST` 返回 `HTMAXBUTTON`，Qt 无公开 API；可近似 | 用 `nativeEvent` 返回 `HTMAXBUTTON` 让系统自己弹；或自绘贴靠菜单作为近似 |
| 无边框窗口的 DWM 投影/圆角 | QT-EFFORT | Qt 不支持给 frameless 窗口加系统阴影；需要 `DwmExtendFrameIntoClientArea` + 处理 `WM_NCCALCSIZE`，是平台专有代码 | 用 `#ifdef Q_OS_WIN` 加 DWM 调用（约 100 行），或接受无阴影 |
| Aero Snap（拖到屏幕边缘分屏） | QT-EFFORT | 需要恢复 `WS_THICKFRAME` 或自绘贴靠逻辑；纯 Qt 层不可达 | 与 4.1 一起做：设置 `WS_THICKFRAME` 后让系统处理贴靠，同时 `WM_NCCALCSIZE` 去掉非客户区 |
| 多显示器 / 分数 DPI 下窗口还原 | QT-NATIVE | Qt6 自动处理 per-monitor DPI；但项目用固定 32px/1280×800 且不 clamp `availableGeometry`，是用法问题 | `setMode(Normal)` 里用 `qBound`/`QScreen::availableGeometry()` 限制尺寸与位置 |
| 单实例 | QT-NATIVE | `QLocalServer/QLocalSocket`（跨平台）；项目完全没做 | 启动时尝试连接命名管道，失败则 listen，并把 argv 传给已有实例 |
| 运行时主题切换 | QT-NATIVE | `qApp->setStyleSheet` 会自动 repolish 所有 widget；`QPalette` 也可用。问题在 80+ 处 inline stylesheet + 硬编码色 | 去 inline 化 + 颜色单源（见 APP-14） |
| QSS 亮/暗双主题 | QT-NATIVE | 项目已实现 23 个占位符替换，机制本身可行 | 修 APP-10 的重复段/硬编码即可 |
| 菜单/action 动态 enable | QT-NATIVE | `QUndoStack::canUndoChanged/canRedoChanged` + `QAction::setEnabled`；项目从未连接 | 建 `updateActionStates()`（见 APP-07） |
| 键盘快捷键冲突检测 | QT-LIMIT | Qt 只在运行期由 `QShortcutMap` 打印 "Ambiguous shortcut overload"，没有编译期检查 | 启动期自检：遍历 `findChildren<QAction*>()` 检测重复 `shortcut()` |
| 菜单图标 | QT-NATIVE | `QIcon::fromTheme` / `QStyle::standardIcon` / 已有 `:/icons/tools` qrc | 给关键 action `setIcon`，普通项用 `QStyle::SP_*` |
| 崩溃恢复 / 自动保存 | QT-EFFORT | Qt 无内置文档自动保存；需要自研快照 + 原子写（`QSaveFile`）。工作量 ~3-5 人天 | 见 APP-16；`QSaveFile` + `QTimer` 去抖 |
| 崩溃转储（minidump） | QT-3RD | 平台 API 可做（`MiniDumpWriteDump`），但成熟方案是 Crashpad/Breakpad（BSD 许可，~2-4MB）或 Sentry Native（MIT） | 引 Crashpad；`SetUnhandledExceptionFilter` 里写 dump（替换 APP-30 的非法过滤器） |
| QML 启动屏 | QT-NATIVE | 已 `qt_add_resources` 进 `:/qml/splash_screen.qml`，代码却从磁盘读（APP-28） | 改用 qrc 路径，删除 POST_BUILD 拷贝 qml 那段 |
| 会话/窗口布局持久化 | QT-NATIVE | `QMainWindow::saveState()/restoreState()` 已具备；项目只写了 `restoreState` 的一半 | 补 `saveState`，配合 APP-19/APP-16 |
| 语言热切换 | QT-NATIVE + QT-EFFORT | `installTranslator` + `LanguageChange` 是 Qt 原生机制；但项目是命令式建菜单，需要重构成 registry 才能全量 retranslate（~1-2 人天） | 见 APP-23 |
| Tab 钉住 / 拖出成新窗口 | QT-EFFORT | `QTabBar` 没有 pin 语义（项目自绘已实现）；拖出成新窗口需 `QDrag` + 新建顶层窗口 + 状态迁移（~3-5 人天） | pin 保留现方案；tear-out 排期做 |

## 5. 设计问题

### 5.1 分层被打破：app 层直接执行业务命令

`MainWindow` 里直接出现 `iw->currentImage().clone()` + `cv::resize`/`cv::cvtColor`/`cv::inRange`/`cv::boundingRect`（622-758）、直接构造 `selection::SelectionCommand`/`layers::LayerMaskCommand`/`LayerCommand`（2133-2164, 2624-2793）、直接算羽化（APP-17）。后果：① 同一条"调整大小/画布大小/裁切"逻辑在 app 层和 media 层各有一套；② 这些操作绕过 `ImageWindow` 的渲染/缓存失效协议，容易造成"图变了但右侧属性面板/图层缩略图没变"；③ app 层被迫 include `opencv2/imgproc.hpp`（mainwindow.cpp:32）与 `imagewindow.h`（会传递到 `mainwindow.h`，见 CMakeLists:17-19 的 PUBLIC OpenCV）。建议：新增 `mediators::DocumentCommandMediator` 或 `ImageOps` 接口，app 只发"意图"（enum + 参数），media 负责执行与撤销。

### 5.2 上帝对象：`mainwindow.cpp` 3518 行 / `buildActions()` 约 850 行

按职责可以拆成 6 块（建议的类 + 行数估算）：

| 目标类 | 迁出内容 | 现位置 | 预计行数 |
|---|---|---|---|
| `AppTitleBar`（QWidget） | 3 段容器、logo、主题/用户/窗控按钮、菜单条挂载 | 1207-1337, 1289-1337 | ~250 |
| `WindowModeController`（QObject） | `setMode/loadWindowState/changeEvent/closeEvent 的 mode 部分/mousePress·Move·Release·DblClick/titleBarDragRect` | 229-244, 1359-1430, 3279-3347 | ~200 |
| `MenuActionRegistry`（QObject） | `buildActions()` 全部菜单+action+快捷键+enable 策略 | 340-1196 | ~900 |
| `TabManager`（QObject） | `addDocTab/onTabChanged/onTabCloseRequested/onTabContextMenu/pin/reorder/refreshTabTitle/rebuildDocsMenu` | 1496-1990 | ~500 |
| `DockBinder`（QObject） | `bindLeftDockWidgets` + 左侧 4 dock 的创建/attach/detach 协议 | 142-189, 3362-3418 | ~150 |
| `SessionController` | `saveSession/restoreSessionFiles` + 自动保存 | 3195-3267 | ~150 |
| 业务命令（迁到 media） | 图像大小/画布/裁切/灰度/羽化/蒙版/智能对象/滤镜转发 | 615-1196, 2480-3115 | ~800 迁出 |

拆完后 `MainWindow` 只剩 <600 行：组装 + 跨控制器信号连接。

### 5.3 生命周期与所有权：裸指针 + unique_ptr + Qt parent 三套并存

- `m_workspaceMed`（`std::unique_ptr`）在 `~MainWindow` 的**成员析构阶段**销毁，而 `ImageWindow`（`tabWidget` 的子对象）要到 `~QWidget` 才销毁——`RightPanelDock` 里 `mediators::WorkspaceMediator* m_wsMed`（RightPanelDock.h:190）在 mediator 已析构后仍然悬垂（虽然正常路径没人再读它，但任何析构期回调都会 UAF）。
- `m_brushOpt`（MainWindow 的子 widget）被 `installBrushOptionsPage` reparent 到 `ImageOptionBar`，所有权在 Qt tree 里，但字段是裸指针且注释说"共享"，跨文档语义不清楚。
- 左侧 dock 长期持有 `ImageWindow` 内部对象的**裸指针**（上文的 P0 根因）。
- 建议：跨对象一律 `QPointer`；长生命周期持有者注册到被持者的 `destroyed` 信号；`MainWindow` 显式在 `closeEvent` 里先 `bindLeftDockWidgets(nullptr)` 再销毁文档。

### 5.4 命令式 UI 构建导致 30 处重复模板与不可测试

`qobject_cast<ImageWindow*>(widgetAt(ui->tabWidget->currentIndex()))` 这行在 mainwindow.cpp 出现 **30 次**，每次后跟同样的 `if (!iw) { statusBar()->showMessage(tr("没有活动的图片窗口"), 2000); return; }`。后果：新增菜单项 = 复制粘贴 + 容易忘 enable/忘记切页；无法单测（MainWindow 与具体 ImageWindow/DocWindow 强耦合）。建议：`activeDoc()` 返回抽象 `IDocument*`，菜单项统一走 `Command` 对象（可注册、可 enable 查询、可 undo 描述）。

### 5.5 全局可变状态

`s_closing`（匿名命名空间）、`s_themeConnOnce`、`s_inReorder`（函数内 static）三处隐藏全局态：不可重入、无法单测、多窗口/多测试实例会互相污染。建议收进 `TabManager` 成员。

### 5.6 性能

- 主题切换：`qApp->setStyleSheet` 全量 repolish（每个 widget 重新计算样式），当前窗口有数百 widget（含每个 tab 的 QMainWindow 子树），预计 100-300ms 卡顿（`applyTheme` 在主线程同步执行，且 `themeChanged` 还可能被多处再触发）。
- 启动：splash 3 秒是纯等待（QML 动画在跑，主线程其实已经把 MainWindow 构造完），期间没有任何后台预热。
- 日志：`main.cpp:56-72` 每条 qDebug 同步 `QTextStream << flush` 写盘（`QFile::flush` → 系统调用），调试密集时会明显拖慢；且与 spdlog 双写。
- `onTabChanged` 每次遍历所有 tab 调 `refreshTabTitle`（1808），每次 `refreshTabTitle` 又 `setTabText/setTabToolTip/setTabTextColor` 三次，tab 多时有可见延迟。

### 5.7 异常安全

`main.cpp:75-313` 用 `try/catch` 包住 `a.exec()`，但 Qt6 的事件分发不保证 C++ 异常能穿过平台事件循环（Qt 明确不建议在 slot 里抛异常，`QApplication::notify` 之外的平台代码是 C 栈），实际结果是 `std::terminate`/崩溃而不是进入 catch。这段 try 给人的"不会崩"错觉比没有更危险。建议：槽函数内部 `try/catch(const std::exception&)` 就地记录并提示；全局崩溃用 crash dump（APP-30）；不要把 `a.exec()` 包在 try 里当保险。

### 5.8 错误处理

- `onSaveAsFile` 对 DocWindow 的 `saveAsFile` 失败只 `return`（2062-2065），没有错误提示；对 ImageWindow 依赖其内部弹窗。
- `onExportFile` 对 DocWindow 静默（2082-2089）。
- `restoreSessionFiles` 里 `img->loadFile` 失败 `delete img` 后无日志（3245），文件打不开时用户只看到少了一个 tab。
- `onHomeOpenPathRequested` 对同一文件的去重靠字符串比较（3151），大小写/相对路径/符号链接会重复打开。

## 6. 修复建议（按优先级排序）

### P0 立即修（1-2 天，风险低）

1. **APP-01** 统一 `saveDocument()`，`closeEvent` 补 `ImageWindow` 保存分支；加"全部保存/全部放弃"。（4h，低风险，改 mainwindow.cpp + 测试退出流程）
2. **APP-02** `removeTab` 后调用 `onTabChanged(currentIndex())`；`LeftToolBar/ModeSelectorBar/ImageOptionBar` 的 `m_toolMed/m_ctx` 改 `QPointer` 并在 `attach()` 首行 `detach()`。（4h，中风险：attach/detach 语义改动需回归工具切换）

### P1 本轮修（4-6 天）

3. **APP-03** 启动顺序改为"先读 lastExitClean 再置 false"；顺便把 key 拆成 `Session/lastExitClean` + `Session/running`。（1h）
4. **APP-04** restore 分支末尾 `w.applyWindowMode()`；构造期不 show（APP-29 一起改）。（1h）
5. **APP-06 / APP-45** 快捷键去重 + 启动期自检；Ctrl+W 改成显式 `setShortcut(QKeySequence("Ctrl+W"))`；替换掉与 PS 冲突的 `QKeySequence::Replace`。（2h）
6. **APP-12 / APP-13 / APP-24** 窗口模式重构：删 `m_mode` 影子状态、`changeEvent` 按 `windowState()` 同步、`mouseEvent` 换 `startSystemMove/startSystemResize`、`normalSize` 在 resize 后更新并持久化、还原时 clamp `availableGeometry`。（1.5d，中风险：需要多显示器 + 高 DPI 手工回归）
7. **APP-15** `ImageWindow` 改 QWidget、右侧 dock 搬 MainWindow、屏幕模式移到 MainWindow、状态栏消息统一。（2-3d，高风险/大改动，建议单独分支）
8. **APP-07** action 状态机（`updateActionStates` + `canUndoChanged`）。（4h）
9. **APP-08** 图像文档 cut/copy/paste（media 配合，见 7）。（1d）
10. **APP-09** 菜单按 PS 顺序重建（纯重排，低风险）。（2h）
11. **APP-10** QSS 去重复段 + 去硬编码色；加"qss 禁 hex 色"检查。（3h）
12. **APP-11 / APP-47** 主页入口可达性：`onOpenFile` 返回结果、菜单补"主页"、空工作区禁止停留。（2h）
13. **APP-16** 自动保存 + 会话补全（`QSaveFile` + `saveState` + pin json），并修正恢复提示文案。（2d）
14. **APP-17** 羽化改 `cv::GaussianBlur` + 后台线程。（4h，依赖线程池 API）
15. **APP-14** inline stylesheet 清理（跨模块，分批）。（3-5d，见 7）

### P2 后续（1-2 周内）

16. APP-19 重置布局（补 `saveState` + 默认布局资源）；APP-20 合并可见；APP-21 模块按钮灰化/过滤器收敛；APP-23 语言热切换（registry 化）；APP-25 信息树 toggle action；APP-26/27 关闭流程与文档菜单；APP-28 qrc splash + 时长统一；APP-29 启动不 show；APP-30 crash dump；APP-31 单实例；APP-32 删 qDebug 文件日志；APP-38 CMake 整理；APP-44 路径集中；APP-46 dock 区域冲突。

### P3 清理（随手）

17. APP-33/34/35/36/37/39/40/41/42/43/48：注释与代码对齐、死代码删除、版本号设置、品牌统一、DPI 尺寸计算、GPU 状态持久化、static 状态收敛、失败提示、空指针防御、拖动阈值、退出批量确认。

### 架构级重构（2-3 周）

18. 按 5.2 的 6 个类拆分 `mainwindow.cpp`；`ImageWindow` 去 QMainWindow 化；建立 `IDocument`/`Command` 抽象与 action registry；业务命令下沉 media；全局样式单源（ThemeManager + palette + 语义 QSS class）。

## 7. 需要 Lead 协调的跨模块问题

1. **ImageWindow 去 QMainWindow 化（APP-15）**：需要 media 模块把 `statusBar()` 的 100+ 次调用改为注入的消息接口，把 `rightDockContainer` 移到 `MainWindow`（左 dock 已有先例），`applyScreenMode` 迁到 app 层。涉及 `src/media/imagewindow.cpp/.h`、`src/media/docks/*`、`src/app/mainwindow.*`——必须同一个 commit 或严格分阶段。
2. **APP-02/APP-05 的 attach/detach 协议**：需要 `src/media/tools/{LeftToolBar,ImageOptionBar,ModeSelectorBar}.cpp` 配合改 `attach()` 语义（先 detach、QPointer、UniqueConnection）。请 media 审查员确认这些类是否有其他调用方。
3. **APP-08 图像剪贴板**：需要在 `src/media` 新增 `copySelectionToClipboard/pasteAsLayer`，并定义"粘贴为新图层/新文档"的撤销语义（LayerCommand）。
4. **APP-14 主题硬编码色清理**：全项目 80+ 处 `setStyleSheet`（`src/media/docks/*`、`src/media/tools/*`、`src/media/imagewindow/*`、`src/ui/*`），本模块只修 app 自己那 2 处（pin/close）；其余需要各模块审查员在各自报告里给清单，否则暗色主题永远半边亮。
5. **APP-17 积分图/羽化算法重复**：`src/media` 的 Lasso/RefineEdge 可能用同一套手写积分图，请 media 审查员核对是否存在同样的 `int` 溢出。
6. **APP-23 菜单 retranslate**：若采用 action registry 方案，需要 ui 模块的翻译文件（`.ts`）同步更新，属于 i18n 交叉项。
7. **APP-28 splash 资源**：`src/app/CMakeLists.txt` 与顶层 `CMakeLists.txt:246-256` 的 qml 拷贝逻辑建议删除（改用 qrc），涉及构建配置，请 Lead 统一决策。
8. **APP-30 crash dump + APP-16 自动保存**：需要引入新依赖（Crashpad/Sentry Native）与 `src/logger` 协作，属于全局基础设施，需要 Lead 拍板第三方库选型与体积预算。
9. **APP-37 高 DPI**：所有固定 px 的尺寸（标题栏 32、按钮 40×32、QSS padding）需要与 ui 模块的 `V*Page`/dock 宽度策略一起定"逻辑像素规范"，否则单改 app 无效。
