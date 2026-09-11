// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — MPMC work-stealing thread pool.
// Implements IExecutor. Uses ChaseLevDeque per worker + a shared injected
// queue for cross-pool submissions and overflow.
//
#pragma once

#include "config.hpp"
#include "cancellation.hpp"
#include "executor.hpp"
#include "stats.hpp"
#include "tls.hpp"
#include "work_stealing_queue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if TP_PLATFORM_LINUX
#  include <pthread.h>
#  include <sched.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#elif TP_PLATFORM_MACOS
#  include <pthread.h>
#endif

namespace vistella::tp {

class ThreadPool;

class ThreadPool final : public IExecutor {
public:
    explicit ThreadPool(PoolConfig cfg);
    ~ThreadPool() override;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // IExecutor
    bool submit(TaskPtr task) override;
    void shutdown(bool cancel_pending = false) override;
    [[nodiscard]] bool is_shutting_down() const noexcept override;
    [[nodiscard]] PoolStats& stats() noexcept override { return stats_; }
    [[nodiscard]] std::size_t thread_count() const noexcept override { return workers_.size(); }
    [[nodiscard]] const char* name() const noexcept override { return name_.c_str(); }
    void wait_for_counter(std::atomic<int>& counter, int target) override {
        wait_while_working(counter, target);
    }
    void attach_continuation(Task* parent, TaskPtr cont) override;

    // Convenience helpers used by submit/attach_continuation.
    bool try_execute_one();
    void wait_while_working(std::atomic<int>& counter, int target);

    [[nodiscard]] std::size_t current_worker_index() const noexcept;
    [[nodiscard]] bool is_worker_thread() const noexcept {
        return current_worker_index() != static_cast<std::size_t>(-1);
    }

private:
    using Deque = ChaseLevDeque<Task*, kLocalDequeCapacity>;

    void worker_loop(std::size_t idx);
    void run_task(Task* t);
    void schedule_continuations(Task* finished);
    void submit_continuation_task(Task* t);

    bool push_injected(TaskPtr task, bool from_worker);
    Task* pop_injected();
    Task* pop_local_or_steal();
    void update_peak(std::size_t cur) noexcept;

    void wake_one() noexcept;
    void cpu_relax() noexcept;
    std::size_t rng_next() noexcept;
    void set_thread_name(const char* base, std::size_t idx) noexcept;

    PoolConfig cfg_;
    std::string name_;

    std::size_t n_threads_ = 0;
    std::vector<std::thread> workers_;
    std::unique_ptr<Deque[]> deques_;

    // Per-worker index slot, indexed by worker id. Workers register their
    // slot via tls_worker_index_ at start; this indirection lets the same
    // TLS pointer work across pools (a worker may be reassigned).
    std::array<std::size_t, kMaxWorkers> worker_idx_storage_{};

    std::mutex start_mu_;
    std::condition_variable start_cv_;
    std::atomic<int> started_count_{0};
    std::atomic<bool> ready_{false};

    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> join_done_{false};
    std::condition_variable done_cv_;

    // Injected queue: shared inbox for non-worker callers and for tasks that
    // don't fit in a worker's local deque. Three sub-queues by priority.
    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable queued_cv_;
    std::array<std::deque<Task*>, 3> injected_;

    std::atomic<std::uint64_t> next_task_id_{0};

    // Cheap per-pool PRNG state — used only for victim selection in
    // work-stealing, no cryptographic requirement.
    std::atomic<std::size_t> rng_state_{0};

    PoolStats stats_;

    std::exception_ptr last_exception_;
};

}  // namespace vistella::tp
