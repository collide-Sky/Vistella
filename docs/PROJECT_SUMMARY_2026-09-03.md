# MultiDoc 项目总结

> **截止日期**: 2026-09-03
> **范围**: 项目创建 → 第 0 阶段 (基础设施) 完成
> **总览**: Qt 6.11.1 + MSVC2022 + CMake, Windows 平台, 6 子目录分层

---

## 目录

1. [项目结构](#1-项目结构)
2. [模块与技术](#2-模块与技术)
3. [第三方库](#3-第三方库)
4. [多线程安全](#4-多线程安全)
5. [第 0 阶段之前的技术问题](#5-第-0-阶段之前的技术问题)
6. [第 0 阶段改造](#6-第-0-阶段改造)
7. [第 0 阶段技术问题 + 解决](#7-第-0-阶段技术问题--解决)
8. [成熟方案参考](#8-成熟方案参考)
9. [现状评估](#9-现状评估)
10. [优化空间 + 方案](#10-优化空间--方案)
11. [后续 9 阶段任务 (调整后 2026-09-03)](#11-后续-9-阶段任务-调整后-2026-09-03)
    - [11.0 重要架构澄清 (AI 助手 vs AI 工具)](#11-后续-9-阶段任务-调整后-2026-09-03)
12. [阶段间关联 + 并行开发建议](#12-阶段间关联--并行开发建议)
13. [优化内容分类](#13-优化内容分类)

---

## 1. 项目结构

```
D:\Collide\MyCode\QT6Projects\MultiDoc\
├── CMakeLists.txt              # 顶层
├── README.md
├── src/
│   ├── app/                    # 入口: main.cpp + MainWindow (QMainWindow 自绘无边框)
│   ├── ui/                     # 窗口类 (.ui XML)
│   ├── models/                 # RecentListModel (QAbstractTableModel)
│   ├── core/                   # 业务逻辑 (无 UI 依赖)
│   ├── media/                  # 图像处理 (OpenCV + QGraphicsView)
│   ├── ai/                     # 阶段 0 新增: AI 模型管理
│   ├── user/                   # 阶段 0 新增: 用户管理 (SQLite)
│   ├── network/                # 阶段 0 新增: 网络层 (AuthClient + HttpClient)
│   └── logger/                 # 独立 SHARED 库 (multidoc_logger.dll)
├── translatefile/              # i18n .ts / .qm (zh_CN / en_US / ja_JP)
├── third_party/
│   └── spdlog/                 # header-only 日志库
├── docs/                       # 文档 (本目录)
└── build/
    └── Desktop_Qt_6_11_1_MSVC2022_64bit_Debug/
        ├── MultiDoc.exe
        ├── multidoc_logger.dll
        ├── data/               # SQLite db
        ├── logs/               # 日志 (按日期切分)
        ├── qml/                # QML 启动屏
        ├── styles/             # QSS 主题
        ├── platforms/          # qwindows.dll
        ├── sqldrivers/         # qsqlited.dll
        ├── imageformats/       # qjpegd.dll 等
        ├── tls/                # qcertonlybackendd.dll
        └── *.dll               # Qt6Cored, Qt6Guid, ... (windeployqt 部署)
```

**目录分层原则**:
- `core/` 不依赖 `ui/` / `media/` / `app/` (反向依赖)
- `ui/` PRIVATE link `core` / `models` (允许用)
- `app/` PRIVATE link 所有其他子模块
- `logger/` **独立** SHARED 库, 任何模块都能 link (CMake `add_subdirectory`)

---

## 2. 模块与技术

### 2.1 `core/` - 业务核心

| 文件 | 技术 | 职责 |
|---|---|---|
| `ThemeManager.h/.cpp` | 单例 + QSS + `{{PLACEHOLDER}}` 替换 | 主题切换, accent/hover/text 配色, QSS 加载 |
| `SessionManager.h/.cpp` | QSettings + last-exit-clean flag | 会话恢复, 崩溃检测 (closeEvent 失败时下次启动弹"是否恢复") |
| `RecentManager.h/.cpp` | QSettings (QStringList) | 最近文件列表 (mode-aware 旧版本) |
| `LanguageManager.h/.cpp` | QTranslator + QLocale | i18n 切换 (zh_CN/en_US/ja_JP) |
| `FileExtensionRegistry.h/.cpp` | 静态 namespace + QHash | 决策 5: 4 大模块扩展名默认表 |
| `IModule.h` (纯头) | 纯虚接口 | IModule (工厂) + IWorkspace (实例) + ModuleInfo |
| `MediaDispatcher.h/.cpp` | 单例 + extMap | 中央文件路由 (扩展名 → 模块 → openFile) |
| `Logger.h` (在 logger/) | spdlog wrapper | 日志接口 (LOG_INFO/DEBUG/WARN/ERROR/CRITICAL) |

### 2.2 `ui/` - 窗口类

| 文件 | 技术 | 职责 |
|---|---|---|
| `MainWindow.h/.cpp/.ui` (在 app/) | QMainWindow 自绘无边框, 3 段 titleBar, 状态机 | 主窗口, 文档 tab, 菜单栏嵌入 titleBar, 4 边 4 角 resize, 多窗口状态机 (WindowMode enum) |
| `InfoTreeDock.h/.cpp/.ui` | QTreeView + FileTreeModel (Model/View 架构) | 左侧文件树, 懒加载, 扩展名过滤, 双击打开 |
| `FileTreeNodeData.h` (纯头) | POD struct | 节点数据 (fileName/absolutePath/isDir/iconKey/fileSize/lastModified/moduleId) |
| `FileTreeItem.h/.cpp` | 树节点类 | 父子关系 + lazy load 状态 |
| `FileTreeModel.h/.cpp` | QAbstractItemModel 子类 | index/parent/rowCount/data/hasChildren + lazy load + 扩展名过滤 |
| `HomePage.h/.cpp/.ui` | HomeModule enum (ImageWorker/AudioWorker/VideoWorker/VisionWorker) | 主页: 新建/打开/最近文件 + 4 模块按钮 |
| `DocWindow.h/.cpp/.ui` | DocType enum (Text/Image/Home) | 文档包装 (阶段 0: 只 Home + Text) |
| `SettingsDialog.h/.cpp/.ui` | QDialog + 3 QStackedWidget pages | 设置: 通用/语言/日志 |
| `ThemeGalleryDialog.h/.cpp/.ui` | 8 accent 颜色 + Light/Dark | 主题画廊 (阶段 0 骨架) |
| `LoginDialog.h/.cpp/.ui` | 5 Provider buttons (Local/GitHub/QQ/WeChat/Phone/Guest) | 登录对话框 (阶段 0 骨架) |

### 2.3 `models/` - 数据模型

| 文件 | 技术 | 职责 |
|---|---|---|
| `RecentListModel.h/.cpp` | QAbstractTableModel | 最近文件列表 (PinnedRole/FilePathRole 自定义 role) |

### 2.4 `media/` - 图像处理

| 文件 | 技术 | 职责 |
|---|---|---|
| `ImageWindow.h/.cpp/.ui` | QGraphicsView + QGraphicsScene + QUndoStack | 图像编辑窗口 (图层/拖拽/撤销) |
| `ImageProcessor.h/.cpp` | OpenCV 4.12 (cv::Mat) + QImage 互转 (深拷贝) | 图像处理算法 (mosaic, drawText, histograms, statistics) |
| `ImageEditCommand.h/.cpp` | QUndoCommand 子类 | 撤销栈 (移动/旋转/缩放) |
| `GraphicsTextItem.h/.cpp` | QGraphicsTextItem 子类 | 文字 item, 8 handle + 1 rotate 圆, 自绘不闪烁 |
| `imageeditor.cpp` | 主图像编辑逻辑 | filter/slider/mosaic 应用 |

### 2.5 `app/` - 入口

| 文件 | 技术 | 职责 |
|---|---|---|
| `main.cpp` | QApplication + QQuickView splash + MainWindow 栈变量 | 5s splash, LanguageManager/UserManager/AIManager/MediaDispatcher init, main 顶层 try/catch |
| `MainWindow.h/.cpp/.ui` | 见 2.2 | |

### 2.6 阶段 0 新增

| 子目录 | 文件 | 职责 |
|---|---|---|
| `core/` | IModule.h, MediaDispatcher.{h,cpp} | 4 大模块统一接口 + 中央路由 |
| `ai/` | IAIModel.h, AIManager.{h,cpp} | AI 模型注册/调用接口 (阶段 0 stub, 阶段 4+ 真模型) |
| `user/` | User.h, UserManager.{h,cpp}, UserDatabase.{h,cpp} | 用户状态机 (Guest/Authenticating/Authenticated/Offline) + SQLite 本地存储 |
| `network/` | AuthClient.{h,cpp}, HttpClient.{h,cpp} | OAuth 第三方登录 + 通用 HTTP (阶段 0 stub) |
| `ui/` | ThemeGalleryDialog, LoginDialog | 主题/登录 UI 骨架 |

### 2.7 `logger/` - 独立日志模块 (SHARED 库 `multidoc_logger.dll`)

- 内部用 spdlog 1.15 (header-only) + 自定义 `SizeDailySink`
- 双维度切分: 大小 (默认 4MB) + 日期 (跨日新开)
- **2026-09-03 修**: 智能切分, latest 文件 < 4MB 追加 (不开新文件); pattern `\n\n` 末尾
- Qt 集成 (LOG_INFO_QT 等) - 用 `QString.toStdString()` 走 UTF-8 字节流
- 导出宏: `LOGGER_API` (Windows `__declspec(dllexport/dllimport)`)
- 异步: `spdlog::async_logger` + thread pool (8192 queue, 1 thread)

---

## 3. 第三方库

| 库 | 版本 | 引入位置 | 用途 | 阶段 |
|---|---|---|---|---|
| spdlog | 1.15 (header-only) | `third_party/spdlog/` | 异步日志 | 阶段 0 之前 |
| OpenCV | 4.12.0 (官方 Windows binary) | `D:/Collide/opencv/build` (CMake find_package) | 图像处理 (cv::Mat ↔ QImage) | 阶段 0 之前 |
| SQLite | (Qt6::Sql 内置) | Qt 6.11.1 自带 | 用户数据库 (QSqlDatabase) | 阶段 0 |
| Qt 插件 | 6.11.1 | `D:/QT6/6.11.1/msvc2022_64/plugins/` | sqldrivers/qsqlited.dll + platforms/qwindowsd.dll + imageformats/*.dll + tls/*.dll | 阶段 0 (windeployqt 自动部署) |
| ~~Eigen 3~~ | ~~5.0.1~~ | ~~3D 模型~~ | ~~矩阵运算~~ | **已删** (3D 模块移除) |
| ~~Assimp~~ | ~~6.0.4~~ | ~~3D 模型导入~~ | ~~.stl/.obj/.fbx~~ | **已删** |
| ~~Qt3D~~ | ~~6.11.1~~ | ~~3D 渲染~~ | ~~OpenGL/D3D 渲染~~ | **已删** |

**未引入** (未来可能):
- vcpkg/Conan (包管理 - 当前 CMake find_package, 没跨平台)
- Catch2 / GoogleTest (单元测试)
- sphinx / doxygen (文档)
- ONNX Runtime (阶段 4+ AI 模型推理, 阶段 0 已在 AIManager 留 backendName="onnxruntime" 占位)

---

## 4. 多线程安全

| 组件 | 线程模型 | 安全性 |
|---|---|---|
| **spdlog 异步 logger** | 业务线程 push 到 queue, 1 个 worker 线程消费写文件 | ✅ 线程安全, async_overflow_policy::block (队列满阻塞, 不丢日志) |
| **QSqlDatabase (UserDatabase)** | 默认主线程 | ⚠️ 阶段 0 单线程 OK; 阶段 5+ 异步 DB 操作要 QSqlDatabase + 跨线程管理 (Qt 官方文档要求) |
| **QUndoStack (ImageEditCommand)** | 单线程 (UI thread) | ✅ OK |
| **FileTreeModel** | 单线程 (UI thread) | ✅ OK |
| **MediaDispatcher 单例** | 单线程, 锁保护 | ✅ std::shared_ptr + 单线程访问, 阶段 1+ 多线程访问要 mutex |
| **OpenCV cv::Mat** | 跨线程 OK, 但内存管理靠引用计数 | ⚠️ ImageWindow paint 是 UI thread, OpenCV 处理要放 worker thread |
| **UserManager 单例** | 单线程 | ✅ 阶段 0 OK; 阶段 5+ 跨线程要 mutex |
| **AIManager 单例** | 单线程 | ✅ 阶段 0 OK; 阶段 4+ 异步推理要 worker thread |
| **HttpClient** | 单线程 (阶段 0 stub) | ⚠️ 阶段 5+ 真正实现要 QNetworkAccessManager + 异步 callback, 不要在主线程做同步网络 IO |

**未来多线程策略**:
- **图像处理 worker thread**: ImageWindow 操作放后台线程, 通过信号槽回主线程更新 view
- **AI 推理 worker thread**: AIManager::submitAsync 排到 worker, 推理完成 emit inferFinished
- **网络请求 worker**: QNetworkAccessManager 自带异步, 但要管理好 lifetime (QPointer 跟踪)
- **DB 操作**: QSqlDatabase 每个线程一个 connection, 跨线程通过信号槽

---

## 5. 第 0 阶段之前的技术问题

| 问题 | 解决 |
|---|---|
| 旧 3D 模块占用大量代码 + 编译时间 | 完整移除 `src/media3d/`, 删除 Eigen/Assimp/Qt3D link, 删除相关 CMake |
| 早期 Unicode 字符散落 (505 entries) | 统一转中文, 写入 .cpp/.h 字符字面量 (`/utf-8` 编译标志) |
| QGraphicsView 文字编辑手柄闪烁 | 重构为 GraphicsTextItem (QGraphicsTextItem 子类), paint 8 handle + 1 rotate 圆, override shape() |
| QGraphicsTextItem 拖拽 setPos/setTransform 边界 | m_dragStartTransform 记录起始 transform, resize/rotate 用 deltaT * m_dragStartTransform (避免 Qt origin trap) |
| Qt 6 setTransformOriginPoint 在 onlyTransform 模式下不生效 | setTransformOriginPoint + deltaT matrix 双修, 显式 baked into m_transform |
| OpenCV ↔ QImage 互转内存管理 | 深拷贝 (.copy()), 不用 constBits() 共享 buffer |
| OpenCV cv::Mat 转 QImage RGB888 step 不一致 | 强制 `step = width * 3` |
| QTreeWidget vs Model/View 选型 | 选 Model/View: FileTreeNodeData (POD) + FileTreeItem (节点) + FileTreeModel (QAbstractItemModel) |
| 文件树扩展名过滤 | FileExtensionRegistry::isSupportedExtension, scanChildren 跳过不在表里的文件 |
| 自绘无边框标题栏 (frameless) | setWindowFlags(Qt::FramelessWindowHint), 3 段 (left/mid/right) 嵌入 setMenuWidget |
| 多文档编辑 (QTabWidget) | DocWindow 包装, 3 DocType (Text/Image/Home), tabMoved 强制 Home 在 index 0 |
| i18n (zh_CN/en_US/ja_JP) | Qt Linguist 工具链, qt_add_translations 嵌入 .qm, LanguageManager 切换 |
| 主题 (Light/Dark + 8 accent) | QSS 文件 + `{{PLACEHOLDER}}` 替换, ThemeManager 单例 |
| 会话恢复 + 崩溃检测 | SessionManager.setLastExitClean(true) 在 closeEvent, false 在 main 开始; 启动读 isLastExitClean |
| 图片信息面板 (左: 滤镜 + 直方图, 右: 统计) | 10 组左侧 dock + 右侧 dock, ImageProcessor::computeStats 算 stats |
| Mosaic 实现 | cv::Mat ROI + cv::blur, 5-120 笔刷, toggle 模式, QImage ↔ cv::Mat 互转 |
| 文字 item 拖拽 setTransform origin drift | 显式 mapToItem + setTransformOriginPoint + deltaT double origin fix |
| QGraphicsItem 多步变换累积位置漂移 | 用 scenePos (item 自身位置) 而非鼠标 scene 位置 |

---

## 6. 第 0 阶段改造 (2026-09-02 ~ 2026-09-03)

### 6.1 主要改动

1. **接口抽象**:
   - `IModule.h`: IModule (工厂) + IWorkspace (实例) + ModuleInfo (id/name/extensions/iconPath)
   - 4 大模块: imageWorker / audioWorker / videoWorker / visionWorker
   - 4 大模块共享 imageWorker 的扩展名 (visionWorker 跑在图像上)

2. **中央路由**:
   - `MediaDispatcher` 单例: 扩展名 → 模块 → openFile, 支持 openFile(path) + openModuleById(id), openFailed 信号

3. **AI 服务**:
   - `IAIModel` (Status enum, inferSync/submitAsync)
   - `AIManager` 单例 (backendName="onnxruntime" 占位)

4. **用户系统**:
   - `UserManager` 状态机 (Guest/Authenticating/Authenticated/Offline)
   - `UserDatabase` SQLite CRUD (users/tokens/login_logs 表)
   - `init(dbPath)` 初始化, `tryAutoLogin` (阶段 5+)

5. **网络层**:
   - `AuthClient` Provider enum (Local/GitHub/QQ/WeChat/Phone)
   - `HttpClient` singleton (Response struct, getAsync/postAsync)
   - 阶段 0 都是 stub, 阶段 5+ 真正实现

6. **UI 骨架**:
   - `ThemeGalleryDialog`: 8 accent 颜色 + Light/Dark radio, 阶段 5+ 真接 ThemeManager
   - `LoginDialog`: 5 Provider 按钮, providerChosen 信号

7. **窗口状态机重做**:
   - `enum class WindowMode { Normal, Maximized }`
   - `setMode/toggleMode` 统一入口, 解决偶发失效
   - 4 边 4 角 resize 用 Qt 6 `QWindow::startSystemResize`
   - 1280x800 切 normal, 居中当前屏 (QGuiApplication::screenAt + moveEvent 跟踪)
   - QSettings 持久化 mode/normalSize/normalPos

8. **多屏支持**:
   - m_screenName + m_screenGeometry + resolveTargetScreen
   - showOnSavedScreen 启动入口
   - **后来撤掉** (用户反馈, 改回"鼠标在哪儿启动在哪儿" + normal 用 screenAt)

9. **崩溃修复**:
   - 构造不调 showMaximized (Qt 反模式, 触发 DWrite first-paint 异常)
   - 5s timer 决定何时 showMaximized/showNormal
   - 栈 MainWindow (替代 new, 解决 COM refcount 异常)
   - main 顶层 try/catch + lambda try/catch
   - `qputenv("QT_QUICK_BACKEND", "software")` (绕开 Intel D3D11On12 异常)
   - `QApplication::setStyle("Fusion")` (绕开 DWrite 字体渲染)

10. **基础设施**:
    - `qApp->installEventFilter` 处理子 widget 覆盖区域 hover 改 cursor (QWindow::setCursor native window cursor)
    - 边缘检测 6px 阈值, mousePressEvent 边缘优先于 titleBar
    - windeployqt POST_BUILD 部署 sqldrivers / platforms / imageformats / tls
    - 日志智能切分 (latest < 4MB 追加), pattern `\n\n` 末尾

### 6.2 main.cpp 顶层 try/catch + 栈 MainWindow

```cpp
int main(int argc, char *argv[]) {
    qputenv("QT_QUICK_BACKEND", "software");  // 绕开 Intel D3D11On12
    QApplication::setStyle("Fusion");        // 绕开 DWrite 字体渲染
    QApplication a(argc, argv);
    a.setFont(QFont("Segoe UI", 9));         // 简单字体, 减少 DWrite bug
    // ... 业务初始化 ...
    MainWindow w;  // 栈, 不用 new (解决 COM refcount)
    w.hide();
    QTimer::singleShot(5000, &splash, [&splash, &w]() {
        try {
            splash.close();
            if (w.mode() == WindowMode::Maximized) w.showMaximized();
            else w.showNormal();
        } catch (...) { /* log + continue */ }
    });
    int ret = a.exec();
    Logger::shutdown();
    return ret;
}
```

---

## 7. 第 0 阶段技术问题 + 解决

| # | 问题 | 根因 | 解决 |
|---|---|---|---|
| 1 | Driver not loaded (QSQLITE) | 缺 sqldrivers/qsqlited.dll | windeployqt POST_BUILD 部署 |
| 2 | 标题栏双击偶发失效 | m_isMaximized bool 跟 Qt WindowState 不同步 | WindowMode enum 状态机重做 |
| 3 | 拖拽恢复 normal 不生效 | setMode 内 setGeometry 顺序错 (maximized 状态下被忽略) | setMode 内部: `showNormal()` **先**, `setGeometry()` **后** |
| 4 | Normal 跳到主屏 | `screen()` 在 Maximized 时返回 primary | `QGuiApplication::screenAt(frameGeometry().center())` + `moveEvent` 跟踪 m_normalPos |
| 5 | 4 边 4 角 cursor 不立刻改 | mouseMoveEvent 收不到子 widget 覆盖区域 | `qApp->installEventFilter` + `QWindow::setCursor` (native window cursor) |
| 6 | top / left 边不拉伸 | mousePressEvent 顺序: titleBarDragRect 先, 命中 6px 顶边 | mousePressEvent 顺序倒: **hitTestEdge 先**, titleBarDragRect 后 |
| 7 | DWrite first paint 异常 (0xe06d7363) | Qt DWrite font engine + Intel 驱动 | `setStyle("Fusion")` + `setFont("Segoe UI", 9)` (无法完全消除, 但 first chance 不 abort) |
| 8 | DWrite `%n` = logger name 误用 | spdlog pattern `%n` 不是 newline | 改用 literal `\n\n` in pattern |
| 9 | Driver crashed by showMaximized in constructor | widget 未完全构造就 maximize, first paint DWrite 异常 | 构造不调 showMaximized, 5s timer 决定 |
| 10 | 栈 MainWindow 没 delete → COM refcount 异常 | `new MainWindow` 没人 delete | 栈 `MainWindow w`, 析构顺序 w → splash → a |
| 11 | UIPFullx64!DllCanUnloadNow 异常 | 进程退出 COM refcount 不归零 | main 顶层 try/catch + lambda try/catch |
| 12 | 0ms warm up timer 无效 | show + hide 立即返回, paint 没真正发生 | 删掉, 用 Fusion style |
| 13 | 程序启动 + showOnSavedScreen 崩 | widget 未 show 过时 setGeometry + showMaximized 触发 first paint 异常 | 撤掉 showOnSavedScreen, 改回 `w.showMaximized()` |
| 14 | spdlog `%n%n` 误用为 newline | spdlog pattern 文档误解 | 改用 literal `\n\n` 字符 |
| 15 | 日志每次启动新文件 | `openNewFile_` 总是 `splitIndex_ = existingMax + 1` | 检查 latest size, < maxFileSize 追加; 调 `fileHelper_.open(path, false)` (append 模式) |
| 16 | 日志无换行 | pattern 没 newline | pattern 末尾 `\n\n` (literal 字符) |
| 17 | 文件树展开触发折叠 | `beginRemoveRows` 没包占位 child 删除 | 显式 `beginRemoveRows(indexForItem(item), 0, 0) + removeAllChildren + endRemoveRows` |
| 18 | 多层展开报错 | `beginInsertRows` 用 invalid parent | 加 `indexForItem(FileTreeItem*)` helper, `beginInsertRows(parentIdx, ...)` |
| 19 | Qt vs WPS 风格对齐 | 闪烁和异常 | WPS 也用 software 渲染 + 状态机; 项目对标 WPS 解决 |
| 20 | spdlog `%n` 是 logger name | 文档误读 | 改用 literal `\n` |

---

## 8. 成熟方案参考

| 需求 | 成熟方案 | 项目当前方案 | 差距 |
|---|---|---|---|
| DWrite 异常 (Windows only) | 用 DComposition / Direct2D 替代 DWrite; Qt Quick 用 D3D RHI | Fusion style + 简单字体 (不完美) | 仍有 first-chance, 不 abort |
| COM 异常 (UIPFullx64) | CoIncrementMTAUsage + 显式 CoUninitialize | 栈 MainWindow + try/catch (够用) | OK |
| 日志按大小切分 | spdlog::sinks::rotating_file_sink (按 size 切多个) | 自定义 SizeDailySink + 智能追加 | 自己实现, 行为接近 spdlog 标准 |
| 窗口状态保存 | QMainWindow::saveState/restoreState (built-in) | QSettings mode/normalSize/normalPos | 自定义, 跨平台一致 |
| 多屏处理 | Qt 6 QScreen::virtualGeometry + QWindow::setScreen | screenAt + moveEvent (轻量) | OK, 跨屏行为靠 Qt |
| i18n | Qt Linguist + tr() + QTranslator | Qt 标准 (zh_CN/en_US/ja_JP) | OK |
| Model/View | Qt QAbstractItemModel + QSortFilterProxyModel | 自定义 FileTreeModel + FileExtensionRegistry 过滤 | OK |
| 图像编辑 | Qt Graphics View + OpenCV | 同 | OK, 阶段 1+ 扩 |
| 异步日志 | spdlog async + thread pool | spdlog (8192 queue, 1 thread) | OK |
| 撤销/重做 | QUndoStack + QUndoCommand | 同 | OK |
| OAuth 第三方登录 | QtNetworkAuth (Qt 6.5+) | AuthClient stub | 阶段 5+ 用 QtNetworkAuth 简化 |
| AI 模型推理 | ONNX Runtime + DirectML / CUDA | AIManager stub (backendName="onnxruntime" 占位) | 阶段 4+ 接入 |
| 文件浏览器 | Qt QFileSystemModel + QSortFilterProxyModel | 自定义 FileTreeModel (扩展名过滤) | OK, 自定义更灵活 |
| IDE frameless 窗口 | VSCode / Qt Creator frameless | 自定义 3 段 titleBar | OK, 风格对标 WPS/VSCode |
| 跨平台字体 | QFontDatabase 查可用字体 | 固定 "Segoe UI" | 阶段 1+ 加 fallback |

**结论**: 项目的核心架构 (Qt 6 + Model/View + 异步日志 + frameless 窗口) 跟成熟方案对齐; 状态机、cursor 处理、edge resize 是**典型 Qt frameless 实现**, 跟 VSCode/Qt Creator/WPS 类似。

---

## 9. 现状评估

| 维度 | 水平 | 说明 |
|---|---|---|
| **跨平台兼容性** | **中** | Qt 6 跨平台基础 OK, 但 OpenCV 是 Windows binary, 字体用 Segoe UI (Windows-centric), 用了 `_WIN32` macro; Linux/macOS 未验证 |
| **交叉编译** | **低** | CMake + Ninja 配置 Windows-only, OpenCV 不能 cross-compile, 无 vcpkg/Conan 工具链; Qt for Android/iOS 需另配 |
| **代码可移植性** | **中** | Qt 跨平台 API, 但部分 platform-specific (Windows 字体, _WIN32 macro), 路径用 `./logs` 相对路径 (应该用 QStandardPaths) |
| **运行稳定性** | **中-高** | 修了 DWrite/UIPFullx64/spdlog 异常, 栈 MainWindow + try/catch; 仍有 first-chance 异常显示, 不 abort |
| **性能** | **中** | 主线程 paint + 业务; spdlog 异步; 图像处理用 OpenCV 但同步; 没独立渲染线程; 没图像缓存 (LOD); ImageWindow paint 频繁重画 |
| **代码可维护性** | **中** | 模块化 OK, 但模块边界有交叉 (core/ui 互引); 缺少单元测试; magic string 多 (QSS / QML) |
| **i18n 完整度** | **中** | 3 语言, 自动化 lupdate/lrelease, 但翻译覆盖率 100% (505 entries 全部翻译) |
| **主题系统** | **低-中** | 8 accent + Light/Dark, 但 QSS 是大字符串没拆, 改色全靠 `{{PLACEHOLDER}}` 替换 |
| **测试覆盖** | **0** | 没单元测试, 集成测试 |
| **CI/CD** | **0** | 没 GitHub Actions / 自动 build |
| **文档** | **低** | 仅 README.md, 没 API 文档, 架构文档 (本文件是新增) |

---

## 10. 优化空间 + 方案

### 10.1 高优先级 (阶段 1-2 必做)

**1. OpenCV 异步化**
- 现状: cv::imread / cv::blur 都在主线程, 大图卡顿
- 优化: 用 `QThreadPool::globalInstance()->start(QRunnable*)` 异步处理, 完成通过信号槽回主线程更新 view
- 工作量: 1-2 天 (封装 QFutureWatcher + QImage result)

**2. 图像处理性能**
- 现状: ImageWindow paint 频繁重画全图
- 优化: 加 QGraphicsItem 缓存层 + 缩略图 (LOD) + invalidate 只重画 dirty region
- 工作量: 1 周

**3. 错误处理统一**
- 现状: 函数返回 bool 或 nullptr, 错误信息散落 qWarning
- 优化: 用 `QPair<T, QString>` (result, error) 或 `std::expected<T, Error>` (C++23)
- 工作量: 1 周 (全 codebase 重构)

**4. 跨平台路径**
- 现状: 硬编码 `./logs`, `./data`, `/qml/splash_screen.qml`
- 优化: `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` + 资源用 qrc
- 工作量: 0.5 天

**0. DWrite 异常成熟修法** ⭐ (阶段 0 已知问题, 阶段 1 开始**前**修)
- **现状**: 阶段 0 调试时 DWrite first-paint 抛 0xe06d7363 C++ exception (Intel 驱动 / Qt DWrite font engine bug)
- **根因**: Qt 6 默认走 DWrite (DirectWrite) 字体渲染 pipeline, 在 Intel 集成显卡驱动 + 特定字体下 first paint 抛 C++ exception
- **成熟方案 (Qt 6 官方推荐 + 社区验证)**:
  1. **Style 走 Fusion (Qt 100% software 渲染, 不调 native DWM/D2D/DWrite)**
     ```cpp
     a.setStyle(QStringLiteral("Fusion"));
     ```
  2. **RHI backend 走 software (Qt 6 推荐方式)**
     ```cpp
     qputenv("QT_QUICK_BACKEND", "software");
     qputenv("QSG_RHI_BACKEND", "software");
     ```
  3. **Font 简单 (Arial 跨平台, 不调 DWrite hinting bug 路径)**
     ```cpp
     QApplication::setFont(QFont(QStringLiteral("Arial"), 9));
     ```
  4. **关键 UI 元素 (主窗口 splash / 启动屏 / 复杂动画) 用 QtQuick + QML**, 不调 QtWidget paint path
     - 阶段 1+ 考虑: ImageWindow 改 QQuickWidget 渲染, 文字窗口改 QQuickWidget
  5. **避免 paintEvent 频繁触发**: `setUpdatesEnabled(false)` 包裹大操作, `viewport()->setAttribute(Qt::WA_StaticContentsOptimized)`
  6. **环境变量** (Windows 特定):
     ```cpp
     qputenv("QT_OPENGL", "software");  // Qt 5, 6 已 deprecated
     ```
- **集成位置**: main.cpp 早期 (QApplication 构造后立即)
- **工作量**: 半天 (改 main.cpp + 测试启动 + 验证无 first-chance 异常)
- **参考**: Qt 6 docs "Platform Integration > Fonts > Font Matching", Qt 6 `QFont::StyleHint::SansSerif`, WPS 内部渲染架构

### 10.2 中优先级 (阶段 3-4 必做)

**5. OpenCV 包管理**
- 现状: `D:/Collide/opencv/build` 硬编码, 跨平台失败
- 优化: 引入 vcpkg (`vcpkg install opencv:x64-windows`), CMake `find_package(OpenCV CONFIG)` 自动找
- 工作量: 1 天 (改 CMake + 测试)

**6. 字体跨平台**
- 现状: `Segoe UI` Windows-only
- 优化: `QFontDatabase::families()` 列出可用, 选第一个 sans-serif 作 fallback
- 工作量: 0.5 天

**7. 单元测试**
- 现状: 0 测试
- 优化: 引入 Qt Test (QTest) + 测 FileExtensionRegistry / MediaDispatcher / UserManager 状态机
- 工作量: 1 周

**8. CI/CD**
- 现状: 手 build
- 优化: GitHub Actions 跨平台 build matrix (Windows / Linux / macOS)
- 工作量: 1 天

### 10.3 低优先级 (阶段 5-7 必做)

**9. OpenGL 渲染**
- 阶段 1+ 考虑用 QOpenGLWidget / QQuickItem 替代 QWidget paint, 性能更好
- 工作量: 2 周

**10. 撤销/重做异步化**
- 大型图像操作 (mosaic, filter) 撤销栈要支持异步
- 用 QPromise / QFuture 模式
- 工作量: 1 周

**11. 主题市场**
- 阶段 6+: 主题 JSON 描述符 + 动态加载
- 工作量: 1 周

**12. 跨平台测试**
- CI 跑 Linux/macOS build + 截图回归
- 工作量: 1 周

### 10.4 安全 / 性能 trade-off

- 跨平台字体 fallback: Windows Segoe UI / macOS San Francisco / Linux Ubuntu → 视觉一致
- 图像处理: 异步 vs 内存 (worker 线程增加 50-100MB 峰值)
- 多屏: 跨屏 dock 位置存储 → 增加 ~1KB QSettings

### 10.5 优化方案 (执行顺序)

**Phase A (阶段 1)**:
- [ ] 4. 跨平台路径 (0.5 天)
- [ ] 6. 字体跨平台 (0.5 天)
- [ ] 1. OpenCV 异步化 (1-2 天)
- [ ] 3. 错误处理统一 - 试点 FileExtensionRegistry (1 天)

**Phase B (阶段 2-3)**:
- [ ] 5. vcpkg 引入 (1 天)
- [ ] 7. 单元测试 - 覆盖 MediaDispatcher (3 天)
- [ ] 8. CI/CD (1 天)
- [ ] 2. 图像处理性能 (1 周)

**Phase C (阶段 5+)**:
- [ ] 9. OpenGL 渲染 (2 周, 可选)
- [ ] 10. 撤销/重做异步化 (1 周)
- [ ] 11. 主题市场 (1 周)
- [ ] 12. 跨平台测试 (1 周)

---

## 11. 后续 9 阶段任务 (调整后 2026-09-03)

> ⚠️ **2026-09-03 调整**: 用户考虑后调整
> - **阶段 5 (用户管理) 推迟到最后** - 涉及服务器 / 第三方 API 申请 (QQ/微信需公司资质) / 时间和金钱成本
> - **AI 助手 (阶段 5 新) 提前** - 主要是 AI 对话 + 历史记录, 不一定需要用户系统
> - **主题拆 2 阶段**: 阶段 6 (本地纯色/渐变) + 阶段 9 (在线主题市场, 最后)
> - **横向 AI 工具 (YOLO 等) 推迟** - 集成 vs 服务器部署待定
> - **DWrite 异常修法**: 阶段 0 已知问题, 阶段 1 开始**前**按 Qt 6 成熟方案修
>
> **阶段 0 任务已完成**: IModule/IWorkspace, MediaDispatcher, IAIModel/AIManager, UserManager/UserDatabase, onNewFile, InfoTreeDock, main.cpp init, ThemeGalleryDialog, LoginDialog, src/ai+user+network CMakeLists

### 阶段 1: 图像系统 (imageWorker) - 4 周

**目标**: 完整 PS + Lr 风格图像处理, 取代现有 ImageWindow 临时实现

**任务**:
- [ ] **图层系统** (1 周)
  - [ ] Layer 基类 + LayerGroup + LayerMask
  - [ ] 图层面板 UI (左 dock)
  - [ ] 拖拽排序 / 可见性 / 锁定 / 不透明度
  - [ ] 图层混合模式 (Normal/Multiply/Screen/Overlay/Darken/Lighten/...)
  - [ ] 智能对象 (Smart Object) - 嵌套图像对象
- [ ] **选择工具** (1 周)
  - [ ] 矩形 / 椭圆 / 套索 / 多边形 / 魔棒 / 快速选择
  - [ ] 选区布尔运算 (Union/Subtract/Intersect)
  - [ ] 选区变换 (移动/缩放/旋转/羽化)
  - [ ] 选区反选 / 羽化 / 扩展 / 收缩 / 平滑
  - [ ] 钢笔工具 (路径编辑)
- [ ] **滤镜** (1 周)
  - [ ] Blur (Gaussian/Motion/Box/Radial)
  - [ ] Sharpen (Unsharp Mask / High Pass)
  - [ ] Distort (Warp/Liquify/Perspective)
  - [ ] Stylize (Emboss/Find Edges/Oil Paint)
  - [ ] Noise (Add Noise/Reduce Noise/Median)
- [ ] **色彩调整** (1 周)
  - [ ] Levels / Curves / Brightness/Contrast
  - [ ] Hue/Saturation / Vibrance / Color Balance
  - [ ] White Balance (Temp/Tint)
  - [ ] HSL / Lab / CMYK 色彩空间切换
  - [ ] Camera Raw (DNG/RAW 解码)

**验收**: 能完整处理一张 24MP RAW 照片, 全部 layer/filter 流畅 60fps

### 阶段 2: 音频系统 (audioWorker) - 3 周

**目标**: Audition + ffmpeg 风格音频处理

**任务**:
- [ ] **FFmpeg 集成** (3 天)
  - [ ] 集成 ffmpeg 6.x (vcpkg `ffmpeg`)
  - [ ] 解码 WAV/MP3/FLAC/AAC/OGG
  - [ ] 编码 WAV/MP3/AAC
  - [ ] 多轨时间线
- [ ] **波形显示** (1 周)
  - [ ] QGraphicsView 多轨波形
  - [ ] 缩放 (毫秒 ↔ 分钟)
  - [ ] 标记 (cue points) / 区域 (regions) / 循环
  - [ ] 频谱图 (spectrogram) - 短时傅里叶变换
- [ ] **编辑操作** (1 周)
  - [ ] 剪切 / 复制 / 粘贴 / 删除 / 静音
  - [ ] 淡入淡出 / 渐变 / 标准化
  - [ ] 降噪 (spectral subtraction / wiener filter)
  - [ ] 均衡器 (parametric EQ)
  - [ ] 压缩 / 限制 / 门限
- [ ] **效果** (4 天)
  - [ ] 混响 (reverb)
  - [ ] 延迟 (delay/echo)
  - [ ] 合唱 (chorus)
  - [ ] 失真 (distortion)
  - [ ] 立体声处理 (pan/width)

**验收**: 能处理 5 轨混音, 实时混音延迟 < 20ms

### 阶段 3: 视频系统 (videoWorker) - 4 周

**目标**: Premiere Pro + 剪映风格视频编辑

**任务**:
- [ ] **FFmpeg 视频解码** (3 天)
  - [ ] 解码 MP4/MKV/AVI/MOV/WebM
  - [ ] 硬件解码 (D3D11VA / NVDEC / VideoToolbox)
  - [ ] 缩略图生成 (每隔 N 帧)
- [ ] **时间线** (1 周)
  - [ ] 多轨时间线 (视频/音频/字幕/特效)
  - [ ] 拖拽剪辑 / 修剪入出点 / 波纹编辑
  - [ ] 关键帧 (keyframes) - 位置/缩放/透明度
  - [ ] 嵌套 (nesting) / 复合剪辑
- [ ] **转场** (1 周)
  - [ ] 交叉溶解 (cross dissolve)
  - [ ] 滑动 (slide) - 8 方向
  - [ ] 擦除 (wipe) - 几何 / 渐变
  - [ ] 推拉 (push) / 翻页 (page turn)
  - [ ] 3D 转场 (cube/flip)
- [ ] **效果** (1 周)
  - [ ] 颜色分级 (color grading)
  - [ ] 模糊 / 锐化
  - [ ] 稳像 (stabilization) - 光流
  - [ ] 速度变化 (speed ramp) - 光流插帧
  - [ ] 绿幕 / chroma key
- [ ] **导出** (4 天)
  - [ ] H.264 / H.265 (硬件编码 NVENC/QSV)
  - [ ] 代理 (proxy) 编辑 - 1/2 / 1/4 分辨率
  - [ ] 字幕烧录 / 单独字幕轨

**验收**: 4K 60fps 多轨时间线编辑, 实时预览 30fps

### 阶段 4: 工业视觉系统 (visionWorker) - 3 周

**目标**: YOLO + 工业检测 + OCR

**任务**:
- [ ] **ONNX Runtime 集成** (3 天)
  - [ ] 集成 onnxruntime (vcpkg)
  - [ ] DirectML (Windows) / CUDA (Linux) 后端
  - [ ] 模型加载 / 推理 / 异步推理队列
- [ ] **目标检测** (1 周)
  - [ ] YOLOv8 / YOLOv9 集成
  - [ ] 预训练模型 (COCO 80 类)
  - [ ] 自定义训练 (YOLO 格式标注)
  - [ ] 实时摄像头检测
- [ ] **工业检测** (1 周)
  - [ ] 缺陷检测 (划痕/凹陷/色差)
  - [ ] 尺寸测量 (像素 → 物理单位, 标定)
  - [ ] 模板匹配 (template matching)
  - [ ] 圆 / 直线 / 矩形检测 (霍夫变换)
  - [ ] OCR (Tesseract / PaddleOCR)
- [ ] **数据标注工具** (3 天)
  - [ ] 矩形 / 多边形 / 关键点标注
  - [ ] 类别管理
  - [ ] 导出 YOLO / COCO / VOC 格式

**验收**: 实时摄像头 30fps YOLOv8 检测, 工业缺陷检出率 > 95%

### 阶段 5: AI 助手 (调整: 用户管理之前, 2026-09-03; **云 API 风格, 类似 ChatGPT**) - 2 周

**目标**: 项目级 AI 助手, 类似 ChatGPT 体验 - 自然语言对话 + 历史记录, **通过配置 API 调云端 LLM**, **不依赖用户系统** (本地 SQLite 存历史)

**定位澄清** (2026-09-03 用户反馈):
- AI 助手 ≠ AI 工具. AI 助手是"项目级对话" (用户问, AI 答)
- AI 助手**走云 API** (OpenAI / Claude / DeepSeek / 通义千问 等)
- AI 工具 (YOLO/Real-ESRGAN/Whisper 等) 走**本地集成**, 在 4 大 Worker **内部**作处理功能, 见阶段 7
- AI 助手**不**调用 AI 工具 (Function Calling 阶段 5 不做, 阶段 8+ 单独做)

**任务**:
- [ ] **LLM API 集成** (1 周) - 成熟方案: OpenAI 兼容协议 (HTTP + SSE)
  - [ ] 支持 OpenAI / DeepSeek / 通义千问 / 智谱 GLM (都兼容 OpenAI 协议)
  - [ ] SettingsDialog 加 "AI 助手" 标签: API endpoint + API key + 模型名 (gpt-4o-mini / deepseek-chat / qwen-turbo)
  - [ ] HttpClient 加 streaming POST (SSE 解析)
  - [ ] Bearer Token 认证
- [ ] **AI 助手 UI** (4 天)
  - [ ] 对话窗口 (右下角 dock 或独立窗口)
  - [ ] 流式输出 (token-by-token 显示, SSE 增量)
  - [ ] Markdown 渲染 (QTextBrowser + 简单解析)
  - [ ] 代码块语法高亮 (Pygments 高亮方案 / 自写简化版)
  - [ ] 历史记录 (本地 SQLite, 不依赖用户系统)
- [ ] **历史记录 + 上下文** (3 天)
  - [ ] SQLite 表: ai_conversations (id/title/created_at/updated_at) + ai_messages (id/conversation_id/role/content/timestamp)
  - [ ] 多会话切换
  - [ ] 上下文窗口 (最近 N 条消息, 默认 10)
  - [ ] 上下文长度可配置 (1k-32k tokens)

**验收**:
- 配置 DeepSeek / OpenAI API key 后能对话
- 流式输出 < 500ms 延迟
- 历史记录保存到本地 SQLite
- 不需要用户系统 (API key 在 SettingsDialog 配置)

**API 推荐 (用户选, 按成本/能力)**:
- **DeepSeek-chat** (国内, ¥1/M tokens, 推荐起步)
- **OpenAI gpt-4o-mini** ($0.15/M tokens)
- **通义千问 qwen-turbo** (国内, ¥0.8/M tokens)
- **智谱 GLM-4-Flash** (国内, ¥0.1/M tokens, 免费额度)

### 阶段 6: 本地主题系统 (调整: 不含在线市场, 2026-09-03) - 1 周

**目标**: 主题画廊完整实现 + 本地主题, **不绑定付费/在线**, 纯色 + 多色渐变

**任务**:
- [ ] **主题模型** (2 天) - 成熟方案: 主题 JSON 描述符
  - [ ] 数据结构: `theme.json` (id/name/dark/light/accent + 渐变定义)
  - [ ] 渐变: linear-gradient / radial-gradient, 3-5 段色
  - [ ] 加载 / 保存 / 切换
- [ ] **ThemeGalleryDialog 完整实现** (2 天) - 替换阶段 0 骨架
  - [ ] 接 ThemeManager.setAccentColor + applyTheme
  - [ ] 主题预览 (实时)
  - [ ] 内置主题: 5 纯色 + 5 渐变 = 10 个
- [ ] **应用主题** (1 天)
  - [ ] QSS 模板支持渐变 (background: qlineargradient(...))
  - [ ] ThemeManager.applyTheme() 替换占位符 + 加载渐变
  - [ ] 主题切换 emit themeChanged → MainWindow.applyTheme

**验收**: 10+ 内置主题 (5 纯色 + 5 渐变), 切换实时, 主题持久化

### 阶段 7: 嵌入式 AI 工具 (YOLO 等, 调整: **全部本地集成进项目**, 2026-09-03) - 3 周

**目标**: 4 大模块 **内部**的 AI 处理功能, **全部本地 ONNX Runtime 推理**, 作为 Worker 的"AI 工具"按钮

**定位澄清** (2026-09-03 用户反馈):
- AI 工具 ≠ AI 助手. AI 工具是 **Worker 内部功能** (image/audio/video/visionWorker 各有 AI 按钮)
- AI 工具**全部本地集成** (ONNX Runtime + OpenCV DNN), 不走云 API
- AI 助手 (阶段 5) 走**云 API**, 跟 AI 工具是**两个不同层**

**架构** (成熟方案):
```
[imageWorker] [audioWorker] [videoWorker] [visionWorker]
        ↓           ↓            ↓            ↓
   ONNX Runtime (本地推理)  ← AIManager 统一调度
        ↓
   模型文件: models/yolov8n.onnx, realesrgan-x4.onnx, sam-vit-b.onnx, whisper-tiny.onnx
```

**任务**:
- [ ] **ONNX Runtime 集成** (3 天) - 成熟方案: vcpkg + DirectML/CUDA
  - [ ] vcpkg `onnxruntime:x64-windows` (含 DirectML EP)
  - [ ] AIManager 加 OnnxRuntimeBackend
  - [ ] 模型加载 / 推理 / 异步推理队列
  - [ ] GPU 内存管理 (模型分页)
- [ ] **visionWorker AI 工具** (1 周) - YOLO 等目标检测
  - [ ] YOLOv8n 集成 (ONNX, 模型 6MB, CPU 30fps)
  - [ ] 预训练模型 (COCO 80 类)
  - [ ] 实时摄像头检测
  - [ ] 工业检测 (缺陷/尺寸/OCR)
- [ ] **imageWorker AI 工具** (1 周) - 超分 / 抠图 / 修复
  - [ ] Real-ESRGAN-x4 (ONNX, 模型 64MB, 2x 超分)
  - [ ] SAM (Segment Anything) 抠图 (ONNX, 模型 375MB)
  - [ ] GFPGAN 老照片修复 (ONNX, 模型 332MB)
  - [ ] 模型懒加载 (首次使用时下载)
- [ ] **audioWorker AI 工具** (3 天) - 语音转文字 / 降噪
  - [ ] Whisper-tiny (ONNX, 模型 39MB, 实时)
  - [ ] RNNoise 降噪 (轻量, CPU 实时)
- [ ] **videoWorker AI 工具** (3 天, 阶段 7 后期) - 视频 AI
  - [ ] Real-ESRGAN 视频超分 (逐帧 + 时序平滑)
  - [ ] 字幕生成 (Whisper + 时间轴对齐)

**验收**:
- YOLOv8n CPU 实时 30fps
- Real-ESRGAN-x4 2x 超分 24MP 图像 < 10s
- Whisper-tiny 中文识别实时
- 所有 AI 工具在 Worker UI 里有按钮, **不**走 AI 助手

**模型分发**:
- 内置 yolov8n.onnx (6MB), 启动时复制到 `<AppData>/models/`
- 重量模型 (Real-ESRGAN/SAM/GFPGAN) 首次使用时下载, 显示进度条
- 模型清单 JSON: `models-manifest.json` (URL + MD5 + size)

### 阶段 8: 用户管理系统 (调整: 推迟到最后, 2026-09-03) - 2 周

**目标**: 完整 OAuth 第三方登录 + 同步 (**服务器依赖, 时间金钱成本高**)

**前置**: 需要申请以下开发者账号 (1-2 周审批)
- [ ] GitHub OAuth App (个人可申请, 免费)
- [ ] 腾讯开放平台 (QQ 互联) (公司资质, 个体工商户, 300元/年)
- [ ] 微信开放平台 (公司资质, 300元/年)
- [ ] 阿里云短信服务 (手机号验证) (个人可申请, 按条计费)
- [ ] 部署服务器 (云服务, ~100 元/月起步)

**任务**:
- [ ] **AuthClient 完整实现** (1 周)
  - [ ] GitHub OAuth (Authorization Code flow) - **优先做, 申请门槛低**
  - [ ] QQ OAuth 2.0
  - [ ] 微信扫码登录
  - [ ] 手机号 + 短信验证
  - [ ] 邮箱 + 密码 (本地 fallback)
- [ ] **HttpClient 完整实现** (3 天) - 成熟方案: QNetworkAccessManager + Qt Network Authentication (Qt 6.5+)
  - [ ] Bearer Token 自动附加
  - [ ] 超时 / 重试
  - [ ] SSL 证书校验 (custom CA)
  - [ ] 请求日志 (敏感信息脱敏)
- [ ] **云端同步** (4 天)
  - [ ] 用户设置 (主题/语言/最近文件) 同步
  - [ ] 项目文件 (PSD/Audio/Video) 上传 / 下载 (分块 + 断点续传)
  - [ ] 离线模式 + 冲突解决 (last-write-wins)
  - [ ] 多人协作 (可选, 阶段 8+ 单独做)

**验收**: GitHub OAuth 登录成功, 同步设置延迟 < 2s

### 阶段 9: 在线主题市场 (调整: 最最后, 2026-09-03) - 1 周

**目标**: 主题从云端下载, 用户分享自定义主题

**前置**: 阶段 8 (用户系统) 完成后

**任务**:
- [ ] **后端 API** (4 天) - 独立小项目
  - [ ] 主题列表 API (`GET /themes`)
  - [ ] 主题下载 API (`GET /themes/{id}/download`)
  - [ ] 主题上传 API (`POST /themes`) (需登录)
  - [ ] 主题搜索 / 标签 / 评分 (可选)
- [ ] **前端集成** (3 天)
  - [ ] ThemeGalleryDialog 加"在线" tab
  - [ ] 浏览 / 搜索 / 下载 / 安装 / 卸载
  - [ ] 已安装主题与在线主题分开管理

**验收**: 能浏览 / 下载 / 安装云端主题, 用户可上传自定义主题

---

## 11.5 阶段 0 → 9 完整时间线 (调整后)

```
Week  0     1     2     3     4     5     6     7     8     9     10    11    12    13    14    15
PhaseA ████                                         vcpkg + 路径 + 字体 + DWrite 成熟修法 + CI
阶段1   ░░░██████████████████████████████████████████████████                    图像 (4 周)
阶段2         ████████████████████████████████████████                          音频 (3 周)
阶段4               ████████████████████████                                    视觉 (3 周, 与 2/3 部分并行)
阶段3                     ████████████████████████████████████████              视频 (4 周, 2/4 之后)
阶段5                           ████████████████████                          AI 助手 (2 周, 不依赖用户)
阶段6                                       ████████                          本地主题 (1 周)
阶段7                                           ████████████████████████      横向 AI 工具 (3 周)
阶段8                                                   ████████████████████  用户 (2 周, 需服务器/审批)
阶段9                                                           ████████      主题市场 (1 周, 需阶段 8)
优化收尾                                                            ██████  OpenGL / 跨平台测试 (可选)
```

**总计**: 阶段 0 之后约 **7-8 个月**完成 9 阶段 (含 PhaseA + 优化). 单人工作量.

---

## 附录 A: 文件大小统计 (截至 2026-09-03)

```
app/        mainwindow.{h,cpp,ui}  ~  90 KB
            main.cpp                ~   7 KB
ui/         *.{h,cpp,ui}           ~ 110 KB
models/     recentlistmodel.{h,cpp}  ~   5 KB
core/       *.{h,cpp}              ~  30 KB
media/      imagewindow.{h,cpp,ui}  ~  50 KB
            imageprocessor.{h,cpp}  ~   8 KB
            graphics*               ~   5 KB
ai/         iaimodel.h, aimanager.*  ~   5 KB
user/       user.*                  ~  15 KB
network/    authclient.*, httpclient.*  ~ 10 KB
logger/     logger.{h,cpp}          ~  30 KB (SHARED library)
CMakeLists.txt                       ~   3 KB
total       ~ 380 KB (代码)
```

```
build/      MultiDoc.exe           2.4 MB
            multidoc_logger.dll   1.6 MB
            Qt6*.dll              ~ 50 MB
            OpenCV                64 MB
            sqldrivers, platforms, imageformats, tls
                                   ~ 5 MB
total       ~ 130 MB (部署)
```

## 附录 B: 关键技术点 (避免再踩)

1. **spdlog pattern `%n` 是 logger name 不是 newline** - 用 literal `\n`
2. **widget 未 show 过时 setGeometry + showMaximized 会触发 first paint 异常** - 先 showMaximized 不传 setGeometry
3. **栈 MainWindow 而非 new** - 避免 COM refcount 异常
4. **Qt6 setTransformOriginPoint 在 onlyTransform 模式下不生效** - bake into m_transform
5. **OpenCV ↔ QImage 必须深拷贝** - 用 .copy()
6. **QAbstractItemModel beginRemoveRows/beginInsertRows 必须用真实 QModelIndex parent** - 写 indexForItem helper
7. **frameless 模式 + Qt 6 QWindow::startSystemResize** 是边缘 resize 官方方案
8. **QWidget::screen() 在 Maximized 时返回 primary** - 跨屏用 screenAt(center) + moveEvent 跟踪
9. **QtQuick RHI 默认 D3D11 在 Intel 集成显卡上崩** - qputenv("QT_QUICK_BACKEND", "software")
10. **DWrite first-paint 异常** - Fusion style + 简单字体 (无法完全消除)

---

## 12. 阶段间关联 + 并行开发建议

> **核心原则**: 一个阶段一个阶段做; 当两个阶段互相依赖 (互相复用代码) 时, **优先** 串行; 当两个阶段互相**独立**且工作量都不大 (1-2 周) 时, 可以并行.

### 12.1 阶段依赖图 (调整后 2026-09-03, 9 阶段)

```
                          [阶段 0: 基础设施] ✓
                                  |
        +-------------+-----------+-----------+-----------+
        |             |           |           |           |
   [阶段 1]      [阶段 2]    [阶段 3]    [阶段 4]    [阶段 5]
   imageWorker  audioWorker  videoWorker visionWorker  AI 助手
        |             |           |           |           |
        +------+------+           |           |           |
               |     imageWorker 复用          |           |
               v                                 |           |
            [阶段 3]                             |           |
            (颜色分级/字幕烧录/稳定)              |           |
               |                                 |           |
               v                                 v           |
            [阶段 7: 横向 AI 工具] <-------------+           |
            (YOLO / Real-ESRGAN / Whisper)                  |
                       |                                     |
                       v                                     v
            [阶段 6: 本地主题]    [阶段 8: 用户管理] ←──────┘
            (纯色 + 渐变)       (需服务器/第三方审批) ↑
                       |                             |
                       v                             |
            [阶段 9: 在线主题市场] ←─────────────────┘
            (主题从云端下载)
```

**关键依赖**:
- 阶段 1-4 (Worker) → 阶段 5 (AI 助手, 调 Worker) → 阶段 7 (横向 AI, YOLO 等)
- 阶段 1-4 → 阶段 3 (复用) → 阶段 6 (本地主题, UI) → 阶段 8 (用户) → 阶段 9 (在线主题市场)
- 阶段 5 (AI 助手) **不依赖** 阶段 8 (用户), 可以提前做
- 阶段 8 → 阶段 9 (主题市场需要用户)

### 12.2 关联强度矩阵 (9 阶段, 调整 2026-09-03 澄清)

✅ = 强关联 (复用大量代码, 串行更省工)
🟡 = 弱关联 (复用部分模块, 串行即可)
⚪ = 无关联 (可以并行)

| 阶段对 | 1 | 2 | 3 | 4 | 5 (AI 助手) | 6 (本地主题) | 7 (AI 工具) | 8 (用户) | 9 (主题市场) |
|---|---|---|---|---|---|---|---|---|---|
| **1 图像** | - | ⚪ | ✅ | ✅ | ⚪ | 🟡 | ✅ | 🟡 | 🟡 |
| **2 音频** | | - | ✅ | ⚪ | ⚪ | 🟡 | ✅ | 🟡 | 🟡 |
| **3 视频** | | | - | ⚪ | ⚪ | 🟡 | ✅ | 🟡 | 🟡 |
| **4 视觉** | | | | - | ⚪ | 🟡 | ✅ | 🟡 | 🟡 |
| **5 AI 助手** | | | | | - | ⚪ | ⚪ | ⚪ | 🟡 |
| **6 本地主题** | | | | | | - | 🟡 | 🟡 | ✅ |
| **7 AI 工具** | | | | | | | - | ⚪ | ⚪ |
| **8 用户** | | | | | | | | - | ✅ |
| **9 主题市场** | | | | | | | | | - |

**关键澄清 (2026-09-03)**:
- 阶段 5 (AI 助手) **不**调 Worker / AI 工具 - 跟阶段 1-7 全部 ⚪ (无强关联)
- 阶段 7 (AI 工具) 必须有阶段 1-4 的 Worker 才能集成 AI 按钮 - 阶段 1-4 ↔ 7 ✅
- 阶段 5 ↔ 阶段 7 ⚪ - 两个**独立层** (AI 助手走云 API / AI 工具走本地 ONNX)
- 阶段 5 ↔ 阶段 6 ⚪ - AI 助手不依赖主题 (但**反向**依赖: SettingsDialog UI 在阶段 1+ 才有, 所以 5 在 1 之后)
- 阶段 9 (主题市场) ✅ 依赖 阶段 8 (用户): 上传/下载需要登录

**11.0 重要架构澄清 (2026-09-03 用户反馈)**

| 维度 | AI 助手 (阶段 5) | AI 工具 (阶段 7) |
|---|---|---|
| **定位** | 项目级对话 (类似 ChatGPT) | Worker 内部处理功能 |
| **位置** | 右下角 dock / 独立窗口 | imageWorker / audioWorker / videoWorker / visionWorker 内的 AI 按钮 |
| **使用方式** | 用户输入自然语言 → AI 回答 / 调 Worker | 用户在 Worker 内点 AI 按钮 → 直接处理 |
| **技术路线** | 云 API (OpenAI 兼容协议) | 本地 ONNX Runtime (ONNX 模型) |
| **推理位置** | 远端服务器 | 用户本地 |
| **成本** | API 调用费 (¥1/M tokens) | 模型下载一次性 + 推理电费 |
| **数据流向** | 用户问题 → 云 → 答案 | Worker 输入图像/视频 → 本地模型 → 结果 |
| **示例** | "把图片调亮 20%" | 点 "AI 超分" 按钮, 选 Real-ESRGAN 4x |
| **历史记录** | 本地 SQLite (对话) | 无 (每次单次处理) |
| **依赖** | API key (SettingsDialog 配置) | 模型文件 (启动下载) |
| **网络依赖** | 必须 (云 API) | 仅模型下载时 |

**结论**: AI 助手和 AI 工具是**两个独立层**, **不**互相调用. AI 助手阶段 5 可以**与阶段 6 并行**, **不依赖**阶段 7.

### 12.3 关键关联详解

#### ✅ 阶段 1 (图像) → 阶段 3 (视频): 强关联
- **复用**: 视频需要"图像帧处理" pipeline; 视频的"颜色分级"完全复用阶段 1 的色彩调整模块
- **复用**: 视频"字幕烧录"用阶段 1 的 GraphicsTextItem 文字渲染
- **复用**: 视频"图像稳定"基于阶段 1 的特征检测 + 变换
- **建议**: 串行, 阶段 1 → 阶段 3. 不要并行.

#### ✅ 阶段 2 (音频) → 阶段 3 (视频): 强关联
- **复用**: 视频需要"音频轨" — 直接用 audioWorker
- **复用**: 视频"音频转字幕" = 阶段 2 的语音转文字
- **建议**: 串行, 阶段 2 → 阶段 3.

#### ✅ 阶段 1 (图像) → 阶段 4 (视觉): 强关联
- **复用**: visionWorker 的"图像分类"完全用 imageWorker 的图像预处理 (resize/normalize)
- **复用**: OCR 用 imageWorker 的二值化 / 形态学
- **建议**: 串行, 阶段 1 → 阶段 4.

#### ✅ 阶段 1 (图像) → 阶段 3 (视频): 强关联
- **复用**: 视频需要"图像帧处理" pipeline; 视频的"颜色分级"完全复用阶段 1 的色彩调整模块
- **复用**: 视频"字幕烧录"用阶段 1 的 GraphicsTextItem 文字渲染
- **复用**: 视频"图像稳定"基于阶段 1 的特征检测 + 变换
- **建议**: 串行, 阶段 1 → 阶段 3. 不要并行.

#### ✅ 阶段 2 (音频) → 阶段 3 (视频): 强关联
- **复用**: 视频需要"音频轨" — 直接用 audioWorker
- **复用**: 视频"音频转字幕" = 阶段 2 的语音转文字
- **建议**: 串行, 阶段 2 → 阶段 3.

#### ✅ 阶段 1 (图像) → 阶段 4 (视觉): 强关联
- **复用**: visionWorker 的"图像分类"完全用 imageWorker 的图像预处理 (resize/normalize)
- **复用**: OCR 用 imageWorker 的二值化 / 形态学
- **建议**: 串行, 阶段 1 → 阶段 4.

#### ✅ 阶段 1-4 → 阶段 5 (AI 助手): 中等关联 🟡
- **澄清 (2026-09-03)**: AI 助手**不**调 Worker / AI 工具 (Function Calling 推迟到阶段 8+ 单独做)
- **关联**: AI 助手**不**依赖 4 Worker, **可与阶段 6 并行**, **可在阶段 1-4 之前/之后启动**
- **建议**: 阶段 5 顺序在阶段 1-4 **之后** (因为 API key 在 SettingsDialog 配置, SettingsDialog UI 阶段 1+ 才有)

#### ✅ 阶段 1-4 → 阶段 7 (AI 工具): 强关联 ✅
- **澄清 (2026-09-03)**: AI 工具**集成进 Worker 内部** (作为 Worker 按钮), 必须先有 Worker
- **复用**: YOLO 跑在 visionWorker 的"目标检测"按钮下; Real-ESRGAN 跑在 imageWorker 的"AI 超分"按钮下
- **建议**: 串行, 阶段 1-4 全部完成后做阶段 7

#### ⚪ 阶段 5 (AI 助手) ↔ 阶段 7 (AI 工具): 无强关联 ⚪ (2026-09-03 澄清)
- **澄清**: AI 助手 (阶段 5) **不**调 AI 工具 (阶段 7). 它们是**两个独立层**
- AI 助手 = 对话 UI, 走云 API, 项目级
- AI 工具 = Worker 内部按钮, 走本地 ONNX, 模块级
- **可并行** (阶段 5 跟 6 并行, 阶段 7 跟 8 部分并行)

#### ✅ 阶段 8 (用户) → 阶段 9 (主题市场): 强关联 ✅
- **复用**: 主题市场需要"用户登录 / 上传主题" — 必须先有用户系统
- **建议**: 串行, 阶段 8 → 阶段 9

#### ⚪ 阶段 5 (AI 助手) ↔ 阶段 8 (用户): 无强关联 ⚪
- AI 助手 API key 在 SettingsDialog 本地配置, **不**需要用户系统
- 历史记录本地 SQLite, **不**需要用户系统
- 可以并行做

#### ⚪ 阶段 7 (AI 工具) ↔ 阶段 8 (用户): 无强关联 ⚪
- AI 工具是**本地**推理 (ONNX Runtime), 不需要用户系统
- 可以并行做

#### ⚪ 阶段 5 (AI 助手) ↔ 阶段 6 (本地主题): 无强关联 ⚪
- AI 助手**不依赖**主题系统
- 可以并行做

### 12.4 推荐开发顺序 + 并行窗口

> **主线** (调整后 9 阶段): 0 → 1 → 2 → 4 → 3 → 5 (AI 助手) → 6 (本地主题) → 7 (AI 工具) → 8 (用户) → 9 (主题市场)
>
> **并行窗口** (主线某阶段空闲时, 可同时开):
> - 阶段 2 + 阶段 4: 音频和视觉无强关联, **可与阶段 1 后期并行做**
> - 阶段 3 + 阶段 4: 阶段 4 在阶段 1 完成后即可启动, 跟阶段 3 完全并行
> - 阶段 5 (AI 助手) 可在阶段 3 完成后启动, 跟阶段 6 并行
> - 阶段 8 (用户) 可在阶段 6 完成后启动, 跟阶段 7 部分并行
> - 阶段 9 必须等阶段 8

### 12.5 时间线 (推荐, 调整后)

```
Week  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15   16   17
PhaseA ████                                                          vcpkg + 路径 + 字体 + DWrite 成熟修法 + CI
阶段1   ░░░██████████████████████████████████████████████████                    图像 (4 周)
阶段2         ████████████████████████████████████████                          音频 (3 周)
阶段4               ████████████████████████                                    视觉 (3 周, 与 2/3 并行)
阶段3                     ████████████████████████████████████████              视频 (4 周, 2/4 之后)
阶段5                           ████████████████████████                        AI 助手 (2 周, 不依赖用户)
阶段6                                       ████████                            本地主题 (1 周, 阶段 5 期间)
阶段7                                           ████████████████████████        横向 AI 工具 (3 周, 阶段 6 之后)
阶段8                                                   ████████████████████    用户 (2 周, 需服务器/审批)
阶段9                                                           ████████        主题市场 (1 周, 需阶段 8)
```

并行组合:
- 阶段 2 + 阶段 4 可同时做 (Week 6-10)
- 阶段 3 + 阶段 4 可同时做 (Week 6-10, 但 4 在 1 完成后, 3 在 2 完成后)
- 阶段 5 (AI 助手) 可与阶段 6 (本地主题) 并行 (Week 10-12)
- 阶段 7 (AI 工具) 完成后, 阶段 8 (用户) 可并行 (Week 14-16)
- 阶段 9 必须等阶段 8 (Week 16-17)

---

## 13. 优化内容分类

> **核心原则**:
> - "**必须前置**" = 不优化, 后续阶段会卡 (编译失败/运行崩溃/性能不可接受)
> - "**可并行**" = 跟某个阶段同步做, 既优化了代码又推进了阶段进度
> - "**阶段 7 后**" = 不紧迫, 主体功能稳定后再优化

### 13.1 必须前置 (阶段 1 开始**前**完成)

| 优化项 | 原因 | 工作量 |
|---|---|---|
| **0. DWrite 异常成熟修法** ⭐ | 阶段 0 first-paint 异常, 阶段 1+ 大图渲染时高发, 用成熟方案 (Fusion + software RHI + Arial + QQuickWidget 改造) | 0.5 天 (集成 main.cpp) + 1 天 (关键 UI 试 QQuickWidget) |
| **5. vcpkg 引入 OpenCV** | 阶段 1-4 都重度用 OpenCV, 不引入 vcpkg 每次配 path, 跨平台失败 | 1 天 |
| **4. 跨平台路径** (QStandardPaths) | `./logs` `./data` 硬编码, 多平台无法运行 | 0.5 天 |
| **6. 字体跨平台** (QFontDatabase fallback) | `Arial` 跨平台, Linux/macOS 必崩 | 0.5 天 |
| **8. CI/CD 搭建** | 每次手 build 浪费时间, 阶段 1 改一处要 build 验证多平台 | 1 天 |

**总计: 3-4 天 (阶段 1 之前 1 周内完成)**

### 13.2 与阶段 1 同时做 (1-2 周内)

| 优化项 | 阶段 1 关联 | 工作量 | 并行理由 |
|---|---|---|---|
| **1. OpenCV 异步化** | 阶段 1 大图处理 | 1-2 天 | 阶段 1 一开始就用 OpenCV, 同步会卡 UI |
| **2. 图像处理性能 (LOD 缓存)** | 阶段 1 大图渲染 | 1 周 | 阶段 1 图层系统需要 LOD, 否则 24MP 卡 |
| **3. 错误处理统一** (试点 FileExtensionRegistry + MediaDispatcher) | 阶段 1 imageWorker | 1 周 | 阶段 1 worker 实现要用统一错误返回 |
| **7. 单元测试** (MediaDispatcher / FileExtensionRegistry / FileTreeModel) | 阶段 1 | 3 天 | 阶段 1 路由逻辑是核心, 必须有测试 |

**总计: 1 周 (跟阶段 1 前 2 周并行)**

### 13.3 与阶段 2-4 同时做

| 优化项 | 关联阶段 | 工作量 |
|---|---|---|
| **10. 撤销/重做异步化** | 阶段 1 (图层), 阶段 3 (视频时间线) | 1 周 (阶段 1 中后期) |
| **3. 错误处理统一 (全 codebase 推广)** | 阶段 2-4 | 2 周 (每阶段同步推) |

### 13.4 阶段 7 之后做 (主体功能稳定后)

| 优化项 | 关联阶段 | 工作量 |
|---|---|---|
| **9. OpenGL 渲染** (QOpenGLWidget 替代 QWidget paint) | 阶段 1/3/4 大图渲染 | 2 周 (可选) |
| **12. 跨平台测试** (CI 跑 Linux/macOS build + 截图回归) | 全部 | 1 周 |

### 13.5 阶段 9 后 (不紧迫, 优化期再做)

| 优化项 | 原因 |
|---|---|
| **2. 图像处理性能** (高级 LOD) | 阶段 1-4 主体功能稳定后, 性能优化才有意义 |
| **10. 撤销/重做异步化** (高级) | 同上 |
| **11. 主题市场** (已拆分到阶段 9) | 阶段 6 (本地) → 阶段 9 (在线) |

### 13.6 总结表 (按新 9 阶段)

| 阶段 | 必做 | 并行 | 后续 |
|---|---|---|---|
| **阶段 0 ✓** | (已完成) | (已完成) | - |
| **阶段 1** (图像) | 0.DWrite修法, 5.vcpkg, 4.路径, 6.字体, 8.CI | 1.OpenCV异步, 2.LOD, 3.错误处理, 7.测试 | - |
| **阶段 2** (音频) | - | 3.错误处理推广 | - |
| **阶段 3** (视频) | - | 3.错误处理推广, 10.撤销异步 | - |
| **阶段 4** (视觉) | - | 3.错误处理推广 | - |
| **阶段 5** (AI 助手) | - | 7.单元测试补 AI 助手 | - |
| **阶段 6** (本地主题) | - | - | - |
| **阶段 7** (横向 AI) | - | 7.单元测试补 ONNX | - |
| **阶段 8** (用户) | - | - | - |
| **阶段 9** (主题市场) | - | - | - |
| **阶段 5** (用户) | - | - | - |
| **阶段 6** (本地主题) | - | - | - |
| **阶段 7** (AI 工具) | - | - | 9.OpenGL, 12.跨平台测试 |
| **阶段 8** (用户) | - | - | - |
| **阶段 9** (主题市场) | - | - | - |

### 13.7 优化执行时间线 (建议, 按新 9 阶段)

```
Week  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15   16   17
PhaseA  ████                                                          vcpkg + 路径 + 字体 + DWrite 修法 + CI
阶段1   ░░░██████████████████████████████████████████████████                    图像 (4 周)
优化1            ████                                                  OpenCV 异步
优化2                 ████████                                          图像 LOD 缓存
优化3                      ████                                          错误处理统一 (试点)
优化4                            ████                                  单元测试
阶段2                              ████████████████████████              音频 (3 周)
优化5                                    ████                          错误处理推广
阶段4                                    ████████████████████████      视觉 (3 周, 与 2/3 并行)
阶段3                                          ████████████████████████████████  视频 (4 周, 2/4 之后)
阶段5                                                  ████████████████████  AI 助手 (2 周, 不依赖用户)
阶段6                                                          ████████  本地主题 (1 周, 阶段 5 期间)
阶段7                                                              ████████████████████████  AI 工具 (3 周, 阶段 6 之后)
阶段8                                                                              ████████████████████  用户 (2 周, 需服务器/审批)
阶段9                                                                                              ████████  主题市场 (1 周, 需阶段 8)
优化6                                                                                  ████  撤销异步
优化7                                                                                                          ████  OpenGL (可选)
优化8                                                                                                              ████  跨平台测试
```

---

## 附录 C: 开发节奏建议 (按新 9 阶段)

> **确认**: 用户选择**一个阶段一个阶段做**, **关联阶段串行**, **独立阶段可并行**.

### C.1 主线节奏 (推荐)

| 顺序 | 阶段 | 周期 | 备注 |
|---|---|---|---|
| PhaseA | 必做优化 (0.DWrite + 5.vcpkg + 4.路径 + 6.字体 + 8.CI) | 3-4 天 | 阶段 1 之前 |
| 1 | 图像系统 | 4 周 | + 并行做优化 1/2/3/4 |
| 2 | 音频系统 | 3 周 | + 并行做优化 5 |
| 4 | 工业视觉 | 3 周 | **可与 2/3 并行** |
| 3 | 视频系统 | 4 周 | (在 2/4 之后) |
| 5 | 用户管理 | 2 周 | 任意时刻启动, 但 6/7 强依赖 |
| 6 | 主题系统 | 1 周 | (在 5 之后) |
| 5 | AI 助手 | 2 周 | 不依赖用户, 跟 6 并行 |
| 6 | 本地主题 | 1 周 | 跟 5 并行, 不含在线市场 |
| 7 | 横向 AI 工具 (YOLO 等) | 3 周 | 需 1-4 完成, 决策: 集成 vs 服务器 |
| 8 | 用户管理 | 2 周 | 需服务器/审批, 推迟到最后 |
| 9 | 在线主题市场 | 1 周 | 需 8 完成 |
| 优化 6-8 | 撤销异步 + OpenGL + 跨平台测试 | 3 周 | 阶段 7 后 |

**总计**: 阶段 0 之后约 **7-8 个月**完成所有 9 阶段 (含 PhaseA + 优化), 单人工作量.

### C.2 并行组合 (按新 9 阶段)

| 主线 | 可并行 | 不可以并行 |
|---|---|---|
| 阶段 1 (图像) | 阶段 2 / 阶段 4 (后启动) | 阶段 3 / 阶段 7 |
| 阶段 2 (音频) | 阶段 4 (强独立) | 阶段 3 (强依赖) / 阶段 7 |
| 阶段 3 (视频) | - | 阶段 7 (强依赖) |
| 阶段 4 (视觉) | - | 阶段 7 (强依赖) |
| 阶段 5 (AI 助手) | 阶段 6 (本地主题) | - |
| 阶段 6 (本地主题) | 阶段 5 (AI 助手) | 阶段 9 (需用户) |
| 阶段 7 (AI 工具) | 阶段 8 (用户) (部分) | - |
| 阶段 8 (用户) | 阶段 7 (AI 工具) (部分) | 阶段 9 (主题市场) |
| 阶段 9 (主题市场) | - | - |

### C.3 风险预案

- **阶段 1 拖延**: 图层系统是核心, 一旦延期 1-2 周影响整个时间线
  - 应对: 简化初期图层 (只 Normal 混合, 不做 17 种混合模式), 后续补
- **OpenCV 跨平台失败**: 阶段 1-4 都没法跨平台
  - 应对: 提前 PhaseA 引入 vcpkg, 验证 Linux/macOS build
- **ONNX Runtime 集成困难**: 阶段 7 延期
  - 应对: 阶段 4 暂用 OpenCV DNN 跑 YOLO (CPU), 阶段 7 升级到 ONNX Runtime
- **DWrite 异常无法完全消除**: 阶段 1+ 高发
  - 应对: PhaseA 用 Qt 6 成熟方案 (Fusion + software RHI + Arial), 关键 UI 试 QQuickWidget
- **第三方登录审核 1-2 周**: 阶段 8 申请 QQ/微信开发者账号
  - 应对: 提前申请, 阶段 8 期间用 GitHub OAuth 验证流程 (门槛低, 个人可申请)
- **服务器成本**: 阶段 8 (用户) + 阶段 9 (主题市场) 需云服务
  - 应对: 用户管理用免费额度 (GitHub OAuth + 本地 SQLite), 主题市场先做前端, 后端用低成本云 (轻量应用服务器 ~100 元/月)
- **AI 模型集成方案未定**: 阶段 7 决策 (集成 vs 服务器)
  - **2026-09-03 澄清**: 阶段 7 AI 工具**全部本地集成** (ONNX Runtime), **不**走云. 此风险已消除
  - 应对: 阶段 7 之前用 yolov8n.onnx (6MB) 验证 ONNX Runtime pipeline 通

### C.4 AI 服务架构 (2026-09-03 澄清, 决策已确定)

**AI 助手 (阶段 5)** - 走云 API:
- **API 协议**: OpenAI 兼容 (HTTP + SSE 流式)
- **可选服务**:
  - **DeepSeek-chat** (国内, ¥1/M tokens, 推荐起步)
  - **OpenAI gpt-4o-mini** ($0.15/M tokens, 国际)
  - **通义千问 qwen-turbo** (国内, ¥0.8/M tokens)
  - **智谱 GLM-4-Flash** (国内, ¥0.1/M tokens, 免费额度)
- **配置**: SettingsDialog "AI 助手" 标签填 endpoint + API key + 模型名

**AI 工具 (阶段 7)** - 走本地 ONNX Runtime:
- **模型格式**: ONNX (Open Neural Network Exchange)
- **推理引擎**: ONNX Runtime 1.x (vcpkg `onnxruntime`)
- **GPU 加速**: DirectML (Windows) / CUDA (Linux) / CoreML (macOS)
- **模型清单** (按 Worker):
  - visionWorker: yolov8n.onnx (6MB), 工业检测模型
  - imageWorker: realesrgan-x4.onnx (64MB), sam-vit-b.onnx (375MB), gfpgan.onnx (332MB)
  - audioWorker: whisper-tiny.onnx (39MB)
  - videoWorker: 视频超分模型 (待选)

**结论**: AI 助手 (云) 和 AI 工具 (本地) 是**两个独立层**, **不**互相调用. 不再需要"集成 vs 服务器"决策.
