// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — single-threaded executor with FIFO ordering.
// Used for GPU work (CUDA contexts aren't thread-safe) and other cases
// where the work must be strictly serialized.
//
#include "serial_executor.h"

#include <chrono>
#include <utility>

namespace vistella::tp {

SerialExecutor::SerialExecutor(Config cfg) : cfg_(std::move(cfg)) {
    name_ = cfg_.name;
    thread_ = std::thread([this] { run_loop(); });
}

SerialExecutor::~SerialExecutor() { shutdown(true); }

bool SerialExecutor::submit(TaskPtr task) {
    if (!task || !task->fn) return false;
    if (stopping_.load(std::memory_order_acquire)) return false;
    task->submit_time = std::chrono::steady_clock::now();
    task->id = next_task_id_.fetch_add(1, std::memory_order_relaxed);

    std::unique_lock<std::mutex> lk(mu_);
    const std::size_t bound = cfg_.queue_capacity;
    if (queue_.size() >= bound) {
        switch (cfg_.backpressure) {
            case Backpressure::Block:
                space_cv_.wait(lk, [&] {
                    return stopping_.load(std::memory_order_acquire) ||
                           queue_.size() < bound;
                });
                if (stopping_.load(std::memory_order_acquire)) return false;
                break;
            case Backpressure::DropOldest:
                queue_.pop_front();
                stats_.expired.fetch_add(1, std::memory_order_relaxed);
                break;
            default:
                stats_.rejected.fetch_add(1, std::memory_order_relaxed);
                return false;
        }
    }
    queue_.push_back(task.release());
    stats_.submitted.fetch_add(1, std::memory_order_relaxed);
    stats_.queued.store(queue_.size(), std::memory_order_relaxed);
    lk.unlock();
    cv_.notify_one();
    return true;
}

void SerialExecutor::shutdown(bool cancel_pending) {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        std::unique_lock<std::mutex> lk(mu_);
        done_cv_.wait(lk, [this] { return joined_.load(std::memory_order_acquire); });
        return;
    }
    if (cancel_pending) {
        std::lock_guard<std::mutex> lk(mu_);
        for (Task* raw : queue_) delete raw;
        queue_.clear();
    }
    cv_.notify_all();
    {
        std::unique_lock<std::mutex> lk(mu_);
        done_cv_.wait(lk, [this] { return joined_.load(std::memory_order_acquire); });
    }
    if (thread_.joinable()) thread_.join();
}

bool SerialExecutor::is_shutting_down() const noexcept {
    return stopping_.load(std::memory_order_acquire);
}

void SerialExecutor::attach_continuation(Task* parent, TaskPtr cont) {
    if (!parent || !cont) return;
    cont->pending_deps.fetch_add(1, std::memory_order_acq_rel);
    // Prepend cont to parent's continuation list (single-writer normally,
    // but use CAS-loop in case attach_continuation is called concurrently).
    Task* old = parent->continuation;
    do {
        cont->continuation = old;
    } while (!std::atomic_compare_exchange_strong(
        reinterpret_cast<std::atomic<Task*>*>(&parent->continuation),
        &old, cont.release()));
}

void SerialExecutor::run_sync(std::function<void()> fn) {
    if (is_serial_thread()) {
        fn();
        return;
    }
    std::atomic<bool> done{false};
    auto t = make_task([&fn, &done](TaskContext&) {
        fn();
        done.store(true, std::memory_order_release);
    }, "run_sync");
    submit(std::move(t));
    while (!done.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void SerialExecutor::run_loop() {
    // Optional CPU affinity. -1 means "no preference".
    if (cfg_.affinity_cpu >= 0) {
#if TP_PLATFORM_LINUX
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cfg_.affinity_cpu, &set);
        ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
#elif TP_PLATFORM_MACOS
        // macOS has thread_policy_set with THREAD_AFFINITY_POLICY but the
        // API is awkward; we skip it on Mac by design.
        (void)cfg_.affinity_cpu;
#endif
    }

    // Best-effort thread name (truncated to 15 chars on Linux).
    {
        std::string name = "tp/";
        name += cfg_.name;
        if (name.size() >= kMaxThreadName) name.resize(kMaxThreadName);
#if TP_PLATFORM_LINUX
        ::pthread_setname_np(::pthread_self(), name.c_str());
#elif TP_PLATFORM_MACOS
        ::pthread_setname_np(name.c_str());
#else
        (void)name;
#endif
    }

    if (cfg_.on_thread_start) {
        try { cfg_.on_thread_start(); } catch (...) {
            std::lock_guard<std::mutex> lk(mu_);
            last_exception_ = std::current_exception();
            stats_.exceptions.fetch_add(1, std::memory_order_relaxed);
        }
    }

    while (true) {
        Task* t = nullptr;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return stopping_.load(std::memory_order_acquire) || !queue_.empty();
            });
            if (stopping_.load(std::memory_order_acquire) && queue_.empty()) break;
            t = queue_.front();
            queue_.pop_front();
            stats_.queued.store(queue_.size(), std::memory_order_relaxed);
        }
        if (cfg_.backpressure == Backpressure::Block) space_cv_.notify_one();
        if (!t) continue;

        if (t->expired()) {
            stats_.expired.fetch_add(1, std::memory_order_relaxed);
            stats_.completed.fetch_add(1, std::memory_order_relaxed);
            delete t;
            continue;
        }
        if (t->is_cancelled()) {
            stats_.cancelled.fetch_add(1, std::memory_order_relaxed);
            stats_.completed.fetch_add(1, std::memory_order_relaxed);
            delete t;
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        stats_.running.fetch_add(1, std::memory_order_relaxed);
        stats_.wait_us.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now - t->submit_time).count(), std::memory_order_relaxed);
        CancellationToken null_token;
        TaskContext ctx{t->token ? *t->token : null_token, 0, name_.c_str()};
        try {
            t->fn(ctx);
        } catch (...) {
            std::lock_guard<std::mutex> lk(mu_);
            last_exception_ = std::current_exception();
            stats_.exceptions.fetch_add(1, std::memory_order_relaxed);
        }
        const auto end = std::chrono::steady_clock::now();
        stats_.exec_us.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(end - now).count(),
            std::memory_order_relaxed);
        stats_.running.fetch_sub(1, std::memory_order_relaxed);
        stats_.completed.fetch_add(1, std::memory_order_relaxed);

        // Schedule DAG continuations (P0 #5).
        Task* cur = t->continuation;
        delete t;
        while (cur) {
            Task* next = cur->continuation;
            cur->continuation = nullptr;
            int prev = cur->pending_deps.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1) {
                std::unique_lock<std::mutex> lk(mu_);
                queue_.push_back(cur);
                cv_.notify_one();
            } else {
                // pending_deps > 1: another predecessor will pick it up.
            }
            cur = next;
        }
    }

    if (cfg_.on_thread_stop) {
        try { cfg_.on_thread_stop(); } catch (...) {}
    }

    joined_.store(true, std::memory_order_release);
    done_cv_.notify_all();
}

}  // namespace vistella::tp
