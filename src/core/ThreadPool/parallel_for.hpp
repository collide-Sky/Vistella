// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — parallel_for / parallel_reduce helpers.
// Work-splitting across N chunks, each chunk submitted as a Task. Cursors
// are atomic so chunks self-claim work (good for variable cost bodies).
//
#pragma once

#include "cancellation.hpp"
#include "config.hpp"
#include "executor.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace vistella::tp {

// ============================================================================
// ParallelOptions
// ============================================================================
struct ParallelOptions {
    std::size_t min_chunk = 1;             // smallest work unit per task
    std::size_t max_tasks = 0;             // 0 = thread_hint * 4

    const Generation* gen = nullptr;       // optional generation guard
    Generation::Value gen_val = 0;

    Priority priority = Priority::Normal;
    CancellationToken* token = nullptr;    // optional cancellation
    bool check_expiry = true;              // honour gen + token per chunk

    ParallelOptions& with_generation(const Generation::Snapshot& s) {
        gen = Generation::Snapshot::owner_of(s);
        gen_val = s.value();
        return *this;
    }
    ParallelOptions& with_token(CancellationToken* t) {
        token = t;
        return *this;
    }
    ParallelOptions& with_chunk(std::size_t n) { min_chunk = n; return *this; }
    ParallelOptions& with_priority(Priority p) { priority = p; return *this; }
};

namespace detail {

template <class Index, class Body>
class ParallelForRunner {
public:
    ParallelForRunner(IExecutor& pool, Index begin, Index end, Body body,
                      ParallelOptions opt, std::size_t thread_hint)
        : pool_(pool), begin_(begin), end_(end), body_(std::move(body)),
          opt_(std::move(opt)), thread_hint_(thread_hint ? thread_hint : 1),
          cursor_(0),
          chunk_size_(compute_chunk(end - begin, opt.min_chunk, thread_hint)),
          n_chunks_((end - begin + chunk_size_ - 1) / chunk_size_) {
        remaining_.store(0, std::memory_order_relaxed);
    }

    void run() {
        const Index total = end_ - begin_;
        if (total <= 0) return;

        const std::size_t n_tasks =
            opt_.max_tasks
                ? std::min<std::size_t>(opt_.max_tasks, n_chunks_)
                : std::min<std::size_t>(n_chunks_, thread_hint_ * 4);

        if (n_tasks <= 1) {
            // Single task path: don't bother with the counter dance.
            execute_range();
            return;
        }

        remaining_.store(static_cast<int>(n_tasks), std::memory_order_relaxed);
        failed_.store(0, std::memory_order_relaxed);

        // Submit n_tasks-1 workers; the caller runs the last one directly so
        // the function is "synchronous" (returns when all chunks finish).
        for (std::size_t i = 1; i < n_tasks; ++i) {
            auto t = make_task([this](TaskContext&) {
                execute_range();
                remaining_.fetch_sub(1, std::memory_order_release);
            }, "parallel_for_chunk");
            t->priority = opt_.priority;
            t->token = opt_.token;
            t->submit_time = std::chrono::steady_clock::now();

            if (!pool_.submit(std::move(t))) {
                // Submission refused (backpressure / shutdown): run inline
                // and decrement remaining_ to keep the wait correct.
                execute_range();
                remaining_.fetch_sub(1, std::memory_order_release);
            }
        }

        // Caller's own chunk: run synchronously, then wait for the others.
        remaining_.fetch_sub(1, std::memory_order_release);
        pool_.wait_for_counter(remaining_, 0);

        if (failed_.load(std::memory_order_acquire) > 0) {
            std::lock_guard<std::mutex> lk(mu_);
            if (last_exception_) std::rethrow_exception(last_exception_);
        }
    }

private:
    static std::size_t compute_chunk(Index total, std::size_t min_chunk,
                                     std::size_t thread_hint) noexcept {
        if (total <= 0) return 1;
        const std::size_t hint = thread_hint ? thread_hint : 1;
        std::size_t cs = static_cast<std::size_t>(total) / (hint * 4);
        if (cs < min_chunk) cs = min_chunk;
        if (cs < 1) cs = 1;
        return cs;
    }

    void execute_range() {
        const Index total = end_ - begin_;
        const std::size_t cs = chunk_size_;
        while (true) {
            // Atomically claim the next chunk by index. Higher contention
            // than a static split, but tolerates variable-cost bodies.
            const Index start_idx =
                cursor_.fetch_add(static_cast<Index>(1), std::memory_order_acq_rel);
            if (start_idx >= static_cast<Index>(n_chunks_)) return;
            const Index lo = begin_ + start_idx * static_cast<Index>(cs);
            const Index hi = std::min<Index>(lo + static_cast<Index>(cs), end_);

            if (opt_.check_expiry) {
                if (opt_.gen && opt_.gen->current() != opt_.gen_val) return;
                if (opt_.token && opt_.token->cancelled()) return;
            }
            try {
                body_(lo, hi);
            } catch (...) {
                failed_.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(mu_);
                last_exception_ = std::current_exception();
            }
        }
    }

    IExecutor& pool_;
    Index begin_, end_;
    Body body_;
    ParallelOptions opt_;
    std::size_t thread_hint_;
    std::atomic<Index> cursor_;
    std::size_t chunk_size_;
    std::size_t n_chunks_;
    std::atomic<int> remaining_;
    std::atomic<int> failed_;

    std::mutex mu_;
    std::exception_ptr last_exception_;
};

}  // namespace detail

// ============================================================================
// parallel_for — synchronous (blocks until all chunks finish).
// ============================================================================
template <class Index, class Body>
inline void parallel_for(IExecutor& pool, Index begin, Index end, Body&& body,
                          ParallelOptions opt = {},
                          std::size_t thread_hint = 0) {
    detail::ParallelForRunner<Index, std::decay_t<Body>> runner(
        pool, begin, end, std::forward<Body>(body),
        std::move(opt), thread_hint);
    runner.run();
}

// ============================================================================
// parallel_reduce — map + reduce, with one slot per chunk.
// ============================================================================
template <class Index, class T, class Map, class Reduce>
inline T parallel_reduce(IExecutor& pool, Index begin, Index end, T init,
                         Map&& map, Reduce&& reduce,
                         ParallelOptions opt = {},
                         std::size_t thread_hint = 0) {
    std::vector<T> partial_results;
    std::atomic<std::size_t> chunk_idx{0};
    const std::size_t hint = thread_hint ? thread_hint : 4;
    const std::size_t n_chunks = std::max<std::size_t>(1, hint * 4);
    const std::size_t chunk_size =
        (static_cast<std::size_t>(end - begin) + n_chunks - 1) / n_chunks;
    partial_results.resize(n_chunks);

    parallel_for(pool, std::size_t{0}, n_chunks,
                 [&](std::size_t lo, std::size_t hi) {
                     for (std::size_t i = lo; i < hi; ++i) {
                         const Index cb = begin + i * chunk_size;
                         const Index ce = std::min<Index>(
                             cb + static_cast<Index>(chunk_size), end);
                         T v = init;
                         map(cb, ce);  // body fills v via its own capture
                         partial_results[i] = v;
                     }
                 },
                 opt, hint);

    T result = init;
    for (auto& v : partial_results) reduce(result, v);
    return result;
}

}  // namespace vistella::tp
