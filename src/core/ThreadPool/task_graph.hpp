// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — DAG helpers: attach_then, when_all, make_chain.
//
// Example:
//   auto a = make_task([]{ doA(); }, "A");
//   auto b = make_task([]{ doB(); }, "B");
//   attach_then(engine.interactive(), a.get(), std::move(b));
//   engine.interactive().submit(std::move(a));
//
//   auto c = make_task([]{ doC(); }, "C");
//   when_all(engine.interactive(), {a.get(), c.get()},
//            []{ doJoin(); }, "join");
//
//   auto chain = make_chain(engine.interactive(),
//                           []{doA();}, []{doB();}, []{doC();});
//   engine.interactive().submit(std::move(chain));
//
#pragma once

#include "cancellation.hpp"
#include "config.hpp"
#include "executor.hpp"
#include "thread_pool.h"  // for attach_continuation

#include <atomic>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace vistella::tp {

// ============================================================================
// attach_then — single-predecessor continuation
// ============================================================================
inline void attach_then(IExecutor& pool, Task* parent, TaskPtr cont) {
    if (!parent || !cont) return;
    pool.attach_continuation(parent, std::move(cont));
}

// ============================================================================
// when_all — multi-predecessor join. Runs `join_fn` once after every
// parent in `parents` has completed.
// ============================================================================
template <typename Pool, typename Fn>
inline void when_all(Pool& pool, const std::vector<Task*>& parents,
                     Fn&& join_fn, const char* name = "when_all") {
    if (parents.empty()) {
        // No predecessors: just run join_fn directly on the caller thread.
        CancellationToken null_token;
        TaskContext ctx{null_token, 0, pool.name()};
        join_fn(ctx);
        return;
    }

    struct JoinState {
        std::atomic<int>    counter;
        std::decay_t<Fn>    fn;
        const char*         name;
        IExecutor*          pool;
    };
    auto state = std::make_shared<JoinState>(JoinState{
        std::atomic<int>(static_cast<int>(parents.size())),
        std::forward<Fn>(join_fn),
        name,
        &pool});

    for (Task* p : parents) {
        // The trigger takes a shared_ptr to keep `state` alive until the
        // final trigger runs; it's safe to call attach_continuation even
        // if `p` is null (we just no-op).
        auto trigger = make_task(
            [state, &pool]() {
                if (state->counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto fn = std::move(state->fn);
                    const char* nm = state->name;
                    pool.submit_fn(
                        [fn = std::move(fn), nm]() {
                            CancellationToken null_token;
                            TaskContext ctx{null_token, 0, nm};
                            fn(ctx);
                        },
                        nm);
                }
            },
            "when_all_trigger");
        pool.attach_continuation(p, std::move(trigger));
    }
}

template <typename Pool, typename Fn>
inline void when_all(Pool& pool, std::initializer_list<Task*> parents,
                     Fn&& join_fn, const char* name = "when_all") {
    when_all(pool, std::vector<Task*>(parents),
             std::forward<Fn>(join_fn), name);
}

// ============================================================================
// make_chain — chain N functions in series, returning a single TaskPtr that
// must be submitted.
// ============================================================================
template <typename Pool, typename... Fns>
inline TaskPtr make_chain(Pool& pool, Fns&&... fns) {
    static_assert(sizeof...(fns) > 0, "make_chain needs at least one fn");
    std::vector<TaskPtr> tasks = { make_task(std::forward<Fns>(fns), "chain")... };
    for (std::size_t i = 1; i < tasks.size(); ++i) {
        pool.attach_continuation(tasks[i-1].get(), std::move(tasks[i]));
    }
    return std::move(tasks.front());
}

}  // namespace vistella::tp
