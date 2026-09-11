
//







//









#include "../config.hpp"

#if TP_USE_QT

#include "../cancellation.hpp"
#include "../executor.hpp"
#include "../stats.hpp"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

namespace multidoc::tp {

// ============================================================================



///




class QtBatchNotifier : public QObject {
    Q_OBJECT
public:
    explicit QtBatchNotifier(QObject* parent = nullptr) : QObject(parent) {}


    void notify() noexcept {
        pending_.fetchAndAddRelaxed(1);
        if (!scheduled_.testAndSetAcquire(0, 1)) return;
        QMetaObject::invokeMethod(this, [this]() { flush(); },
                                  Qt::QueuedConnection);
    }

    int pending() const noexcept { return pending_.loadAcquire(); }

signals:
    void progress(int count);

private:
    void flush() {
        scheduled_.storeRelease(0);
        const int n = pending_.fetchAndStoreRelaxed(0);
        if (n > 0) emit progress(n);
    }

    QAtomicInt pending_{0};
    QAtomicInt scheduled_{0};
};

// ============================================================================

// ============================================================================
class QtThreadGuard {
public:
    explicit QtThreadGuard(QThread* expected = nullptr)
        : expected_(expected ? expected : QCoreApplication::instance()->thread()) {}

    [[nodiscard]] bool on_expected_thread() const noexcept {
        return QThread::currentThread() == expected_;
    }
    void assert_thread() const noexcept { TP_ASSERT(on_expected_thread()); }

    void run_sync(std::function<void()> fn) const {
        if (on_expected_thread()) { fn(); return; }
        QMetaObject::invokeMethod(qApp, fn, Qt::BlockingQueuedConnection);
    }

private:
    QThread* expected_;
};

// ============================================================================

// ============================================================================
class QtPoolExecutor final : public IExecutor {
public:
    explicit QtPoolExecutor(PoolConfig cfg) : cfg_(std::move(cfg)) {
        name_ = cfg_.name.empty() ? role_name(cfg_.role) : cfg_.name;
        pool_.setMaxThreadCount(static_cast<int>(
            cfg_.thread_count ? cfg_.thread_count
                              : default_thread_count(cfg_.role)));
        if (cfg_.os_priority != 0) {
            pool_.setThreadPriority(cfg_.os_priority < 0 ? QThread::HighPriority
                                                         : QThread::LowPriority);
        }
    }

    ~QtPoolExecutor() override { shutdown(false); }

    bool submit(TaskPtr task) override {
        if (!task || !task->fn) return false;
        if (stopping_.load(std::memory_order_acquire)) return false;
        task->submit_time = std::chrono::steady_clock::now();
        task->id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
        stats_.submitted.fetch_add(1, std::memory_order_relaxed);

        std::shared_ptr<Task> shared(std::move(task));
        pool_.start(new LambdaRunnable([this, shared]() {
            run_task(std::move(const_cast<std::shared_ptr<Task>&>(shared)));
        }));
        return true;
    }

    void shutdown(bool cancel_pending = false) override {
        if (stopping_.exchange(true, std::memory_order_acq_rel)) return;
        if (cancel_pending) pool_.clear();
        pool_.waitForDone();
    }

    [[nodiscard]] bool is_shutting_down() const noexcept override {
        return stopping_.load(std::memory_order_acquire);
    }
    [[nodiscard]] PoolStats& stats() noexcept override { return stats_; }
    [[nodiscard]] std::size_t thread_count() const noexcept override {
        return static_cast<std::size_t>(pool_.maxThreadCount());
    }
    [[nodiscard]] const char* name() const noexcept override { return name_.c_str(); }

private:
    class LambdaRunnable : public QRunnable {
    public:
        explicit LambdaRunnable(std::function<void()> f) : fn_(std::move(f)) {
            setAutoDelete(true);
        }
        void run() override { if (fn_) fn_(); }
    private:
        std::function<void()> fn_;
    };

    void run_task(std::shared_ptr<Task> t) {
        if (t->expired() || t->is_cancelled()) {
            stats_.expired.fetch_add(1, std::memory_order_relaxed);
            if (t->is_cancelled())
                stats_.cancelled.fetch_add(1, std::memory_order_relaxed);
            stats_.completed.fetch_add(1, std::memory_order_relaxed);
            // continuation
            Task* cur = t->continuation;
            while (cur) {
                Task* next = cur->continuation;
                cur->continuation = nullptr;
                int prev = cur->pending_deps.fetch_sub(1, std::memory_order_acq_rel);
                if (prev == 1) {
                    auto* owned = cur;
                    submit(TaskPtr(owned));
                }
                cur = next;
            }
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        stats_.running.fetch_add(1, std::memory_order_relaxed);
        CancellationToken null_token;
        TaskContext ctx{t->token ? *t->token : null_token, 0, name_.c_str()};
        try { t->fn(ctx); } catch (...) {
            stats_.exceptions.fetch_add(1, std::memory_order_relaxed);
        }
        const auto end = std::chrono::steady_clock::now();
        stats_.exec_us.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(end - now).count(),
            std::memory_order_relaxed);
        stats_.running.fetch_sub(1, std::memory_order_relaxed);
        stats_.completed.fetch_add(1, std::memory_order_relaxed);

        // continuation
        Task* cur = t->continuation;
        while (cur) {
            Task* next = cur->continuation;
            cur->continuation = nullptr;
            int prev = cur->pending_deps.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1) {
                auto* owned = cur;
                submit(TaskPtr(owned));
            }
            cur = next;
        }
    }

    PoolConfig cfg_;
    std::string name_;
    QThreadPool pool_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> next_task_id_{0};
    PoolStats stats_;
    CancellationToken null_token_;
};

}  // namespace multidoc::tp

#endif  // TP_USE_QT
