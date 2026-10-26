# 03 · core 基础设施（会话/主题/媒体路由/线程池/日志/模型/用户/网络/AI）— 审计报告

> 范围：`src/core/**`（17 文件）、`src/core/ThreadPool/**`（16 文件 + backends/）、`src/logger/**`、`src/models/**`、`src/user/**`、`src/network/**`、`src/ai/**` 及各自 CMakeLists
> 约 5,300 行（全部逐个读完）；审查者：audit-core

## 1. 模块概览

本模块是整个 app 的“地基”，但当前处于两种截然不同的状态叠加：

- **真正在线运行的**：`ThreadPool v2`（`EngineContext` 6 池 + `SerialExecutor`）被 `ImageWindow`/`ImageProcessor`/`LayerStack`/`AdjustmentPanel`/`RenderGraph` 作为异步执行器使用；`Logger`（spdlog SHARED 库）被几乎所有模块打日志；`SessionManager`/`RecentManager`/`ThemeManager`/`LanguageManager` 被 `main.cpp`、`MainWindow`、`HomePage`、`SettingsDialog` 直接调用；`UserManager`/`UserDatabase` 在启动时初始化。
- **骨架/悬空/死代码**：`MediaDispatcher` + `IModule`/`IWorkspace` + `FileExtensionRegistry` 的“扩展名路由”从未被生产代码调用（`MainWindow` 直接 `new ImageWindow`）；`Async.h` + `Result.h` 只被 `ImageProcessorAsync.cpp` 用；`CancellationToken` 树 / `Generation` / `Deadline` / `parallel_for` / `task_graph` / `RealtimeThread` / `SpscRing` / `qt_bridge` 在 `src/` 内**零调用点**；`AuthClient`/`HttpClient`/`AIManager` 全部是 stub。

调用方向：`app/main → {core 单例, EngineContext, Logger}`；`media/* → {EngineContext::background(), Logger, RecentManager, ImageProcessorAsync→Async.h→QThreadPool}`；`models/RecentListModel → RecentManager`；`user/UserManager → UserDatabase → QSqlDatabase`。core 不被 UI 反向依赖（`MediaDispatcher` 只 include `IModule.h`），分层方向基本正确，问题在于“接口预留了、实现没接上”。

## 2. 缺陷清单

| ID | 严重度 | 位置 | 类型 | 现象 | 根因 | 修复建议（一句话） |
|---|---|---|---|---|---|---|
| CI-01 | P0 | core/ThreadPool/thread_pool.cpp:198-235 | memory | 每个执行完的任务对象都不释放；异步滤镜/渲染任务捕获的 `cv::Mat clone()` 永久泄漏 | `run_task(Task*)` 与 `schedule_continuations()` 均未 `delete t`，提交侧 `submit()` 已 `task.release()` 交出所有权 | 在 `run_task` 末尾（含 expired/cancelled 早退分支）`delete t`，或全程改用 `TaskPtr` 作用域持有 |
| CI-02 | P0 | core/ThreadPool/work_stealing_queue.hpp:41-69 | threading | 本地 Chase-Lev 队列永远不前进：同一任务被反复执行、最后一个任务被丢弃 | `b = bottom_-1` 是“末元素下标”，但两处 `bottom_.store(b+1)`/`CAS(b → b+1)` 用的是它，多元素路径下 `bottom_` 不变；末元素路径 CAS 期望值比实际值小 1，必失败 | 按标准 Chase-Lev 改为 `bottom_.store(b)`；末元素用 `top_.compare_exchange_strong` 抢；删除 `top_.store` 的无条件写 |
| CI-03 | P0 | media/imagewindow.cpp:1965-1971 + media/imageworker/layers/LayerStack.cpp:1796-1800 | crash | 大图异步渲染完成后投递到主线程的回调捕获裸 `this`；关 tab 后事件仍会派发，读写已析构 `ImageWindow` → UAF | `QMetaObject::invokeMethod(qApp, functor, Queued)` 的 context 是 `qApp` 而非接收者，Qt 无法在接收者析构时撤销该事件 | `invokeMethod(imageWindow, …)` 用接收者作 context，或回调中改 `QPointer<ImageWindow>` + 在窗口析构时 bump `m_currentTaskId` |
| CI-04 | P1 | core/ThreadPool/thread_pool.cpp:102-143 | threading | `Block` 背压下阻塞的提交线程在池关闭时永不唤醒；池析构时会销毁其正在等待的 `cv_/mu_` | 关闭路径只 `cv_.notify_all()`（worker 用），从不 `queued_cv_.notify_all()`；`cancel_pending` 清空队列后等待线程谓词已满足却收不到通知 | `shutdown()` 里对 `queued_cv_` 也 notify_all；等待返回后若 `is_shutting_down()` 直接拒收 |
| CI-05 | P1 | core/ThreadPool/serial_executor.cpp:93-107 | threading | `run_sync()` 在 `submit` 被拒/任务抛异常时无限 `yield()` 自旋，调用线程永久挂死 | 忽略 `submit()` 返回值；`done` 只在 lambda 正常返回时置位，异常路径不置位 | 用 `std::promise/future` + 检查 `submit` 返回值，失败时同步执行；异常也置位 |
| CI-06 | P1 | media/imagewindow.cpp:293-300, app/main.cpp:89-95 | perf | 8 核机器启动即创建约 42 个池线程；每打开一个 ImageWindow 再创建 11 个 → 多 tab 时线程数上百，全部空转 | `main.cpp` 的全局 `EngineContext` 从未被使用（`EngineContext::current()` 零调用），`ImageWindow` 又各自 `new` 一个 `EngineContext`（hw 写死 2） | 只保留一个进程级 EngineContext 并注入到各窗口；删掉 `ImageWindow` 自建池；池线程按需懒启动 |
| CI-07 | P1 | core/SessionManager.cpp:47-56 + app/mainwindow.cpp:295-298 + app/main.cpp:178,196-200 | data-loss | 崩溃恢复标记不可靠：多实例互相覆盖、`QSettings` 未 `sync`、closeEvent 之后崩溃仍算“干净”、QML 错误早退分支完全跳过清理；且**没有任何自动保存**，崩溃后未保存的编辑全部丢失 | `lastExitClean` 是单布尔全局状态，无进程互斥（无 QLockFile/单实例）；无版本字段；恢复只重开文件不恢复未保存内容 | 用 `QLockFile` 单实例 + 每窗口独立 `QSaveFile` 自动保存/恢复清单；退出前 `QSettings::sync()` |
| CI-08 | P1 | core/recentmanager.cpp:69-85 | logic | 重新打开一个已 `pinned` 的文件会静默取消其固定状态 | `touchOpen` 先 `erase` 旧条目再新建 `RecentEntry`，`e.pinned=false` 硬编码，原 pinned 在 erase 时丢失 | erase 前记录 `pinned`（`findIndex` 已有现成实现）并写回 |
| CI-09 | P1 | core/ThreadPool/engine_context.cpp:145-165 | crash | `enable_gpu_thread=false` 时 `gpu()`/`stats_gpu()` 解引用空 `unique_ptr` → 立即崩溃 | `gpu()` 没有像 `ai()/offline()` 那样回退，直接 `return *gpu_` | 增加空检查并回退到 `io()`，或把 gpu 池改成非可选 |
| CI-10 | P1 | core/ThreadPool/thread_pool.cpp:42-57,383-385 | threading | 构造期自旋超时后池进入“僵尸态”：worker 永久等 `ready_`，析构 `join()` 永久挂死；调用方却拿到“成功”对象 | 失败路径只置 `shutting_down_`、notify `start_cv_`，但 worker 的等待谓词是 `ready_`，永不满足 | 失败路径置 `ready_=true` 让 worker 走正常退出；或构造失败直接抛异常 |
| CI-11 | P1 | logger/Logger.cpp:416-420,522-539 | perf | 日志队列满时 `async_overflow_policy::block` 会阻塞打日志的线程（可能是 UI 线程）；`flush()` 睡 600ms、`shutdown()` 睡 700ms；改日志目录/级别还要重建 logger + 再睡 700ms | 与头文件“业务线程只 push，不阻塞”的承诺矛盾；用 sleep 等队列排空 | 改 `overrun_oldest`（丢最旧），Critical 单独同步 sink；`flush` 用 `thread_pool()->flush()`；配置热更新走 `sink->updateConfig` |
| CI-12 | P1 | logger/Logger.cpp:139-151,216-249 + app/main.cpp:127 | i18n | 日志目录含中文（`%LOCALAPPDATA%/…/<中文用户名>/logs`）时窄字符 `_mkdir/fopen` 失败，`Logger::init` 返回 false 被忽略 → **全程无日志**且无提示 | spdlog `file_helper` 用窄 `fopen`；目录创建用 `_mkdir(A)` 逐字符 | 路径统一 `std::filesystem::u8path` + `_wfopen` 自定义 sink；init 失败必须显式报错 |
| CI-13 | P1 | user/UserDatabase.cpp:25-56,58-100,165-246 | state-sync | `removeDatabase()` 在 `m_db` 仍持有连接引用时调用（Qt 警告+连接泄漏）；无 `foreign_keys/WAL/busy_timeout`；`exec()` 失败被吞成空 User（表现为“登录态突然丢失”）；无 schema 版本迁移 | 连接句柄未先释放；SQLite 默认 `journal_mode=delete`、`busy_timeout=0`、`foreign_keys=off` | `close()` 里先 `m_db = QSqlDatabase()` 再 remove；open 时执行 3 条 PRAGMA；用 `PRAGMA user_version` 做迁移；token 加密入库 |
| CI-14 | P2 | core/ThreadPool/thread_pool.cpp:136-139,253-256; engine_context.cpp:135-140 | memory | 关闭时 `cancel_pending=false`：队列中未执行 Task 既不执行也不删除，泄漏；续接任务 `submit` 失败时整条 DAG 被静默丢弃 | `shutdown(false)` 不清理 `injected_`；`submit_continuation_task` 忽略 `submit` 返回值（`TaskPtr` 析构直接删） | 关闭时统一删除所有 pending（或返回给调用方）；续接失败要回调错误 |
| CI-15 | P2 | core/ThreadPool/thread_pool.cpp:368-369 + config.hpp:104 | memory | `thread_count > kMaxWorkers(128)` 时 `worker_idx_storage_[idx]` 越界写（64 核机上 io 池 `n*2=128` 已达上限，96 核必崩） | 构造期未 clamp，TLS 槽位数组定长 128 | 构造时 `n = std::min(n, kMaxWorkers)`，或改 `std::unique_ptr<size_t[]>` |
| CI-16 | P2 | core/ThreadPool/thread_pool.cpp:184-196; serial_executor.cpp:80-91 | threading | `attach_continuation` 对非原子 `Task* continuation` 做 `reinterpret_cast<std::atomic<Task*>*>` CAS：严格别名 UB；同一 Task 挂到两个父节点时 `cont->continuation` 被第二次挂接覆盖 → fan-in DAG 断链 | 用任务自身的 `continuation` 字段同时当两个父链的 next 指针 | 每个父节点持有独立的 `std::vector<TaskPtr>`；不要 reinterpret_cast |
| CI-17 | P2 | core/ThreadPool/cancellation.hpp:61-68,71-86,98-109 | threading | 取消树：`cancel()` 快照裸子指针后逐个调用，子 token 并发析构 → UAF；子 token 析构读 `parent_->children_`，父先死 → UAF；`create_child()` `new` 无所有者约定 | 所有权不表达，锁只保护容器不保护生命周期 | 子 token 用 `std::shared_ptr<CancellationToken>`；父持 `weak_ptr` |
| CI-18 | P2 | core/ThemeManager.cpp:56-110 + app/mainwindow.cpp:1576-1585 + media/imagewindow.cpp:344-348 | ux | 主题切换只重刷 `MainWindow` tab 按钮与 `HomePage`；`ImageWindow` 内自绘组件/自建 dock 不重取 palette，深色主题下残留浅色；每次切换重新读盘 QSS + `qApp->setStyleSheet` 全量 re-polish | `ImageWindow` 在 P5.5 删掉了 `themeChanged` 连接；`globalStyleSheet()` 每次都读盘 | 保留 `themeChanged` 订阅并暴露 `applyTheme()`；QSS 解析一次并缓存 |
| CI-19 | P2 | core/MediaDispatcher.cpp:112-145 + app/mainwindow.cpp:3164 | design | `MediaDispatcher::openFile` / `IModule::openFile` / `IWorkspace` 在生产路径**从未被调用**；`MainWindow` 有两份重复的“先 ImageWindow 后 DocWindow”打开逻辑 | 路由层只服务测试；`ImageWorker::openFile` 是死实现 | 要么统一走 `MediaDispatcher`，要么删除该抽象 |
| CI-20 | P2 | core/MediaDispatcher.cpp:83-88 | design | 扩展名路由“后注册覆盖先注册”：`visionWorker` 与 `imageWorker` 共享 `.png/.jpg`，一旦注册 vision 模块，所有普通照片会被路由到 YOLO 模块 | `rebuildExtMap` 用 `m_extMap.insert(ext, m)` 覆盖；`moduleIdForExtension` 有优先级而这里没有 | 路由改 `QHash<QString, QList<IModule*>>` + 显式优先级/`canOpen()` 二次判定 |
| CI-21 | P2 | core/IModule.h:54-60 | perf | 默认 `canOpen()` 每次调用都 `info()` 按值返回 `ModuleInfo`（`QStringList` 深拷贝+原子操作），批量拖入 N 个文件会 2N 次分配 | 接口把静态元数据做成虚函数返回值 | 改 `virtual const ModuleInfo& info() const` 或增加 `extensions()` 引用访问器 |
| CI-22 | P2 | core/Async.h:118-125,152-156 | ux | `Async::cancel()` 名为取消实则 `waitForFinished()`：在 UI 线程点“取消”会阻塞到整个滤镜跑完 | QtConcurrent 任务不可抢占；`QFutureWatcher::cancel()` 对 `QtConcurrent::run` 无效 | 改为“代次号丢弃结果”（项目 `m_currentTaskId` 已用对）；不要在 UI 线程 wait |
| CI-23 | P2 | core/Async.h:98-111 | crash | 若任务抛异常，`watcher->future().result()` 在 `finished` 槽里重新抛出 → 异常穿过 Qt 事件循环 → `std::terminate` | 回调链没有 `try/catch` | lambda 内 `try { cb(...) } catch (const std::exception&) { qCritical…; }` |
| CI-24 | P2 | core/Result.h:215-226 | build | `Q_TRY` 展开成 `return _r.error();`（`Error` 无法隐式转 `Result<T>`）→ 按文档用必然编译失败；`Q_TRY_VAL` 是 GNU 语句表达式 `({...})`，MSVC 不支持 | 宏按 GCC 习惯写，从未编译验证过（全项目零使用） | 删除两个宏，或改 `return Result<T>::err(_r.error());` |
| CI-25 | P2 | core/Result.h:143-155 | design | `value()` 在错误时 `std::abort()`：Release 下用户看到进程直接消失，没有任何错误提示 | 用 abort 替代异常 | `value()` 抛 `std::logic_error`/`qFatal` 带 `error().toString()`；并给 `Result` 加 `[[nodiscard]]` |
| CI-26 | P2 | core/recentmanager.cpp:162-182,203-225 | data-loss | 最近列表是 `QSettings` 单一来源且加载时不校验文件存在：已删除/移动的文件长期挂在列表里，点击报错；`clearAll` 后旧 key 仍留在注册表（隐私残留） | `load()` 不做存在性过滤；`save()` 用 `beginWriteArray` 不带 size，缩表不删旧索引 | `load()` 时 `QFileInfo::exists` 过滤并置灰；`save()` 前 `remove(group)` 再写 |
| CI-27 | P2 | core/LanguageManager.cpp:30-66 + app/mainwindow.cpp:239-242 | i18n | 运行时切语言只有 `MainWindow` 响应 `LanguageChange`，`ImageWindow`/`HomePage`/自定义控件文字保持旧语言直到重启 | 子窗口无一实现 `changeEvent` 的 `LanguageChange` 分支 | 各顶层窗口统一 `retranslateUi()`；或切语言时提示“重启生效” |
| CI-28 | P2 | core/SessionManager.cpp:18-56 | data-loss | Session 存 `QSettings`（Windows 注册表 NativeFormat），无版本号、无 schema；每次 `setValue` 不 `sync`，进程被 kill 时可能丢最后一次写 | 无格式版本、无原子文件 | 改 `QSaveFile` 写 JSON `{version, files[], active}` + `commit()`；兼容旧 key 一次性迁移 |
| CI-29 | P2 | core/ThreadPool/executor.hpp:90-93 | threading | `Task::token` 是裸指针“not owned”，`gen_src` 是裸 `Generation*`：调用方 token/generation 先于任务销毁时 `is_cancelled()/expired()` 读悬垂指针 | 生命周期契约只写在注释里 | `token` 改 `weak_ptr<CancellationToken>`，`gen_src` 改 `weak_ptr<Generation>` |
| CI-30 | P2 | app/main.cpp:59-72 | threading | 全局 `qInstallMessageHandler` 用同一个 `static QFile`，worker 线程 `qWarning` 与主线程 `qInfo` 并发 `QTextStream<<`+`flush` 无锁 → 行交错/竞争；写死 `D:/vistella_qdebug.log`，无 D 盘机器上 open 失败被忽略 → Qt 告警全静默 | 自定义 handler 未加互斥，路径硬编码 | 用 `QMutex` 串行化 + `QStandardPaths::AppLocalDataLocation`；或转发给 `Logger` |
| CI-31 | P2 | logger/Logger.cpp:88-108,276-302 | logic | 每次按大小切分后，被切走的那条日志会同时出现在旧文件末尾和新文件开头（重复一行） | `openNewFile_` 设 `justCreated_=true`，`verifyAndRewrite_` 把新文件 0 字节误判为“创建失败”，把上一条 payload 又写进新文件 | `verifyAndRewrite_` 只在“首次打开且尚未写入”时启用；切分后不要复用 lastPayload |
| CI-32 | P2 | logger/Logger.cpp:404-408,477-520 | perf | 运行时改日志目录/级别会重建 logger 并 `sleep 700ms`（阻塞 UI），旧 async logger 队列在 `reset()` 时可能丢尾日志 | 重建流程用 sleep 兜底 | `setLevel` 只改 level 不重建；`setLogDir` 用已存在的 `sink->updateConfig()` |
| CI-33 | P2 | network/HttpClient.cpp:41-42,55 | build | `qWarning("... (%1 bytes)", url, body.size())` 用 Qt 的 printf 风格传 `%1`（非法转换符）+ 多余实参 → 格式化未定义行为，`body.size()` 实际没被打印；`QJsonDocument` 未 include（依赖传递包含） | stub 里随手写的日志与 include | 改 `{}`/`%d` 或 `qWarning() << …`；补 `#include <QJsonDocument>` |
| CI-34 | P2 | network/AuthClient.cpp:55-118 | logic | 所有 `loginXxxAsync` 对任意输入（空 code、空验证码）立即返回 `success=true` + mock token；`refreshTokenAsync` 永远成功 | 阶段 0 stub 未加“未配置 server 时返回失败”的保护 | stub 至少返回 `errorCode="NotConfigured"`，避免误接线造成鉴权绕过 |
| CI-35 | P3 | core/ThreadPool/realtime_thread.cpp:100-104 + ThreadPool/CMakeLists.txt | build | Windows MMCSS 调用 `AvSetMmThreadCharacteristicsW(L"Pro Audio", nullptr)` 传空 `pTaskIndex`（该参数不允许 NULL）→ 永远失败，实时优先级静默不生效；且未链 `avrt.lib`（一旦实例化即链接错误） | 参数误用 + CMake 缺库 | 传 `DWORD idx{}` 地址并检查返回值；CMake 加 `target_link_libraries(... PRIVATE avrt)` |
| CI-36 | P3 | core/ThreadPool/backends/qt_bridge.hpp:20,41 | build | 整个 Qt 后端：缺 `#pragma once`、namespace 写成 `multidoc::tp`（其余为 `vistella::tp`）、`TP_USE_QT` 默认 0 → 从未参与编译，属未验证死代码 | 复制粘贴后未清理 | 删除该文件，或补 pragma once/改 namespace 并加进 CI 编译矩阵 |
| CI-37 | P3 | core/ThreadPool/thread_pool.cpp:22,32,66,367,378,386; engine_context.cpp:83-122 | perf | 池构造/每个 worker 启动都无条件 `fprintf(stderr, …)`（8 核机启动约 100+ 行），Release 也输出 | 调试打印未加条件编译 | 改 Debug-only 宏或删掉 |
| CI-38 | P3 | core/Throttle.cpp:26-32 | perf | 每次 `trigger()` 打一条 `LOG_DEBUG`：拖动滑块（PropertiesDock 200ms 节流）持续产生日志，与 CI-11 的 block 策略叠加可卡 UI | 把节流器当日志点用 | 删除该日志或降到 Trace |

## 2.1 P0 / P1 缺陷详述

### CI-01 线程池执行完的任务对象永不释放 (P0)

- **位置**：`src/core/ThreadPool/thread_pool.cpp:198-235`（`run_task`）、`:237-256`（`schedule_continuations/submit_continuation_task`）
- **证据**：

```cpp
void ThreadPool::run_task(Task* t) {
    if (t->expired() || t->is_cancelled()) { ...; schedule_continuations(t); return; }  // 早退也不 delete
    ...
    try { t->fn(ctx); } catch (...) { ... }
    ...
    schedule_continuations(t);      // 只遍历 t->continuation
}                                   // 全文再无 delete t；对照 serial_executor.cpp:197 `delete t;`
```

- **现象 / 复现路径**：任何走 `EngineContext::background().submit_fn(...)` 的操作都泄漏一个 `Task`（约 150–200 字节）**以及它 `std::function` 里捕获的全部对象**。真正致命的是捕获了图像：`AdjustmentPanel::applyCurrentTab` 每次提交捕获 `cv::Mat snapshot = originalImage().clone()`（AdjustmentPanel.cpp:714,722）；拖动一次曝光/色阶滑块会触发几十次提交 → 每次泄漏一整份全分辨率图像缓冲（24MP×3B≈72MB/次）。任务对象不析构 ⇒ `cv::Mat` 引用计数不归零 ⇒ 底层 `Mat` 数据永远不会 free。拖一次滑块即可让内存上涨数 GB 直至 OOM 崩溃。
- **根因**：提交侧 `submit(TaskPtr task)` 用 `task.release()` 把所有权降级为裸指针（thread_pool.cpp:89），执行侧却没有配对的回收；`SerialExecutor` 是对的（`delete t`），`ThreadPool` 漏写。这是“所有权从 `unique_ptr` 逃逸后无人接管”的典型。
- **修复建议**：`run_task` 的**所有**返回路径（含 expired/cancelled 早退）执行 `std::unique_ptr<Task> guard(t);`；或在 `run_task` 结尾先取走 `Task* cont = t->continuation` 再 `delete t`。另加 ASAN/CRT 堆检查与 `PoolStats.completed - 存活任务` 自检。
- **关联**：`media/imagewindow.cpp:1929` 渲染、`LayerStack::renderAsync`、`ImageProcessorAsync::runOpEngine`、`RenderGraph::executeParallel` 全部受影响；现有测试若只断言统计数会漏掉。

### CI-02 自研 Chase-Lev 本地队列 pop 逻辑错误 (P0)

- **位置**：`src/core/ThreadPool/work_stealing_queue.hpp:41-69`
- **证据**：

```cpp
const std::int64_t b = bottom_.load(...) - 1;   // b = 末元素下标
...
if (t == b) {
    std::int64_t expected = b;
    if (!bottom_.compare_exchange_strong(expected, b + 1, ...)) { item = nullptr; }  // bottom_ 实为 b+1，CAS 期望 b → 恒失败
    top_.store(b + 1, ...);                                                          // 无条件写，丢掉该元素
} else {
    bottom_.store(b + std::int64_t{1}, ...);   // = 原 bottom_，没变 → 下一次 pop 仍取同一个 slot
}
```

- **现象 / 复现路径**：只有“池内 worker 向同一池提交”才会进本地队列（`thread_pool.cpp:86-95`），因此当前 UI→池的路径（走 injected 队列）不受影响，但以下情况必现：
  1. 任一任务内部再 `submit` 到同池（DAG 续接 `submit_continuation_task`、`parallel_for`、`make_chain`）→ 队列里有 ≥2 个任务时，末元素被反复执行（同一 `Task*` 被 `run_task` 跑 N 次），重复执行有副作用/重复消费捕获对象；
  2. 队列只剩 1 个任务时 pop 返回 nullptr 并把 `top_` 推到 `b+1`，该任务既未执行也未被释放 → 任务丢失（提交方在等它的副作用/回调 → 逻辑挂起）。
  因 `parallel_for` / `task_graph` 目前零调用，这是**潜伏但已具备触发条件**的 P0；一旦有人用 DAG 或嵌套提交就会表现为“任务重复执行 + 偶发丢任务/挂起”。
- **根因**：把“pop 后 bottom 应等于 b（末元素下标）”误写成 `b+1`；末元素的竞争应 CAS `top_`（与 thief 相同变量），这里却 CAS `bottom_`，且失败后仍无条件推进 `top_`。
- **修复建议**：按标准 Chase-Lev 重写：`b = bottom_-1; bottom_.store(b); t=top_; if (t>b) { bottom_.store(b+1); return null; } item=slots[b]; if (t==b) { if (!top_.CAS(t,t+1)) { bottom_.store(b+1); return null; } } return item;`（`top_` 用 seq_cst、`slots_` 用 acq_rel）。更稳妥：直接采用 oneTBB（Apache-2.0）或 Taskflow（MIT），不要自研 work-stealing。
- **关联**：CI-14（丢任务）、CI-16（DAG 断链）同源；`completed` 统计会大于实际执行数，掩盖问题。

### CI-03 异步渲染回调捕获裸 `this`，关 tab 后 UAF (P0)

- **位置**：`src/media/imagewindow.cpp:1954-1972`（`rebuildCurrentCache`）、`src/media/imageworker/layers/LayerStack.cpp:1791-1803`（`renderAsync`）
- **证据**：

```cpp
// LayerStack::renderAsync —— 主线程投递用 qApp 作 context
const bool submitted = engine->background().submit_fn(
    [self, cb, ctx](vistella::tp::TaskContext &) -> void {
        cv::Mat result = self->render();
        QMetaObject::invokeMethod(ctx,                       // ctx = QCoreApplication::instance()
            [cb, result = std::move(result)]() -> void { cb(result); },
            Qt::QueuedConnection);
    }, "LayerStack::renderAsync", ...);

// ImageWindow::rebuildCurrentCache —— cb 捕获裸 this
m_layerStack->renderAsync(m_engine.get(), [this, myTaskId](cv::Mat result) -> void {
    if (m_currentTaskId.load() != myTaskId) return;   // 窗口已析构后仍会执行：读已释放成员
    m_current = std::move(result);
    m_currentDirty = false;
    renderToView();
});
```

- **现象 / 复现路径**：大图（>1024×1024）上改图层/滤镜 → `rebuildCurrentCache` 走异步 → 渲染在 background 线程完成并把 lambda 用 `qApp` 作 context 投递到主线程事件队列；此时用户立刻 `Ctrl+W` 关掉该 tab，`ImageWindow` 析构，`m_engine.reset()` 会 join 掉 worker（此时 lambda 已经投递出去了），但**已投递的事件仍留在主线程队列**；事件循环下一个 tick 执行该 lambda → 访问已析构的 `m_current/m_currentTaskId/renderToView` → UAF，典型崩溃（Debug 栈在 `QMetaCallEvent::placeMetaCall` 之后）。
- **根因**：`QMetaObject::invokeMethod(ctx, functor, QueuedConnection)` 的 `ctx` 是**接收者 context object**：Qt 在 ctx 析构时撤销待派发事件。这里 ctx 是整个 `qApp`，永远不会在窗口关闭时析构，于是“生命周期保护”完全失效。`AdjustmentPanel::applyCurrentTab` 用 `QMetaObject::invokeMethod(host, …)`（AdjustmentPanel.cpp:737）才是正确写法。
- **修复建议**：
  1. `LayerStack::renderAsync` 增加 `QObject* context` 参数，把 `QMetaObject::invokeMethod(ctx, …)` 换成传进来的接收者（ImageWindow）；
  2. `ImageWindow::rebuildCurrentCache` 的回调改 `[guard = QPointer<ImageWindow>(this), myTaskId]`，并在窗口析构时 `m_currentTaskId.fetch_add(1)` 让旧结果失效；
  3. 更彻底：让 `renderAsync` 返回 `QFuture<cv::Mat>`，窗口侧用 `QFutureWatcher` 作 child（watcher 随窗口析构自动断开），不用手写 `invokeMethod`。
- **关联**：`AdjustmentPanel.cpp:718-744` 同理（QPointer 跨线程读本身是 data race，见 §5.4）；`ImageProcessor::applyLutAsync(callback)`（imageprocessor.cpp:1044-1082）的 callback 直接在后台线程触发，风险更高。

### CI-04 Block 背压 + 关闭 = 提交线程永久阻塞 (P1)

- **位置**：`src/core/ThreadPool/thread_pool.cpp:295-311`（Block 等待）、`:102-143`（shutdown）
- **证据**：

```cpp
// push_injected
queued_cv_.wait(lk, [&] {
    return shutting_down_.load(std::memory_order_acquire) ||
           (injected_[0].size() + injected_[1].size() + injected_[2].size()) < bound;
});
// shutdown()
if (cancel_pending) { std::lock_guard lk(mu_); for (auto& q : injected_) { for (Task* raw : q) delete raw; q.clear(); } }
cv_.notify_all();                       // 只唤醒 worker 的 cv_，queued_cv_ 没有 notify
for (auto& t : workers_) if (t.joinable()) t.join();
```

- **现象**：`io`/`offline` 池默认 `Backpressure::Block`（engine_context.h:52,54）。当队列满（4096/8192）时某非 worker 线程（如 UI 线程或另一个池的 worker）阻塞在 `queued_cv_`；此时关窗/退出触发 `shutdown()`：`injected_` 被清空（谓词第一次为真）但等待者收不到 notify，永远睡着；随后 `ThreadPool` 析构销毁 `mu_/queued_cv_`，等待线程苏醒后在已析构的 `std::condition_variable` 上操作 → UB/崩溃。worker 侧没有问题（用了 `cv_`）。
- **根因**：两个条件变量分别服务 worker 与 producer，关闭路径只广播了一个；`cancel_pending` 清空队列未同步 producer。
- **修复建议**：`shutdown()` 在置位 `shutting_down_` 后 `queued_cv_.notify_all()`；更推荐把 Block 改成“`wait_for(50ms)` + 循环检查 `is_shutting_down()`”，关闭中直接拒收。另 `EngineContext::~EngineContext` 用 `shutdown(false)`（engine_context.cpp:135-140）不清队列，与 CI-14 一起构成“退出时 pending 全泄漏”。
- **关联**：CI-05、CI-10、CI-14。

### CI-05 `SerialExecutor::run_sync` 可无限自旋 (P1)

- **位置**：`src/core/ThreadPool/serial_executor.cpp:93-107`
- **证据**：

```cpp
std::atomic<bool> done{false};
auto t = make_task([&fn, &done](TaskContext&) { fn(); done.store(true, ...); }, "run_sync");
submit(std::move(t));                       // 返回值被忽略
while (!done.load(std::memory_order_acquire)) { std::this_thread::yield(); }  // 无超时、无退出条件
```

- **现象**：`submit` 因队列满（默认 `Reject`）或 `stopping_` 返回 false 时任务被丢弃，`done` 永假 → 调用线程死循环（100% CPU）；任务 `fn()` 抛异常时 `done.store` 不执行 → 同样死循环；`shutdown(true)` 清队列删掉该 Task 时，`&done` 悬垂 → 调用方仍自旋或读悬垂栈变量。若在 UI 线程调用则整个 app 卡死。
- **根因**：把“队列语义”当“同步语义”用；没有把完成通知做成 promise，也没有检查提交结果/异常路径。
- **修复建议**：`std::promise<void> p; auto f = p.get_future();` 提交 `[&fn, &p] { try { fn(); p.set_value(); } catch(...) { p.set_exception(current_exception()); } }`，`submit` 失败时本地同步执行；`f.wait_for(30s)` 超时返回错误。当前 `run_sync`/`is_serial_thread()` 零调用点，可直接删掉减少维护面。
- **关联**：`engine_context.cpp:154` `gpu()` 串行执行器目前无调用方，属“未验证代码”。

### CI-06 线程池数量爆炸 + 全局 EngineContext 无人使用 (P1)

- **位置**：`src/app/main.cpp:89-95`、`src/media/imagewindow.cpp:293-300`、`src/core/ThreadPool/engine_context.cpp:34-74`
- **证据**：

```cpp
// main.cpp：全进程一个 EngineContext（ImageEditor 档，hw = hardware_concurrency）
auto engine_cfg = vistella::tp::EngineContext::make_profile(Profile::ImageEditor);
vistella::tp::EngineContext engine(std::move(engine_cfg));   // 8 核 → 7+8+16+2+8 线程 + 1 gpu = 42 线程
{ EngineContext::ScopedCurrent scope(&engine); }             // 之后 current() 全项目零调用 → 该 engine 空转

// imagewindow.cpp：每个窗口再造一个
m_engine = std::make_unique<EngineContext>(make_profile(Profile::ImageEditor, 2));
// hw=2 → interactive 1 / background 2 / io 4 / ai 1 / offline 2 + gpu 1 = 11 线程/窗口
```

- **现象**：8 核机器空载启动即有 ~42 个池线程（全部阻塞在 `cv_.wait`），打开 5 个图再 +55 线程；每线程还有 16384 槽的本地 deque（config.hpp:77，约 128KB）+ 默认 1MB 栈 → 光虚拟内存就 ~100MB。线程数 = 核数×6 级别，context switch 和调度抖动会直接吃掉图像处理的实时性；`EngineContext::current()`/`set_current()` 是设计给“任意模块拿全局池”的入口，但生产代码没有一处调用（只有 main.cpp 自己 `ScopedCurrent`），说明全局池是**纯浪费**。
- **根因**：`default_thread_count` 对每个 role 都取 `n` 或 `2n`，5 个池叠加 5×n；同时 `ImageWindow` 把池当成窗口私有资源（生命周期绑窗口），与“进程级共享池”的设计初衷相反。
- **修复建议**：进程只创建一个 `EngineContext`（`main` 里注入 `ImageWindow`/`MediaDispatcher`/`AIManager`）；`default_thread_count` 改为总和上限 `max(2, n-1)`（interactive ≥1、background ≥1、io 2、ai 1、offline 1，其余共享）；`ThreadPool` 懒启动（第一个任务到来才起线程）。删掉 `ScopedCurrent` 或真正使用它。
- **关联**：CI-19（架构悬空）、`Async.h` 又用 Qt 全局 `QThreadPool`（第三个池）→ 三套线程设施并存。

### CI-07 崩溃恢复标记不可靠 + 无自动保存 (P1 / 数据丢失)

- **位置**：`src/core/SessionManager.cpp:47-56`、`src/app/main.cpp:178,196-200`、`src/app/mainwindow.cpp:295-298,3197-3214`
- **证据**：

```cpp
// main.cpp 启动即标脏；只有 closeEvent 成功才标干净
SessionManager::instance().setLastExitClean(false);
    ...
    if (splash.status() == QQuickView::Error) { MainWindow w; w.show(); return a.exec(); }  // ← 早退，跳过 Logger::shutdown 与所有收尾
    ...
// mainwindow.cpp closeEvent 末尾
SessionManager::instance().setLastExitClean(true);   // 写标记后还有析构/保存动作，此时崩溃也算“干净”
saveSession();                                       // 只有这里保存文件列表；没有任何 autosave
```

- **现象 / 复现路径**：
  1. **多实例**：项目没有单实例保护（全库无 `QLockFile`/`QSharedMemory`）。实例 A、B 同开：B 启动写 `false`，B 正常退出写 `true`，随后 A 崩溃 → 下次启动读到的 `true` → A 的未保存工作**不再提示恢复**；反之 A 正常退出后 B 崩溃 → 误报。两个实例还会互相覆盖 `Session/openFiles`（注册表/INI 最后写者赢）。
  2. **标记写后崩溃**：`closeEvent` 里先写 `true` 再做 `saveSession()`、`QMainWindow::closeEvent`、Qt 析构；这段期间任何崩溃（图像释放、驱动 unload，项目里已多次记录 DWrite/COM 异常）都会让标记停留 `true`。
  3. **无 `QSettings::sync()`**：Windows 注册表写入是异步落盘的，`setValue(false)` 后立即断电，键可能仍是上一次的 `true` → 不弹恢复。
  4. **QML 加载失败早退**（main.cpp:196-200）：直接 `return a.exec()`，跳过 `Logger::shutdown()`（异步日志尾部丢失）且 `lastExitClean` 保持 `false`，下次启动会对一个从未打开文件的会话弹恢复框。
  5. **最致命**：恢复流程只 `restoreOpenFiles()` 重开磁盘文件（main.cpp:234-263），**从不恢复未保存内容**；也没有 autosave（`settingsdialog.ui:183` 有个 `autoSave` 复选框，`settingsdialog.cpp` 里零引用）。所以“崩溃恢复”对用户真正的损失（未保存的修图）毫无帮助——这比 PS 的 “Auto-Recover” 是代差。
- **根因**：用单个全局布尔表达“进程是否干净退出”，缺少进程互斥、缺少原子写入、缺少版本；恢复的粒度是“文件路径清单”而非“文档状态”。
- **修复建议**：
  1. `QLockFile(ApplicationDataLocation + "/instance.lock")` 单实例；第二实例通过 `QLocalServer/QLocalSocket` 把 `argv` 转给主实例后退出（Qt 原生方案，见第 4 节）；
  2. 会话写 `QSaveFile` + JSON：`{"version":2,"instanceId":…,"files":[{path,active,unsavedAutoSavePath}]}`，`commit()` 原子替换；读时按 version 迁移；
  3. 每个 `ImageWindow`/`DocWindow` 在 dirty 后 30s 或每 5 分钟把当前文档（含图层/LUT）写 `AutoSave/<uuid>`；
  4. `main.cpp` 所有 `return` 路径走 `qScopeGuard`/RAII 统一执行 `saveSession + Logger::shutdown`；`setLastExitClean` 后显式 `QSettings::sync()`；
  5. 增加 `QApplication::commitDataRequest`（Windows 注销/关机）保存会话。
- **关联**：CI-28（格式无版本）、`RecentManager` 同用 QSettings 无 sync。

### CI-08 重新打开 pinned 文件会掉 pin (P1)

- **位置**：`src/core/recentmanager.cpp:60-111`
- **证据**：

```cpp
m_entries.erase(std::remove_if(... x.filePath == filePath ...), m_entries.end());  // 旧条目连 pinned 一起删
RecentEntry e;
...
e.pinned = false;   // 注释：“新打开的文件默认 unpinned. 钉住必须用户主动调 togglePin.”
```

- **现象**：用户在最近列表里把 `a.png` 固定（pinned=true），之后再次打开/保存 `a.png`（任何 `touchOpen`）→ 该条目的 pin 被清除，固定区消失；`HomePage` 上钉住的入口“自己掉了”。可见的状态丢失（P1，用户会以为固定功能失效）。
- **根因**：erase 前没有保存旧条目的 `pinned`（`findIndex` 已有现成实现却在 `touchOpen` 里没用）；注释把“新文件默认 unpinned”误解为“已存在文件也重置”。
- **修复建议**：`touchOpen` 开头 `int old = findIndex(m_entries, filePath); bool wasPinned = old>=0 && m_entries[old].pinned;` 然后 `e.pinned = wasPinned;`；同时保留旧 `lastEditTime`（现在每次打开都重置编辑时间，`ColLastEditTime` 语义变成“最后打开时间”，与列名不符）。
- **关联**：`models/recentlistmodel.cpp` 的 `PinnedRole`/委托点击。

### CI-09 可选池为 null 时解引用 (P1)

- **位置**：`src/core/ThreadPool/engine_context.cpp:145-165`
- **证据**：

```cpp
IExecutor& EngineContext::gpu()  noexcept { return *gpu_; }                 // enable_gpu_thread=false → UB
PoolStats& EngineContext::stats_gpu() noexcept { return gpu_->stats(); }    // 同上
```

- **现象**：`EngineContext::Config::enable_gpu_thread=false`（头文件默认 true，但 `Config` 是公开结构、`make_profile` 之外可由调用方改）时，任何 `engine.gpu()` 调用立即空指针解引用崩溃。`ai()`/`offline()` 都做了回退，`gpu()` 漏了。
- **修复建议**：`gpu()` 与 `ai()` 一致回退 `io()`，或返回 `SerialExecutor*`；`stats_gpu()` 同理。补单元测试覆盖 4 个 `enable_* = false` 组合。
- **关联**：目前 `gpu()` 零调用，属潜伏型。

### CI-10 线程池构造超时后进入僵尸态，析构 join 挂死 (P1)

- **位置**：`src/core/ThreadPool/thread_pool.cpp:42-57`、`:383-385`
- **证据**：

```cpp
// 构造：自旋等 worker 注册，超时后“放弃”
for (int spins = 0; started_count_.load(...) < (int)n; ++spins) {
    if (spins > 1'000'000) {                       // ~1s 没进展
        shutting_down_.store(true, std::memory_order_release);
        start_cv_.notify_all();
        return;                                    // 构造函数直接返回！对象已交付给调用方
    }
}
...
// worker：等待 ready_，而不是等待 shutting_down_
{ std::unique_lock<std::mutex> lk(start_mu_);
  start_cv_.wait(lk, [this] { return ready_.load(std::memory_order_acquire); }); }
```

- **现象**：在 CI 机器/杀软扫描/进程被 Suspend 时线程创建可能 >1s，构造函数“优雅放弃”并返回半初始化对象：`ready_` 永远 false，已启动的 worker 永久阻塞在 `start_cv_.wait`（`notify_all` 在 worker 进入 wait 前发出，或谓词不匹配直接丢失）；此后 `~ThreadPool → shutdown(true)` 用 `std::thread::join()` 等这些 worker → **永久挂死**，退出时进程卡住。更糟的是调用方拿到“成功构造”的池，`submit()` 因 `shutting_down_` 一律返回 false，所有异步操作静默退化为同步/无结果。
- **根因**：失败路径没有把 `ready_` 置位（worker 的退出谓词与构造失败条件不是同一个变量），且“放弃构造”不是合法状态却仍返回对象。
- **修复建议**：失败时 `{ lock(start_mu_); ready_.store(true); } start_cv_.notify_all();` 再置 `shutting_down_` 让 worker 走正常退出；或直接 `throw std::runtime_error` 让 `EngineContext` 回退单线程模式。另把自旋改成 `start_cv_.wait_for(2s)`（构造期无写竞争，安全）。
- **关联**：CI-06（线程多 → 创建慢 → 更易触发）、`EngineContext` 未对池构造失败做任何处理。

### CI-11 异步日志的 block 溢出策略会阻塞调用线程（可能是 UI） (P1)

- **位置**：`src/logger/Logger.cpp:416-420`、`:522-539`；承诺见 `src/logger/Logger.h:7`
- **证据**：

```cpp
// Logger.h:7 —— 设计承诺
//    - 业务线程只 push 到 spdlog async queue, 不阻塞
auto logger = std::make_shared<spdlog::async_logger>(
    "vistella_logger", sink, spdlog::thread_pool(),
    spdlog::async_overflow_policy::block);       // ← 队列满时生产者阻塞，直到消费线程腾出空间
...
void Logger::flush() { if (s.logger) { s.logger->flush(); std::this_thread::sleep_for(600ms); } }
void Logger::shutdown() { if (s.logger) { s.logger->flush(); std::this_thread::sleep_for(700ms); s.logger.reset(); } }
```

- **现象**：`init_thread_pool(8192, 1)` 只有 **1 个消费线程**，且 pattern 每条都格式化时间戳。Debug 级别下项目日志量很大（`Throttle::trigger`、文件扫描、`LOG_DEBUG` 遍地），一旦 8192 条队列填满（批量打开 200 张图 + 拖动滑块），任何打日志的线程（包括 UI 线程的 `LOG_INFO`）会被 `block` 策略**同步阻塞**，直到后台消费线程腾出槽位 → UI 卡顿/假死；若消费线程正写慢磁盘或 `flush_on(warn)` 同步刷盘，阻塞可达秒级。
- **根因**：把“不阻塞”写进文档却选了最阻塞的 overflow 策略；用 `sleep(600/700ms)` 作为“等队列排空”的兜底，把异步日志在退出/flush 路径上重新变成同步且固定耗时。
- **修复建议**：`overrun_oldest`（丢最旧、绝不阻塞业务线程），Critical 单独挂同步 `basic_file_sink`；队列加大或换 quill；`flush()` 用 `spdlog::thread_pool()->flush()`（等待排空，无需 sleep）；配置热更新走 `SizeDailySink::updateConfig()`（已实现但零调用，Logger.cpp:58-69），不要重建 logger + sleep。
- **关联**：CI-38（Throttle 每次 trigger 打日志）、CI-30（Qt 消息处理器也在写文件、无锁）。

### CI-12 中文/非 ASCII 日志目录导致完全无日志 (P1)

- **位置**：`src/logger/Logger.cpp:139-151`（目录创建）、`:216-249`（文件打开）；`src/app/main.cpp:124-131`
- **证据**：

```cpp
// defaultLogDir(): Windows 拼接 "%LOCALAPPDATA%\\Vistella\\Vistella\\logs"  (std::string, 窄字符)
void ensureLogDir_() {                                   // MKDIR(p) = _mkdir(p)：窄字符 ANSI API
    std::string acc;
    for (char c : logDir_) { acc.push_back(c); if (c == '/' || c == '\\') MKDIR(acc.c_str()); }
    MKDIR(acc.c_str());
}
fileHelper_.open(path, false);                           // spdlog file_helper → fopen(窄字符)
...
// main.cpp
vistella::Logger::init("Vistella", logDir.toStdString(), LogLevel::Debug, 4*1024*1024);  // 返回值被丢弃
```

- **现象**：`logDir` 来自 `QStandardPaths::writableLocation(AppLocalDataLocation)`（main.cpp:124-126），Windows 上是 `C:/Users/<用户名>/AppData/Local/Vistella/Vistella/logs`。中文用户名时 `toStdString()` 得 UTF-8 字节，`_mkdir`/`fopen` 按当前 ANSI 代码页解释 → 目录创建失败、`file_helper_.open` 抛 `spdlog_ex` → `Logger::init` 返回 false（`:452-455`），`main.cpp:127` 忽略返回值继续 `LOG_INFO(...)` —— 所有日志因 `logger == nullptr` 静默 return（`Logger.h:152-153`）。用户/技术支持**完全拿不到日志**，且无任何提示。
- **根因**：Logger 是“不依赖 Qt 的 SHARED 库”，路径全走窄字符 `std::string`，没有 Windows 宽字符转换；`ensureLogDir_` 手写逐字符 mkdir 也不处理盘符/UNC/中文。
- **修复建议**：目录创建改 `std::filesystem::create_directories(std::filesystem::u8path(logDir))`（C++17）；文件打开用 UTF-8→`std::wstring` + `_wfopen` 的自定义 `file_helper`（spdlog 1.15 无此能力）；`Logger::init` 失败写 stderr + `OutputDebugString`，并让 `main.cpp` 至少 `qWarning`/弹一次提示。另 `LOGGER_ENABLE_QT` 默认 OFF（logger/CMakeLists.txt:24,51-55）→ `LOG_*_QT` 宏从未编译过（全项目零调用），Qt 集成层等于未验证。
- **关联**：`main.cpp:59` 的 `qDebug` 备用通道写死 `D:/vistella_qdebug.log`，与中文路径问题叠加时两个通道都可能失效。

### CI-13 SQLite 连接/事务/迁移：Qt 用法有系统性遗漏 (P1)

- **位置**：`src/user/UserDatabase.cpp:25-56`（连接生命周期）、`:58-100`（schema）、`:165-246`（读取）
- **证据**：

```cpp
void UserDatabase::close() {
    if (m_db.isOpen()) m_db.close();
    if (QSqlDatabase::contains(m_connName))
        QSqlDatabase::removeDatabase(m_connName);   // m_db 成员仍持有该连接引用！
}                                                   // Qt: "connection is still in use, all queries will cease to work"
...
bool UserDatabase::ensureSchema() {                 // 3 条 CREATE 都没有事务，也没有 PRAGMA
    QSqlQuery q(m_db);
    q.exec("CREATE TABLE IF NOT EXISTS users (...)");  // 无 user_version / 无 ALTER 迁移
    ... // tokens 表声明 FOREIGN KEY ... ON DELETE CASCADE，但 SQLite 默认 foreign_keys=OFF，从未开启
}
...
User UserDatabase::loadUser(const QString &id) {
    if (q.exec() && q.next()) return userFromQuery(q);  // exec 失败（锁库/IO 错误）被吞，返回空 User
    return User{};                                      // 调用方看到的是"没有这个用户"，不是错误
}
```

- **现象 / 影响**：
  1. `close()` 顺序错误 → Qt 输出 `QSqlDatabasePrivate::removeDatabase: connection '...' is still in use`，连接实际保留在全局 registry；随后 `open()` 再 `addDatabase` 同名 → 又一条警告，连接句柄泄漏。`~UserDatabase` 每次都走这条路径。
  2. 没有 `PRAGMA foreign_keys=ON` → `tokens` 外键/级联删除是装饰；`DELETE FROM users` 后 token 残留，若同 id 用户被重建会“复活旧 token”。
  3. 没有 `journal_mode=WAL` + `busy_timeout`：双开时两个连接写同一 `user.db` → `SQLITE_BUSY` 立即失败；`exec()` 返回值在 `loadUser*/loadAccessToken/allUsers/recentLogins` 里大多被忽略 → 表现为“登录态突然丢失/最近登录为空”，无错误上报。
  4. **无 schema 迁移**：只有 `CREATE TABLE IF NOT EXISTS`。下版本给 `users` 加列时老库不升级，新代码 `SELECT` 新列失败且被吞 → “登录后信息空白”。
  5. token 明文：`saveToken` 直接写 `access_token/refresh_token`（UserDatabase.cpp:199-220），注释也承认“阶段 5 加密”。对已发布产品这是凭据泄露面。
  6. `logLogin` 实现了但 `UserManager` 从不调用（全项目仅定义处出现）→ `login_logs` 永远空表，审计能力为 0。
- **修复建议**：`close()` 里先 `m_db = QSqlDatabase();` 再 `removeDatabase`；`open()` 成功后立即 `PRAGMA foreign_keys=ON / journal_mode=WAL / busy_timeout=5000`；用 `PRAGMA user_version` 建迁移；所有 `exec()` 失败走统一 `Result<void>` 并打日志；token 用 DPAPI/`QKeychain` 加密入库；`login/registerLocal/logout` 补 `logLogin` 调用。
- **关联**：`UserManager.cpp:77` 把 token 前 8 字符写日志；CI-34（AuthClient mock）使这些问题在接真 OAuth 后立刻升级。

## 3. 功能完整度 vs Photoshop

| 功能项 | Photoshop 行为 | 本项目现状 | 差距 | 优先级 |
|---|---|---|---|---|
| 崩溃恢复（未保存内容） | 定时 Auto-Recover，崩溃后开机弹“恢复未保存文档”，能还原图层/历史 | 只有“重开已保存文件路径”，未保存内容全丢；`autoSave` 复选框未接线 | 缺失 | P0 |
| 会话恢复（已保存文件 + 布局） | 恢复上次打开文档、工作区布局、激活文档 | 恢复文件列表 + activeFile；窗口布局另存 QSettings（`window/mode`）；无版本 | 部分 | P1 |
| 最近文件 | 支持 pin、按工作区过滤、缺失文件置灰、右键“在资源管理器中显示” | pin/删除/数量上限有；重新打开掉 pin（CI-08）；失效文件不清理；无右键菜单 | 有但错 | P1 |
| 主题（浅色/深色/系统） | 跟随系统 + 多套配色，实时切换全部面板 | 浅/深两套 + QSS 占位符；切主题只刷 MainWindow/HomePage，ImageWindow 自绘不刷新 | 部分 | P2 |
| 多语言 | 全 UI 即时切换 | `LanguageManager` 有 3 语言；运行时切换只有 MainWindow 重译（CI-27） | 部分 | P2 |
| 用户登录 / 云同步 | Adobe ID 登录、云文档 | `UserManager` 本地 SQLite 模拟登录（密码被忽略）、`AuthClient` 全 mock、token 明文 | 缺失（stub） | P2 |
| AI 功能 | Neural Filters / 生成式填充 / 主体选择 | 仅 `IAIModel` 接口 + `AIManager` 空注册表，无 ONNX 运行时、无模型 | 缺失（仅骨架） | P3 |
| 日志与诊断 | 结构化日志 + 崩溃报告 | spdlog 异步双维度切分、级别可调；但存在 CI-11/12/31 数据与可靠性问题 | 部分 | P2 |
| 任务调度（并行滤镜/渲染） | GPU/CPU 自适应、取消、进度 | 自研 6 池 + work-stealing，无进度、取消不生效、任务泄漏、本地队列 bug | 有但错 | P0/P1 |

## 4. Qt 能力边界分类

| 需求点 | 分类 | 说明 | 建议方案 |
|---|---|---|---|
| 单实例 + 命令行转发 | QT-NATIVE | `QLockFile` 判重，`QLocalServer/QLocalSocket` 转发参数（Qt 官方 localsocket 范例） | 实现 `SingleInstanceGuard`（约 100 行），放 `src/core` |
| 会话/自动保存原子写 | QT-NATIVE | `QSaveFile`（ICC 模块已在用）+ `QJsonDocument`；断电安全靠 `commit()` rename | Session/AutoSave 全部改 `QSaveFile` |
| 崩溃后未保存状态 | QT-EFFORT | Qt 无进程内存快照能力（`QSessionManager` 只能存路径类数据）；需自研“定时序列化文档状态（图层+LUT）”，约 3–5 人日 | AutoSave 增量写 + 恢复 UI |
| 跨线程回调生命周期 | QT-NATIVE | `QMetaObject::invokeMethod(receiver, functor, Queued)` 会在 receiver 析构时撤销；`QPointer` 只保证“判空”，不保证“判空后仍存活” | 统一 `invokeMethod(receiver,…)`；后台线程不要读 `QPointer` |
| 线程池/并行 for | QT-3RD | 自研 work-stealing 已验证有 bug（CI-02）。成熟方案：oneTBB（Apache-2.0，`task_arena`+`parallel_for`）、Taskflow（MIT，DAG 原生），或退回 `QThreadPool` + `QtConcurrent::mapped`（原生、无优先级/无 DAG） | 推荐 oneTBB 替换调度层（约 2–3 人日接入） |
| 优先级调度 | QT-NATIVE | `QThreadPool::start(runnable, priority)` 只有 1..N 数值优先级且仅对排队任务生效；本项目自研三档优先级只在 injected 队列生效（本地 deque 完全绕过） | 若保留自研池：本地 deque 也按优先级分 3 条，否则形同虚设 |
| 取消长时间任务 | QT-LIMIT | `QFuture` 无抢占、`QtConcurrent::run` 无法取消；自研任务同样只能协作式取消 | 采用“代次号/丢结果”模式（项目 `m_currentTaskId` 已是对的方向）+ 任务内每 tile 检查 token |
| 阻塞等待带超时 | QT-LIMIT | Qt 6 `QFuture::waitForFinished()` 无 timeout（`Async.h:123` 注释已指出）；`QSemaphore::tryAcquire(timeout)` 可替代 | 自建 `std::future::wait_for` 包装，或 `QSemaphore` |
| 日志（异步、轮转、多 sink） | QT-3RD | 已用 spdlog（MIT）；若需更低延迟可选 quill（MIT，无锁队列） | 保留 spdlog，改 `overrun_oldest` + 自定义 sink 用 `_wfopen`/`u8path` |
| SQLite 本地库 | QT-NATIVE | `Qt6::Sql` 已有；需补 3 条 PRAGMA、`QSqlDatabase::transaction()` | 见 CI-13 |
| 网络 OAuth/HTTPS/TLS | QT-NATIVE | `QNetworkAccessManager` 原生支持 HTTPS、`QSslConfiguration`、`setTransferTimeout`、`sslErrors`；目前 `src/network` 连 `Qt6::Network` 都没链接 | 接 `Qt6::Network`（CMake 改动），实现 QNAM 封装；OAuth 需系统浏览器 + 回环回调（`QDesktopServices`+`QLocalServer`） |
| ONNX 推理 | QT-3RD | Qt 无推理能力；需 `onnxruntime`（MIT）或 OpenCV DNN（Apache-2.0，项目已有 OpenCV，可先 `cv::dnn`） | 先 `cv::dnn::readNetFromONNX` 满足 phase 1，再按需上 ORT |
| 真·实时线程（音频） | QT-LIMIT | Windows 无硬实时，只能 MMCSS + `timeBeginPeriod`；当前实现还传错 `pTaskIndex`（CI-35） | 不做音频工作站可删 `RealtimeThread`；要做则修参数并链 `avrt.lib` |
| QSS 主题切换 | QT-NATIVE | `qApp->setStyleSheet` 全量 re-polish；更细粒度用 `QPalette` + `QStyle::polish` / `QProxyStyle` | QSS 缓存 + 只对受影响窗口 `setStyleSheet`；不要每次读盘 |

## 5. 设计问题

**5.1 线程模型：三层并存的执行设施，互不知情。** `EngineContext` 5 池（自研 std::thread）、`Async.h` 的 `QThreadPool::globalInstance()`、`QtConcurrent`，再加 `SerialExecutor`/`RealtimeThread`。同一个滤镜可能跑在自研 background 池，另一个跑在 Qt 全局池；两套线程数相乘；取消语义完全不同。**建议**：明确唯一执行入口（`EngineContext`），`Async.h` 改为薄适配器转发到 `interactive()/background()`；删除 `RealtimeThread`/`qt_bridge`。

**5.2 `Async.h`/`Result.h`：设计意图好，落地为零。** 全 `src/` 只有 `ImageProcessorAsync.cpp` 用 `Async::run` + `Result`；`Q_TRY/Q_TRY_VAL/ErrF` 零调用且宏本身编译不过（CI-24）；其余 IO/保存/加载仍是 `bool + QString* err`、`emit openFailed`、`nullptr` 三套并存。`Error` 只有 `ErrorCode+std::string`，没有文件名/行号/上下文/可翻译文本，UI 直接展示英文；`Result<T>` 无 `[[nodiscard]]`，忽略错误无人发现。**建议**：要么把 `Result` 提升为唯一错误契约（`[[nodiscard]]`、`Error` 加 `context`、`and_then/map`），要么删掉以免“看起来统一其实没人用”。

**5.3 所有权与生命周期：裸指针 + 注释约定。** `IWorkspace* openFile(parent)`：`MediaDispatcher` 会 `setParent(parent)`（MediaDispatcher.cpp:139-143），但调用方传 `nullptr` 就泄漏；`Task::token/gen_src` 是“not owned”裸指针（executor.hpp:93）；`CancellationToken::create_child()` 返回裸 `new`，父子互相引用却无所有权表达（CI-17）；`IAIModel*`/`IModule*` 注册表全是裸指针，`registerModel` 替换旧对象“交给 owner”但没有任何机制验证 owner 还活着。**建议**：跨接口对象一律 `shared_ptr`/`QPointer`；注册表用 `QPointer`（QObject）或 `weak_ptr`。

**5.4 QObject 线程亲和性未建模。** `EngineContext`/`ThreadPool` 不是 QObject（好事），但任务里可能触碰 QObject：`ImageProcessor::applyLutAsync(callback)` 明确“callback 在后台线程触发”（imageprocessor.cpp:1041），把“必须自己切线程”的责任丢给调用方；`AdjustmentPanel` 在后台线程读 `QPointer<ImageWindow>`（AdjustmentPanel.cpp:718,733），Qt 文档明确 `QPointer` 非线程安全，与窗口析构并发时是 data race。`Throttle`/`RecentManager`/`ThemeManager` 都是 QObject 单例，其“首次调用线程”隐式决定亲和线程，没有断言保护。**建议**：core 提供 `tp::invokeOn(QObject*, fn)`；后台线程禁止直接读 GUI 对象。

**5.5 数据竞争：后台 `render()` 读 UI 改写的图层栈。** `LayerStack::render()` 标 `const` 但读 `m_layers`/`Layer::image`；`renderAsync` 在 background 线程调用它（LayerStack.cpp:1793），UI 线程此时可以改 opacity/增删图层/绘制。`m_currentTaskId` 只解决“旧结果丢弃”，不解决“并发读写同一 cv::Mat/vector”。**建议**：`renderAsync` 提交前 snapshot 图层元数据（`shared_ptr<const Layer>` + COW），或渲染期间加 `QReadWriteLock`（UI 写、后台读）。

**5.6 单例析构顺序 vs Logger/QApplication。** `ThemeManager/RecentManager/MediaDispatcher/AIManager/LanguageManager/UserManager` 都是函数内 `static`，在 `main` 返回后才析构，晚于 `Logger::shutdown()`（main.cpp:315）与 `QApplication a`（main.cpp:81）。`~UserManager → ~UserDatabase → QSqlDatabase::removeDatabase` 在 Qt 全局句柄已销毁后执行，Qt 会 qWarning（写入已被销毁的 qInstallMessageHandler 目标 `QFile`），极端情形崩溃。**建议**：单例改显式 `initialize()/shutdown()`（main 末尾逆序），或用 `Q_GLOBAL_STATIC` + `aboutToQuit` 清理；`Logger::shutdown()` 放最末。

**5.7 空抽象层（死代码）占比高。** `MediaDispatcher` 路由、`IModule/IWorkspace`、`IAIModel`、`AuthClient`、`HttpClient`、`RealtimeThread`、`SpscRing`、`parallel_for/task_graph` 全部零生产调用。它们增加编译单元/心智负担，却无法被业务测试覆盖。**建议**：Lead 层面决策“要么接线要么删除”，不要保留三条并行空管线。

**5.8 可测试性：全局单例 + 硬编码路径。** `Logger`、`ThemeManager`、`SessionManager`（QSettings 全局）、`RecentManager`、`UserManager`（路径由 main 传）、`MediaDispatcher` 均无法注入替身；`ThemeManager::resolveQss` 直接读 `qApp` 目录，`Logger::defaultLogDir` 读环境变量。**建议**：把“配置源”抽成 `ISettingsStore`/`ILogSink`，单例只持接口指针。

**5.9 线程统计有、但不可观测。** `PoolStats` 很完整（wait/exec/peak/steal/reject/exceptions），`stats_report()/health_check()` 也有，但**没有任何 UI/日志消费**（`stats_*`、`stats_report` 零调用）；`last_exception_` 只存最后一次、从不读取 → 线程池“静默失败”：任务抛异常只有计数器 +1。**建议**：`last_exception_` 改 `vector<ExceptionRecord>{task name, exception_ptr, time}` 并在 `health_check` 输出；`SettingsDialog` 加“池统计”面板。

**5.10 性能细节**：`EngineContext` 每窗口 11 线程（CI-06）；`IModule::canOpen` 每次构造 `ModuleInfo`（CI-21）；`ThemeManager::globalStyleSheet()` 每次切换都读盘 + 25 次 `QString::replace`；`Async.h::run` 用全局 `QThreadPool`；`Logger::get()` 每条日志一次 `shared_ptr` 原子增减。单点不大，叠加在“拖动滑块 100Hz 提交任务”的场景上就明显。

## 6. 修复建议（按优先级）

**P0 立即修（本周，约 2–3 人日）**
1. `run_task` 所有路径 `delete t`（CI-01，30 分钟；改完用任务计数器 + ASAN 验证），并排查 `LayerStack::renderAsync`/`AdjustmentPanel` 的 clone 捕获是否仍泄漏。
2. 修 `ChaseLevDeque::pop()`（CI-02，2 小时）；临时规避可让 `from_worker` 分支也走 `push_injected`（1 行），再排期换 oneTBB。
3. `LayerStack::renderAsync` 改接收者 context + `ImageWindow` 回调改 `QPointer`（CI-03，半天）；重点回归：大图异步渲染中关 tab/关程序。
4. Session 恢复链路：加 `QSaveFile` 原子写 + `QLockFile` 单实例（CI-07 的 1/2/3 步，1 人日）；至少先保证“崩溃提示”不会被多实例/未 sync 破坏。

**P1 本轮修（约 3–5 人日）**
5. 线程池关闭协议：`queued_cv_.notify_all()`、`shutdown` 清理 pending、`SerialExecutor::run_sync` 改 promise（CI-04/05/14）。
6. 池数量重构：进程单例 EngineContext + 懒启动（CI-06，1–2 人日，需要改 `ImageWindow` 构造/析构与 `main.cpp` 注入）。
7. `RecentManager::touchOpen` 保留 pinned、load 时过滤失效路径（CI-08/26）。
8. Logger：`overrun_oldest`、中文路径 `_wfopen`、`init` 失败可见、切分重复行修复（CI-11/12/31）。
9. `UserDatabase`：`close()` 释放句柄再 `removeDatabase`、补 3 个 PRAGMA、加 `user_version` 迁移（CI-13）。

**P2 后续（约 3–5 人日）**
10. `Result` 加 `[[nodiscard]]`、删/修 `Q_TRY`，统一 `Error` 上下文；`Async::cancel` 改为代次丢弃（CI-24/25/22）。
11. `MediaDispatcher` 决策：接线到 `MainWindow::openFileByPath/restoreSessionFiles`，统一 `ImageWorker::openFile`（CI-19/20）。
12. 主题/语言联动：`themeChanged` 全窗口订阅 + retranslate 全窗口（CI-18/27）。
13. 网络/AI：接 `Qt6::Network` 实现 `HttpClient`（含 `setTransferTimeout`/`sslErrors`），`AuthClient` stub 返回 `NotConfigured`；ONNX 先走 `cv::dnn`（CI-33/34）。

**架构级重构（另立专项）**
14. 用 oneTBB/Taskflow 替换 `vistella_threadpool`（删除 `work_stealing_queue/parallel_for/task_graph/cancellation` 自研件），或先包一层 `IExecutor` 适配再逐步迁移；收益：CI-02/04/16/17/29 一整类问题消失。
15. 单例生命周期统一改为显式 init/shutdown，`Logger` 最后关；引入 `ISettingsStore` 做可测试化（§5.6/5.8）。

## 7. 需要 Lead 协调的跨模块问题

- **CI-01/CI-06 影响 media 全模块**：修 `ThreadPool` 后必须回归 `ImageWindow` 异步渲染、`AdjustmentPanel` 拖动调参、`RenderGraph::executeParallel`。media 审查员会从使用侧看到同类问题：`RenderGraph.cpp:143-159` 忽略 `submit_fn` 返回值并 `QThread::msleep(1)` 忙等 → 任务被拒/被 DropOldest 丢弃时**主线程死循环**，建议合并到同一份缺陷记录。
- **CI-03 需 media 审查员确认**：同样“后台线程捕获裸 `this`”模式在 `AdjustmentPanel.cpp:718-744`（`QPointer` 跨线程读）、`ImageProcessor::applyLutAsync(callback)`（callback 在后台线程）都存在；属 media 侧代码但根因是 core 没提供线程安全回调桥，建议 core 提供 `tp::invokeOn(QObject*, std::function<void()>)` 后统一改。
- **构建/CMake**：`src/network` 需新增 `Qt6::Network` 链接；`vistella_threadpool` 若启用 Windows 实时线程需链 `avrt`；`CMakeLists.txt:202-204` 的“暂不 link vistella_threadpool”注释与事实不符（`vistella_app → vistella_core → vistella_threadpool` 是传递链接）；`src/app/CMakeLists.txt:5` 与根 `CMakeLists.txt:200` 都编译 `main.cpp`（重复编译，需清理）。
- **单实例/文件锁**（CI-07）会改 `main.cpp` 启动流程，需与 `app` 审查员协调（启动屏 + splash timer 路径）。
- **`ThemeManager`/QSS**：`ui` 审查员若发现自绘控件颜色硬编码，需要 core 提供“调色板变化通知”而非全局 `setStyleSheet` 重刷；建议与 `ui`/`media` 对齐统一入口（`ThemeManager::themeChanged` + 各窗口 `applyTheme()`）。
- **`UserManager` 与 `AuthClient`**：两者目前完全没接线（`UserManager::login` 不走 `AuthClient`），若后续接 OAuth，`UserDatabase::saveToken` 明文存储 + `loginWithToken` 把 token 前 8 字符写日志（UserManager.cpp:77）需一起处理（安全项）。
- **测试缺口**：`tests/` 有 `tst_MediaDispatcher`/`tst_F_O_Throttle` 等，但没有覆盖 `ThreadPool` 被执行任务析构、`Queued` 回调生命周期、Session 崩溃恢复。建议 Lead 在总报告点出“core 最危险的 CI-01/02 无法被现有测试发现”，修复后必须补 ASAN + 线程池压力测试。
