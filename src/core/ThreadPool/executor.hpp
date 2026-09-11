// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — Executor interface, task model, and pool configuration
// Provides: Role / Priority / Backpressure / Task / IExecutor / PoolConfig.
//
#pragma once

#include "config.hpp"
#include "cancellation.hpp"
#include "stats.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vistella::tp {

// ============================================================================
// Role — distinct pools for different latency/throughput requirements.
// ============================================================================
enum class Role : std::uint8_t {
    Interactive,
    Background,
    IO,
    Gpu,
    AI,
    Offline,
};

inline const char* role_name(Role r) noexcept {
    switch (r) {
        case Role::Interactive: return "interactive";
        case Role::Background:  return "background";
        case Role::IO:          return "io";
        case Role::Gpu:         return "gpu";
        case Role::AI:          return "ai";
        case Role::Offline:     return "offline";
    }
    return "unknown";
}

// ============================================================================
// Priority — three levels. Count = number of distinct levels.
// ============================================================================
enum class Priority : std::uint8_t { High = 0, Normal = 1, Low = 2, Count = 3 };

// ============================================================================
// TaskContext — read-only per-task execution context passed to Task::Fn.
// ============================================================================
struct TaskContext {
    CancellationToken& token;       ///< Caller-supplied cancellation token.
    std::uint32_t      worker = 0;  ///< Current worker index (0xFFFFFFFF if not on a pool).
    const char*        pool_name = "";  ///< Pool name (for diagnostics).
};

// ============================================================================
// Task — unit of work submitted to an IExecutor.
//   * fn          : the actual callable.
//   * name        : human-readable name (for stats / profiling).
//   * id          : monotonically increasing id within the pool.
//   * priority    : High / Normal / Low.
//   * gen_src/val : generation guard — if gen_src->current() != gen_val, the
//                   task is considered expired (e.g. UI redraw cycle changed).
//   * deadline    : absolute lifetime; expired tasks are dropped on dispatch.
//   * token       : optional cancellation token. Not owned.
//   * continuation: single-linked list of tasks that must run after this one
//                   (DAG: when_all / attach_then). Mutated under the parent
//                   task's own state — only the worker that finishes a parent
//                   touches its continuations.
//   * pending_deps: count of unfinished predecessors. When it drops to 0 the
//                   task becomes runnable. Decremented atomically by the
//                   worker finishing the predecessor.
//   * submit_time : captured by make_task() and updated by submit() to
//                   measure wait_us accurately.
// ============================================================================
struct Task {
    using Fn = std::function<void(TaskContext&)>;

    Fn fn;

    const char* name = "";
    std::uint64_t id = 0;

    Priority priority = Priority::Normal;

    const Generation*  gen_src = nullptr;
    Generation::Value  gen_val = 0;
    Deadline           deadline;
    CancellationToken* token = nullptr;  // not owned

    Task* continuation = nullptr;  // next DAG node (single-linked list head)
    std::atomic<int> pending_deps{0};

    std::chrono::steady_clock::time_point submit_time;

    bool expired() const noexcept {
        if (gen_src && gen_src->current() != gen_val) return true;
        if (deadline.expired_since(submit_time)) return true;
        return false;
    }

    bool is_cancelled() const noexcept {
        return token && token->cancelled();
    }
};

using TaskPtr = std::unique_ptr<Task>;

inline TaskPtr make_task(Task::Fn fn, const char* name = "") {
    auto t = std::make_unique<Task>();
    t->fn = std::move(fn);
    t->name = name ? name : "";
    t->submit_time = std::chrono::steady_clock::now();
    return t;
}

// ============================================================================
// IExecutor — abstract pool interface.
// ============================================================================
class IExecutor {
public:
    virtual ~IExecutor() = default;

    virtual bool submit(TaskPtr task) = 0;
    virtual void shutdown(bool cancel_pending = false) = 0;

    virtual void wait_for_counter(std::atomic<int>& counter, int target) {
        int spins = 0;
        while (counter.load(std::memory_order_acquire) > target) {
            if (++spins < 64) {
                for (int k = 0; k < 16; ++k) {
                    if (counter.load(std::memory_order_acquire) <= target) return;
                }
            } else if (spins < 256) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                spins = 128;
            }
        }
    }

    [[nodiscard]] virtual bool is_shutting_down() const noexcept = 0;
    [[nodiscard]] virtual PoolStats& stats() noexcept = 0;
    [[nodiscard]] virtual std::size_t thread_count() const noexcept = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    // DAG: attach `cont` as a successor of `parent`. The pool records the
    // dependency and only runs `cont` once `parent` finishes. Concatenates
    // onto parent's existing continuation list (O(1) prepend).
    virtual void attach_continuation(Task* parent, TaskPtr cont) = 0;

    // Helper: build + submit a Task from a callable. Default priority = Normal.
    bool submit_fn(Task::Fn fn, const char* name = "",
                   Priority p = Priority::Normal) {
        auto t = make_task(std::move(fn), name);
        t->priority = p;
        return submit(std::move(t));
    }

    // Helper: same as submit_fn but pins a Generation snapshot for expiry checks.
    bool submit_fn_generated(Task::Fn fn, const Generation::Snapshot& snap,
                             const char* name = "",
                             Priority p = Priority::Normal) {
        auto t = make_task(std::move(fn), name);
        t->priority = p;
        t->gen_val = snap.value();
        t->gen_src = Generation::Snapshot::owner_of(snap);
        return submit(std::move(t));
    }

    // Helper: same as submit_fn but with a wall-clock deadline.
    bool submit_fn_with_deadline(Task::Fn fn, Deadline dl,
                                 const char* name = "",
                                 Priority p = Priority::Normal) {
        auto t = make_task(std::move(fn), name);
        t->priority = p;
        t->deadline = dl;
        return submit(std::move(t));
    }
};

// ============================================================================
// PoolConfig — declarative description of a pool. Used by ThreadPool
// constructor and EngineContext factory.
// ============================================================================
struct PoolConfig {
    Role        role = Role::Interactive;
    std::string name = "pool";

    std::size_t thread_count = 0;            // 0 = auto (default_thread_count)
    int         os_priority  = 0;            // hint only; -2..+2 typical
    std::size_t queue_capacity = kDefaultQueueBound;
    Backpressure backpressure = Backpressure::Reject;
    bool        enable_stealing = true;      // allow work-stealing across workers

    std::vector<int> affinity;               // empty = no affinity hint

    std::size_t stack_size = 0;              // 0 = OS default
};

inline std::size_t default_thread_count(Role r) noexcept {
    const unsigned n = hardware_concurrency();
    switch (r) {
        case Role::Interactive: return n > 1 ? n - 1 : 1;  // leave 1 for UI/main
        case Role::Background:  return n;
        case Role::IO:          return n * 2;              // IO benefits from more threads
        case Role::Gpu:         return 1;                  // one task at a time
        case Role::AI:          return n > 2 ? 2 : 1;      // 2 ONNX inferences max in parallel
        case Role::Offline:     return n;
    }
    return n;
}

}  // namespace vistella::tp
