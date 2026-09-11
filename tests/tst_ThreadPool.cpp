// =============================================================
// tst_ThreadPool.cpp — ThreadPool v2 smoke tests
//
// Strategy: 5 focused cases, condition_variable-based sync (no busy-poll),
// uses only the public v2 API (submit_fn / CancellationToken / IExecutor&).
// =============================================================

#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QEventLoop>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../src/core/ThreadPool/cancellation.hpp"
#include "../src/core/ThreadPool/config.hpp"
#include "../src/core/ThreadPool/engine_context.h"
#include "../src/core/ThreadPool/executor.hpp"
#include "../src/core/ThreadPool/thread_pool.h"

using vistella::tp::CancellationToken;
using vistella::tp::EngineContext;
using vistella::tp::IExecutor;
using vistella::tp::PoolStats;
using vistella::tp::TaskContext;

namespace {

// RAII barrier: increments counter, then notifies waiting thread.
// Use to avoid 1ms-polling race conditions.
class NotifyGuard {
public:
    NotifyGuard(std::mutex& mu, std::condition_variable& cv,
                std::size_t& count, std::size_t target)
        : mu_(mu), cv_(cv), count_(count), target_(target) {}
    ~NotifyGuard() {
        std::lock_guard<std::mutex> lk(mu_);
        ++count_;
        cv_.notify_all();
    }
private:
    std::mutex& mu_;
    std::condition_variable& cv_;
    std::size_t& count_;
    std::size_t target_;
};

bool wait_for_count(std::mutex& mu, std::condition_variable& cv,
                    std::size_t& count, std::size_t target,
                    std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, timeout, [&] { return count >= target; });
}

}  // namespace

class tst_ThreadPool : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<EngineContext> engine_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::size_t done_ = 0;

private slots:
    void initTestCase() {
        // Use a small HW count so the 5 pools don't start 60+ threads on
        // a 12-core dev box. 2 threads per pool is enough to exercise
        // submit / cancel / stats semantics without saturating the system.
        engine_ = std::make_unique<EngineContext>(
            EngineContext::make_profile(EngineContext::Profile::General, 2));
    }

    void cleanupTestCase() {
        engine_.reset();
    }

    void init() {
        std::lock_guard<std::mutex> lk(mu_);
        done_ = 0;
    }

    // 1) EngineContext exposes a working pool for every mandatory role.
    void all_pools_have_at_least_one_worker() {
        QVERIFY(engine_->background().thread_count() >= 1);
        QVERIFY(engine_->io().thread_count()         >= 1);
        QVERIFY(engine_->ai().thread_count()         >= 1);
        QVERIFY(engine_->offline().thread_count()    >= 1);
    }

    // 2) submit_fn delivers a callable; result captured via shared atomic.
    void submit_fn_runs_callable() {
        std::atomic<int> result{0};
        engine_->background().submit_fn(
            [this, &result](TaskContext&) {
                result.store(42);
                std::lock_guard<std::mutex> lk(mu_);
                ++done_;
                cv_.notify_all();
            },
            "test_simple");
        QVERIFY(wait_for_count(mu_, cv_, done_, 1));
        QCOMPARE(result.load(), 42);
    }

    // 3) CancellationToken parent/child cascade.
    void cancel_token_cascades_to_children() {
        auto* parent = new CancellationToken();
        auto* child  = parent->create_child();
        QVERIFY(!parent->cancelled());
        QVERIFY(!child->cancelled());
        parent->cancel();
        QVERIFY(parent->cancelled());
        QVERIFY(child->cancelled());
        delete child;
        delete parent;
    }

    // 4) Pre-cancelled token -> task body is skipped.
    void pre_cancelled_token_skips_fn() {
        auto* token = new CancellationToken();
        token->cancel();
        std::atomic<bool> fnCalled{false};
        auto t = vistella::tp::make_task(
            [&fnCalled](TaskContext&) { fnCalled.store(true); },
            "test_precancelled");
        t->token = token;
        QVERIFY(engine_->background().submit(std::move(t)));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        QVERIFY(!fnCalled.load());
        delete token;
    }

    // 5) Many concurrent submissions; per-task identity preserved.
    void many_concurrent_submissions() {
        const int N = 50;
        std::vector<std::unique_ptr<std::atomic<int>>> results;
        for (int i = 0; i < N; ++i) {
            results.emplace_back(std::make_unique<std::atomic<int>>(-1));
        }
        // P0-2 (2026-09-08) bug fix: 原 NotifyGuard 模式在 loop 同步析构时就 ++ done_,
        //   wait_for_count(d >= N) 立刻返回 (race), tasks 还没跑完.
        //   改成 lambda 自己 ++ done_ (跟 submit_fn_runs_callable 一致), 这样
        //   wait_for_count 真正等所有 task 跑完才返回.
        for (int i = 0; i < N; ++i) {
            int expected_i = i;
            auto* slot = results[i].get();
            engine_->io().submit_fn(
                [this, expected_i, slot](TaskContext&) {
                    slot->store(expected_i);
                    NotifyGuard guard(mu_, cv_, done_, 1);
                    (void)guard;   // 析构时 ++ done_ + notify
                },
                "concurrent");
        }
        QVERIFY(wait_for_count(mu_, cv_, done_, N));
        for (int i = 0; i < N; ++i) {
            QCOMPARE(results[i]->load(), i);
        }
    }

    // 6) P0-2 (2026-09-08) integration smoke:
    //    EngineContext::ImageEditor profile + background().submit_fn +
    //    QMetaObject::invokeMethod 跨线程回调 — 跟 ImageAdjustmentPanel
    //    ::asyncApplyCurrentParams 用同样模式, 验证集成可用.
    //    不模拟完整 cancel-by-id race (那需要 QSignalSpy 完整时序控制,
    //    留给 P0-3+ 集成测试), 只验证 round-trip + task id 校验.
    void imageEditor_profile_async_roundtrip() {
        // ImageEditor profile 配 5 池 + 1 GPU serial
        auto imageEng = std::make_unique<EngineContext>(
            EngineContext::make_profile(EngineContext::Profile::ImageEditor, 2));
        QVERIFY(imageEng);
        QVERIFY(imageEng->background().thread_count() >= 1);
        QVERIFY(imageEng->interactive().thread_count() >= 1);

        // 模拟 asyncApplyCurrentParams: 后台跑 + 主线程回调
        std::atomic<uint64_t> taskId{0};
        const uint64_t myId = taskId.fetch_add(1) + 1;

        std::atomic<bool> mainThreadCallbackFired{false};
        std::atomic<int>  mainThreadValue{0};
        QObject* ctx = QCoreApplication::instance();
        QVERIFY(ctx != nullptr);

        imageEng->background().submit_fn(
            [ctx, myId, &taskId, &mainThreadCallbackFired, &mainThreadValue](TaskContext&) {
                // 模拟 OpenCV pipeline (短任务)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                int val = 42;
                // 跨线程 invokeMethod: 后台线程 -> 主线程
                QMetaObject::invokeMethod(ctx,
                    [myId, &taskId, &mainThreadCallbackFired, &mainThreadValue, val]() -> void {
                        if (taskId.load() != myId) return;   // 已被新 task 取代
                        mainThreadValue.store(val);
                        mainThreadCallbackFired.store(true);
                    },
                    Qt::QueuedConnection);
            },
            "test_async_roundtrip");

        // 等主线程回调
        QElapsedTimer t;
        t.start();
        while (!mainThreadCallbackFired.load() && t.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        QVERIFY(mainThreadCallbackFired.load());
        QCOMPARE(mainThreadValue.load(), 42);

        // 取消场景: 把 taskId 改成新值, 再跑 task, 回调应被丢弃
        const uint64_t newId = taskId.fetch_add(1) + 1;
        std::atomic<bool> droppedCallbackFired{false};
        imageEng->background().submit_fn(
            [ctx, myId, newId, &taskId, &droppedCallbackFired](TaskContext&) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                // 故意用 myId 闭包, 但 taskId 已经被改成 newId
                // → 回调里 if (taskId.load() != myId) return; 触发
                QMetaObject::invokeMethod(ctx,
                    [myId, &taskId, &droppedCallbackFired]() -> void {
                        if (taskId.load() != myId) return;  // 已是 newId, 不应 fire
                        droppedCallbackFired.store(true);
                    },
                    Qt::QueuedConnection);
            },
            "test_cancel_by_id");

        // 等回调跑完
        QElapsedTimer t2;
        t2.start();
        while (t2.elapsed() < 1000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // 老 task 的回调在 main thread 跑过, 但因 taskId != myId 提前 return
        // → droppedCallbackFired 保持 false
        QVERIFY(!droppedCallbackFired.load());

        imageEng.reset();
    }
};

QTEST_MAIN(tst_ThreadPool)
#include "tst_ThreadPool.moc"
