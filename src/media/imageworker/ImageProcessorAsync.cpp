// =============================================================
// ImageProcessorAsync 实现
// 阶段 1 W2.3 (2026-09-03) — 详见 ImageProcessorAsync.h
// P0-2.5 (2026-09-08): 6 个 EngineContext overload (走 background pool)
// =============================================================

#include "ImageProcessorAsync.h"
#include "../imageprocessor.h"
#include "../../core/Async.h"
// P0-2.5 (2026-09-08): EngineContext 完整 include
#include "../../core/ThreadPool/engine_context.h"
#include "../../core/ThreadPool/executor.hpp"

#include <opencv2/imgproc.hpp>

#include <memory>

using vistella::Result;

// 通用 helper: 调 fn(), 包成 Result<cv::Mat> 异步返
template <typename Fn>
QFuture<Result<cv::Mat>> ImageProcessorAsync::runOp(Fn &&fn)
{
    return vistella::run<vistella::Result<cv::Mat>>(
        [fn = std::forward<Fn>(fn)]() -> vistella::Result<cv::Mat> {
            try {
                cv::Mat out;
                fn(out);
                if (out.empty()) {
                    return Result<cv::Mat>::err(
                        vistella::ErrorCode::InvalidArgument,
                        std::string("ImageProcessor operation produced empty Mat"));
                }
                return out;
            } catch (const cv::Exception &e) {
                return Result<cv::Mat>::err(
                    vistella::ErrorCode::Internal,
                    std::string("OpenCV exception: ") + e.what());
            } catch (const std::exception &e) {
                return Result<cv::Mat>::err(
                    vistella::ErrorCode::Internal,
                    std::string("std::exception: ") + e.what());
            }
        });
}

// P0-2.5 (2026-09-08): EngineContext 版 helper
//   走 background().submit_fn, 返 std::future<Result<cv::Mat>>
//   跟 runOp 同样包装: cv::Mat 空 → err(InvalidArgument)
//                     cv::Exception / std::exception → err(Internal)
//
//   实现要点:
//     1. promise 包 shared_ptr — submit_fn 的 std::function 按值捕获, promise 不可拷贝
//     2. 闭包捕获 shared_ptr<promise>, future 拿自 promise->get_future()
//     3. engine == nullptr 兜底: 走旧 facade (QFuture 版) 然后转 std::future
//        旧版用 QFutureWatcher 也能桥, 但 std::promise 更直接
//        P0-2.5 简化: engine == nullptr 时调旧的 QFuture 版同步等 (没 pool 时直接等
//        也合理), 然后用 std::promise 包 Result 返
//     4. submit_fn 失败 (池满 / shutting down) → 同步跑 + promise.set_value
template <typename Fn>
std::future<Result<cv::Mat>> ImageProcessorAsync::runOpEngine(
    vistella::tp::EngineContext *engine, Fn &&fn)
{
    auto promisePtr = std::make_shared<std::promise<Result<cv::Mat>>>();
    auto futureRet  = promisePtr->get_future();

    if (!engine) {
        // 兜底: 没 engine → 同步跑 (返 future 立即就绪)
        try {
            cv::Mat out;
            fn(out);
            if (out.empty()) {
                promisePtr->set_value(Result<cv::Mat>::err(
                    vistella::ErrorCode::InvalidArgument,
                    std::string("ImageProcessor operation produced empty Mat")));
            } else {
                promisePtr->set_value(Result<cv::Mat>(std::move(out)));
            }
        } catch (const cv::Exception &e) {
            promisePtr->set_value(Result<cv::Mat>::err(
                vistella::ErrorCode::Internal,
                std::string("OpenCV exception: ") + e.what()));
        } catch (const std::exception &e) {
            promisePtr->set_value(Result<cv::Mat>::err(
                vistella::ErrorCode::Internal,
                std::string("std::exception: ") + e.what()));
        }
        return futureRet;
    }

    // 正常路径: 走 background pool
    const bool submitted = engine->background().submit_fn(
        [promisePtr, fn = std::forward<Fn>(fn)](vistella::tp::TaskContext & /*taskCtx*/) -> void {
            try {
                cv::Mat out;
                fn(out);
                if (out.empty()) {
                    promisePtr->set_value(Result<cv::Mat>::err(
                        vistella::ErrorCode::InvalidArgument,
                        std::string("ImageProcessor operation produced empty Mat")));
                } else {
                    promisePtr->set_value(Result<cv::Mat>(std::move(out)));
                }
            } catch (const cv::Exception &e) {
                promisePtr->set_value(Result<cv::Mat>::err(
                    vistella::ErrorCode::Internal,
                    std::string("OpenCV exception: ") + e.what()));
            } catch (const std::exception &e) {
                promisePtr->set_value(Result<cv::Mat>::err(
                    vistella::ErrorCode::Internal,
                    std::string("std::exception: ") + e.what()));
            }
        },
        "ImageProcessorAsync::engine",
        vistella::tp::Priority::Normal);

    if (!submitted) {
        // 提交失败 (池满 / shutting down) → 同步跑兜底
        try {
            cv::Mat out;
            fn(out);
            if (out.empty()) {
                promisePtr->set_value(Result<cv::Mat>::err(
                    vistella::ErrorCode::InvalidArgument,
                    std::string("ImageProcessor operation produced empty Mat (fallback)")));
            } else {
                promisePtr->set_value(Result<cv::Mat>(std::move(out)));
            }
        } catch (const cv::Exception &e) {
            promisePtr->set_value(Result<cv::Mat>::err(
                vistella::ErrorCode::Internal,
                std::string("OpenCV exception: ") + e.what()));
        } catch (const std::exception &e) {
            promisePtr->set_value(Result<cv::Mat>::err(
                vistella::ErrorCode::Internal,
                std::string("std::exception: ") + e.what()));
        }
    }
    return futureRet;
}

// ---- 具体操作 ----

QFuture<Result<cv::Mat>> ImageProcessorAsync::gaussianBlur(cv::Mat img, int ksize, double sigma)
{
    return runOp([img = std::move(img), ksize, sigma](cv::Mat &out) {
        ImageProcessor::gaussianBlur(img, out, ksize, sigma);
    });
}

QFuture<Result<cv::Mat>> ImageProcessorAsync::saturation(cv::Mat img, double scale)
{
    return runOp([img = std::move(img), scale](cv::Mat &out) {
        ImageProcessor::saturation(img, out, scale);
    });
}

QFuture<Result<cv::Mat>> ImageProcessorAsync::hueShift(cv::Mat img, double degrees)
{
    return runOp([img = std::move(img), degrees](cv::Mat &out) {
        ImageProcessor::hueShift(img, out, degrees);
    });
}

QFuture<Result<cv::Mat>> ImageProcessorAsync::exposure(cv::Mat img, double ev)
{
    return runOp([img = std::move(img), ev](cv::Mat &out) {
        ImageProcessor::exposure(img, out, ev);
    });
}

QFuture<Result<cv::Mat>> ImageProcessorAsync::sharpen(cv::Mat img)
{
    return runOp([img = std::move(img)](cv::Mat &out) {
        ImageProcessor::sharpen(img, out);
    });
}

QFuture<Result<cv::Mat>> ImageProcessorAsync::edgeDetect(cv::Mat img, double low, double high)
{
    return runOp([img = std::move(img), low, high](cv::Mat &out) {
        ImageProcessor::edgeDetect(img, out, low, high);
    });
}

// =====================================================================
// P0-2.5 (2026-09-08): 6 个 EngineContext overload
//   走 background pool, 返 std::future<Result<cv::Mat>>
//   跟 P0-2 ImageAdjustmentPanel::asyncApplyCurrentParams 用同样的
//   submit_fn + Task::Fn 模式, facade 跟它保持调用风格一致
// =====================================================================

std::future<Result<cv::Mat>> ImageProcessorAsync::gaussianBlur(
    vistella::tp::EngineContext *engine, cv::Mat img, int ksize, double sigma)
{
    return runOpEngine(engine,
        [img = std::move(img), ksize, sigma](cv::Mat &out) {
            ImageProcessor::gaussianBlur(img, out, ksize, sigma);
        });
}

std::future<Result<cv::Mat>> ImageProcessorAsync::saturation(
    vistella::tp::EngineContext *engine, cv::Mat img, double scale)
{
    return runOpEngine(engine,
        [img = std::move(img), scale](cv::Mat &out) {
            ImageProcessor::saturation(img, out, scale);
        });
}

std::future<Result<cv::Mat>> ImageProcessorAsync::hueShift(
    vistella::tp::EngineContext *engine, cv::Mat img, double degrees)
{
    return runOpEngine(engine,
        [img = std::move(img), degrees](cv::Mat &out) {
            ImageProcessor::hueShift(img, out, degrees);
        });
}

std::future<Result<cv::Mat>> ImageProcessorAsync::exposure(
    vistella::tp::EngineContext *engine, cv::Mat img, double ev)
{
    return runOpEngine(engine,
        [img = std::move(img), ev](cv::Mat &out) {
            ImageProcessor::exposure(img, out, ev);
        });
}

std::future<Result<cv::Mat>> ImageProcessorAsync::sharpen(
    vistella::tp::EngineContext *engine, cv::Mat img)
{
    return runOpEngine(engine,
        [img = std::move(img)](cv::Mat &out) {
            ImageProcessor::sharpen(img, out);
        });
}

std::future<Result<cv::Mat>> ImageProcessorAsync::edgeDetect(
    vistella::tp::EngineContext *engine, cv::Mat img, double low, double high)
{
    return runOpEngine(engine,
        [img = std::move(img), low, high](cv::Mat &out) {
            ImageProcessor::edgeDetect(img, out, low, high);
        });
}
