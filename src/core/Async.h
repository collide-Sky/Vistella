#ifndef vistella_ASYNC_H
#define vistella_ASYNC_H

// =============================================================
// Async — 异步任务基础架构 (阶段 1 前置优化 C.1, 2026-09-03)
//
// 设计动机:
//   阶段 1+ worker (imageWorker / audioWorker / videoWorker / visionWorker)
//   大量计算密集操作 (OpenCV 滤镜, 音频解码, 视频转码, AI 推理), 同步执行会卡 UI.
//   用 QtConcurrent + QFutureWatcher 提供轻量级异步 API, 不用每处自己写 QThread.
//
// 设计原则 (成熟方案):
//   1. 全局唯一 QThreadPool (从 QApplication::threadPool() 拿, 不用自己 new)
//      - 任务数随负载自动调节 (idealThreadCount 上下)
//      - 线程回收 / 异常处理由 Qt 负责
//      - 整个 app 共享一个 pool, 避免每个 worker 各开 pool 浪费线程
//
//   2. Async::run<Result<T>>() — 函数级封装, 同步 API + 异步执行
//      - 调用方: Async::run<MyResult>([]() { ... })
//      - 内部用 QtConcurrent::run + QFutureWatcher 在调用方线程 emit result
//      - 调用方不需要懂 QThread / QtConcurrent
//
//   3. Async::runOn(QObject*, lambda) — 配合 QObject 生命周期, 自动 disconnect
//      - 接收 QObject* (e.g. widget), QObject 销毁时 future 自动 cancel + 不 emit
//      - 用 QFutureWatcher::setParent(QObject*) 模式 (Qt 5.15+ 标准做法)
//
//   4. 错误传递 — 用 Result<T> (见 Result.h), 跟 worker 错误处理统一
//      - Async::run<Result<T>>(...) 返 QFuture<Result<T>>, 失败带 Error
//
//   5. 进度报告 — 不用 (阶段 1 简单, 进度走 log + status bar; 阶段 7+ AI 推理再考虑)
//
// 用法示例 (阶段 1 imageWorker 滤镜):
//     auto future = Async::run<vistella::Result<cv::Mat>>(
//         [img = m_current]() -> vistella::Result<cv::Mat> {
//             cv::Mat out;
//             cv::GaussianBlur(img, out, cv::Size(5, 5), 1.5);
//             return out;
//         });
//     auto *watcher = Async::watch(future, this,
//         [this](const vistella::Result<cv::Mat> &r) {
//             if (r.isOk()) setCurrentImage(r.value());
//             else QMessageBox::warning(this, "滤镜失败", QString::fromStdString(r.error().message));
//         });
//
// 注意:
//   - 调用方负责 watcher 的生命周期 (delete 或 setParent)
//   - future 本身是值类型, 不需要 delete
//   - 多线程安全: 回调 lambda 在 watcher 所在线程 (一般是 UI 线程) 执行
// =============================================================

#include "Result.h"

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QThreadPool>

#include <functional>
#include <utility>

namespace vistella {

// ---- 任务提交 ----

// run<T>(fn) — 异步执行 fn, 返 QFuture<T>
//   T 可以是任意 movable 类型 (cv::Mat, Result<cv::Mat>, 自定义 struct 等)
//   fn 在 QApplication::threadPool() 的工作线程执行
template <typename T>
inline QFuture<T> run(std::function<T()> fn)
{
    return QtConcurrent::run(QThreadPool::globalInstance(), std::move(fn));
}

// 重载: 直接传 lambda (避免显式写 std::function)
template <typename Fn>
inline auto run(Fn &&fn) -> QFuture<typename std::invoke_result<Fn>::type>
{
    using R = typename std::invoke_result<Fn>::type;
    return QtConcurrent::run(QThreadPool::globalInstance(), std::forward<Fn>(fn));
}

// ---- 任务监听 (带生命周期管理) ----

// watch(future, context, callback) — 在 context 线程上回调 result
//   context 一般是 QWidget* / QObject*, context 销毁时 watcher 自动 disconnect
//   返回 QFutureWatcher* (调用方负责 delete 或 setParent)
//
//   callback 签名: void(T) — T 是 future 的 result 类型
//   失败时: 如果 T = Result<X>, callback 收到 Result, 自己判 isOk
//           如果 T = 普通类型, 异常会走 QtConcurrent 的异常路径
//             (Qt 5.15+ 抛到 future, callback 调 future.result() 时触发)
//
//   实现注意:
//     QFutureWatcher<T>::finished 是无参数信号, callback 自己从 watcher->future()
//     拿 result. 这样避开 Qt 6 connect 对 "signal 带类型参数" 的推断困难.
//     callback 必须显式 `-> void` (Qt 6 connect 要求).
template <typename T, typename Ctx, typename Fn>
QFutureWatcher<T> *watch(const QFuture<T> &future, Ctx *context, Fn &&callback)
{
    static_assert(std::is_void_v<decltype(callback(std::declval<T>()))>,
                  "Async::watch callback must return void, e.g. [](int v) -> void { ... }");
    auto *watcher = new QFutureWatcher<T>(context);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, context,
        [watcher, cb = std::forward<Fn>(callback)]() -> void {
            // result() 第一次调用 move 走值, T 必须 movable (Result<T> / cv::Mat 等都是)
            cb(watcher->future().result());
        });
    watcher->setFuture(future);
    return watcher;
}

// ---- 任务取消 ----

// cancel(watcher) — 取消未开始的任务
//   注意: 已在执行的任务无法取消 (Qt 不支持抢占式 cancel)
//   用法: 用户点"取消"按钮时调
template <typename T>
inline void cancel(QFutureWatcher<T> *watcher)
{
    if (watcher) {
        watcher->cancel();
        watcher->waitForFinished();   // 等当前任务结束 (Qt 6 无 timeout 参数, 阻塞)
    }
}

// ---- 任务状态查询 ----

// isRunning(future) — 简单包装
template <typename T>
inline bool isRunning(const QFuture<T> &future)
{
    return future.isRunning();
}

// progress(future) — 0.0 ~ 1.0
template <typename T>
inline qreal progress(QFuture<T> &future)
{
    if (!future.isStarted() || future.isFinished()) {
        return future.isFinished() ? 1.0 : 0.0;
    }
    return future.progressValue();
}

// ---- 任务完成等待 (UI 线程慎用, 会卡 UI) ----

// wait(future) — 同步等待 (UI 线程慎用, 会卡 UI)
//   Qt 6 的 QFuture::waitForFinished() 不支持 timeout 参数, 阻塞到任务结束
//   UI 线程调用会卡 UI — 仅用于测试或非 UI 线程
//   非 const 引用: Qt 6 QFuture::waitForFinished() 不是 const
template <typename T>
inline void wait(QFuture<T> &future)
{
    future.waitForFinished();
}

} // namespace vistella

#endif // vistella_ASYNC_H
