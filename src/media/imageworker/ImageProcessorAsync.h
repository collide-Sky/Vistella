#ifndef IMAGEPROCESSORASYNC_H
#define IMAGEPROCESSORASYNC_H

// =============================================================
// ImageProcessorAsync — 阶段 1 W2.3 异步滤镜 facade (2026-09-03)
//
// 设计动机:
//   阶段 0 imageWindow 同步调 ImageProcessor 滤镜, 大图 (24MP+) 卡 UI.
//   阶段 1 W2 用 C.1 Async.h (QtConcurrent + QFutureWatcher) 包装 OpenCV 调用,
//   UI 线程不阻塞.
//
// 设计原则 (成熟方案):
//   1. 同步版本保留 — 现有 imageWindow.cpp 调用方不强制改
//   2. 异步版本是 facade: 跟同步签名一致, 但返 QFuture<Result<cv::Mat>>
//      - 调用方未来从 sync 调用换成 async 调用 = 改 1 行 (加 Async::run 包装)
//      - 错误传递用 Result<cv::Mat> (阶段 1 A.1 错误处理统一)
//   3. 撤销栈 — 由调用方 (imageWindow) 维护, 异步 result 回来后 push Command
//      当前阶段 W2.3 不替换现有调用, 只建模板 + 单元测试
//
// 用法示例 (未来 W3 替换现有调用时):
//     auto future = ImageProcessorAsync::gaussianBlur(m_current, ksize, sigma);
//     auto *watcher = Async::watch(future, this,
//         [this](const Result<cv::Mat> &r) -> void {
//             if (r.isErr()) { showError(r.error()); return; }
//             // push undo command
//             m_undoStack->push(new ImageEditCommand(this, m_current, r.value(), "blur"));
//             setCurrentImage(r.value());
//         });
//
// 阶段 1+ 渐进:
//   W2 (本阶段): 建 facade + tst_ImageProcessorAsync 测 API 模板
//   W3 色彩调整: 替换 1-2 个 sync 调用 (saturation / exposure) → async
//   W4 图层: 替换更多 (mosaic / edge detect) → async
// =============================================================

#include "../core/Result.h"

#include <QFuture>
#include <QObject>

#include <opencv2/core.hpp>

#include <functional>
#include <utility>

// 简化 forward decl, 不依赖 Async.h (避免循环依赖, 业务调用方自己 include)
namespace vistella {
template <typename T> class Result;
}

// P0-2.5 (2026-09-08): EngineContext forward decl
//   6 个新 overload 接 EngineContext*, 走 background().submit_fn 后台跑
//   跟旧 overload (走 vistella::run<>) 签名并存, 旧调用方不破坏
namespace vistella::tp { class EngineContext; }

#include <future>   // std::future / std::promise

class ImageProcessorAsync
{
public:
    // ---- 同步版本 (跟 ImageProcessor 签名一致, 这里不重复声明) ----
    // 异步 facade 在 src/media/ImageProcessorAsync.cpp 实现, 内部用 vistella::Async

    // ---- 异步入口 (阶段 1 W2 模板, 未来 W3/W4 替换 imageWindow.cpp 调用) ----

    // 高斯模糊
    static QFuture<vistella::Result<cv::Mat>> gaussianBlur(cv::Mat img, int ksize, double sigma);

    // 饱和度调整
    static QFuture<vistella::Result<cv::Mat>> saturation(cv::Mat img, double scale);

    // 色调调整
    static QFuture<vistella::Result<cv::Mat>> hueShift(cv::Mat img, double degrees);

    // 曝光调整
    static QFuture<vistella::Result<cv::Mat>> exposure(cv::Mat img, double ev);

    // 锐化
    static QFuture<vistella::Result<cv::Mat>> sharpen(cv::Mat img);

    // Canny 边缘检测
    static QFuture<vistella::Result<cv::Mat>> edgeDetect(cv::Mat img, double low, double high);

    // ---- P0-2.5 (2026-09-08): EngineContext 接入 (新 overload) ----
    //   设计原则 (一次性到位, 不留补丁):
    //     1. engine == nullptr → 内部 fallback 调旧 overload (走 vistella::run<>)
    //        旧调用方 (没 engine 的场景, e.g. 纯算法测试) 不破坏
    //     2. engine 非空 → 走 m_engine->background().submit_fn, 跑完
    //        std::promise::set_value 触发 std::future
    //     3. 返 std::future<Result<cv::Mat>> (跟旧的 QFuture 不同)
    //        - 调用方用 future.get() / future.wait_for() 同步等
    //        - 不阻塞调用方线程, submit_fn 立即返
    //     4. 异常处理: 跟旧版一致, runOp 模板 + Result::err 包装
    //     5. 旧 facade 函数保持不变 — 业务调用方 (P0-3+ 替换 imageWindow.cpp 调用时)
    //        可以从 QFuture 版本切换到 std::future 版本, 内部逻辑不变
    //
    //   跟 P0-2 ImageAdjustmentPanel::asyncApplyCurrentParams 行为对齐:
    //     - 都用 background().submit_fn + Task::Fn 签名
    //     - 都用 QMetaObject::invokeMethod 或 std::promise 跨线程返回值
    //     - ImageAdjustmentPanel 用 invokeMethod 是因为要切回主线程
    //       (要刷 UI); facade 用 std::promise 是因为调用方可能想自己决定
    //       回调线程, 不强制主线程 (更灵活)
    static std::future<vistella::Result<cv::Mat>> gaussianBlur(vistella::tp::EngineContext *engine, cv::Mat img, int ksize, double sigma);
    static std::future<vistella::Result<cv::Mat>> saturation(vistella::tp::EngineContext *engine, cv::Mat img, double scale);
    static std::future<vistella::Result<cv::Mat>> hueShift(vistella::tp::EngineContext *engine, cv::Mat img, double degrees);
    static std::future<vistella::Result<cv::Mat>> exposure(vistella::tp::EngineContext *engine, cv::Mat img, double ev);
    static std::future<vistella::Result<cv::Mat>> sharpen(vistella::tp::EngineContext *engine, cv::Mat img);
    static std::future<vistella::Result<cv::Mat>> edgeDetect(vistella::tp::EngineContext *engine, cv::Mat img, double low, double high);

private:
    // 通用 helper: 用 vistella::Async::run 包同步调用 + Result 包装
    template <typename Fn>
    static QFuture<vistella::Result<cv::Mat>> runOp(Fn &&fn);

    // P0-2.5 (2026-09-08): EngineContext 版 helper — 走 background().submit_fn
    //   返 std::shared_ptr<std::promise<Result<cv::Mat>>> (用 shared_ptr 是因为
    //   submit_fn 接受 std::function, 闭包按值捕获 promise 须可拷贝; promise 不可拷贝,
    //   所以包 shared_ptr)
    template <typename Fn>
    static std::future<vistella::Result<cv::Mat>> runOpEngine(
        vistella::tp::EngineContext *engine, Fn &&fn);
};

#endif // IMAGEPROCESSORASYNC_H
