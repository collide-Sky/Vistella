// SPDX-License-Identifier: MIT
//
// ThreadPool v2 implementation.
// P0 items implemented here:
//   #1 Block backpressure cannot deadlock when caller is itself a worker
//      of the same pool (push_injected short-circuits to Reject).
//   #5 DAG continuation (attach_continuation + schedule_continuations).
//
#include "thread_pool.h"

#include <cstring>

namespace vistella::tp {

// ============================================================================
// Construction / Destruction
// ============================================================================
ThreadPool::ThreadPool(PoolConfig cfg) : cfg_(std::move(cfg)) {
    name_ = cfg_.name.empty() ? role_name(cfg_.role) : cfg_.name;
    const std::size_t n =
        cfg_.thread_count ? cfg_.thread_count : default_thread_count(cfg_.role);
    fprintf(stderr, "[TP %s] ctor n=%zu\n", name_.c_str(), n);

    n_threads_ = n;
    workers_.reserve(n);
    deques_ = std::make_unique<Deque[]>(n);

    shutting_down_.store(false, std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i) {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
    fprintf(stderr, "[TP %s] launched, spinning for ready\n", name_.c_str());

    // Spin-wait for all workers to register their started_count_ increment.
    // We can't use start_cv_ here: workers also wait on start_cv_ for `ready_`
    // after they've incremented started_count_, and if the ctor releases
    // start_mu_ inside a cvar.wait() the workers' second wait will steal the
    // lock first, leaving the ctor starved (and the cvar's wait queue mixed
    // between the ctor's predicate check and workers' ready_ check).
    // Each worker startup is a single std::thread::joinable() + lambda setup,
    // well under a millisecond on Windows; a busy spin is acceptable here.
    for (int spins = 0;
         started_count_.load(std::memory_order_acquire) < static_cast<int>(n);
         ++spins) {
        if (spins < 1000) {
            cpu_relax();
        } else {
            std::this_thread::yield();
        }
        if (spins > 1'000'000) {
            // ~1s of busy spin without progress = something is wrong; bail
            // rather than spin forever.
            shutting_down_.store(true, std::memory_order_release);
            start_cv_.notify_all();
            return;
        }
    }

    // All workers registered. Flip ready_ and release them under the lock so
    // no worker that's still in the cvar-notify path can miss the signal.
    {
        std::lock_guard<std::mutex> lk(start_mu_);
        ready_.store(true, std::memory_order_release);
        start_cv_.notify_all();
    }
    fprintf(stderr, "[TP %s] ctor done\n", name_.c_str());
}

ThreadPool::~ThreadPool() { shutdown(true); }

// ============================================================================
// Submission
// ============================================================================
bool ThreadPool::submit(TaskPtr task) {
    if (!task || !task->fn) return false;
    if (shutting_down_.load(std::memory_order_acquire)) return false;

    task->submit_time = std::chrono::steady_clock::now();
    task->id = next_task_id_.fetch_add(1, std::memory_order_relaxed);

    // P0 #1: from_worker = caller is a worker of THIS pool. Used to break
    // Block backpressure self-deadlock inside push_injected.
    const std::size_t self = current_worker_index();
    const bool from_worker = (self < n_threads_ && tls_pool_ == this);

    if (from_worker) {
        // Fast path: try to put it on the calling worker's local deque.
        if (deques_[self].push(task.get())) {
            task.release();
            stats_.submitted.fetch_add(1, std::memory_order_relaxed);
            wake_one();
            return true;
        }
        // Local deque full — fall through to injected queue.
    }
    return push_injected(std::move(task), from_worker);
}

// ============================================================================
// Shutdown
// ============================================================================
void ThreadPool::shutdown(bool cancel_pending) {
    bool expected = false;
    if (!shutting_down_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        // Another thread already initiated shutdown — just wait for join.
        std::unique_lock<std::mutex> lk(mu_);
        done_cv_.wait(lk, [this] { return join_done_.load(std::memory_order_acquire); });
        return;
    }

    if (cancel_pending) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& q : injected_) {
            for (Task* raw : q) delete raw;
            q.clear();
        }
    }

    // Wake every worker so they observe shutting_down_ and exit.
    // std::thread::join() is the actual wait — it blocks until the thread
    // function returns. The previous version of this code used a separate
    // cv_.wait(active_workers_==0) guard, which had a missed-notify race:
    // a worker waking up, observing shutting_down_, and decrementing
    // active_workers_ could happen AFTER shutdown had already entered its
    // cv_.wait, and the worker's exit path did not call notify, so the
    // predicate would never re-check and shutdown would hang forever.
    cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }

    // Drain per-worker deques; tasks submitted directly to the local deque
    // by workers themselves bypass the injected queue.
    for (std::size_t i = 0; i < n_threads_; ++i) {
        Task* raw = nullptr;
        while ((raw = deques_[i].pop()) != nullptr) delete raw;
    }

    join_done_.store(true, std::memory_order_release);
    done_cv_.notify_all();
}

bool ThreadPool::is_shutting_down() const noexcept {
    return shutting_down_.load(std::memory_order_acquire);
}

// ============================================================================
// Local execution
// ============================================================================
bool ThreadPool::try_execute_one() {
    Task* raw = pop_local_or_steal();
    if (!raw) return false;
    run_task(raw);
    return true;
}

void ThreadPool::wait_while_working(std::atomic<int>& counter, int target) {
    int spins = 0;
    while (counter.load(std::memory_order_acquire) > target) {
        if (try_execute_one()) { spins = 0; continue; }
        if (shutting_down_.load(std::memory_order_acquire)) break;
        if (++spins < 32) {
            cpu_relax();
        } else if (spins < 128) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            spins = 64;
        }
    }
}

std::size_t ThreadPool::current_worker_index() const noexcept {
    const auto* idx = tls_worker_index_;
    if (!idx || tls_pool_ != this) return static_cast<std::size_t>(-1);
    return *idx;
}

// ============================================================================
// DAG continuation (P0 #5)
// ============================================================================
void ThreadPool::attach_continuation(Task* parent, TaskPtr cont) {
    if (!parent || !cont) return;
    cont->pending_deps.fetch_add(1, std::memory_order_acq_rel);
    // Prepend cont to parent's continuation list. Single-writer (the
    // scheduler) under normal use, but use CAS-loop in case attach_continuation
    // is called from multiple threads on the same parent.
    Task* old = parent->continuation;
    do {
        cont->continuation = old;
    } while (!std::atomic_compare_exchange_strong(
        reinterpret_cast<std::atomic<Task*>*>(&parent->continuation),
        &old, cont.release()));
}

void ThreadPool::run_task(Task* t) {
    if (t->expired() || t->is_cancelled()) {
        if (t->expired())
            stats_.expired.fetch_add(1, std::memory_order_relaxed);
        if (t->is_cancelled())
            stats_.cancelled.fetch_add(1, std::memory_order_relaxed);
        stats_.completed.fetch_add(1, std::memory_order_relaxed);
        schedule_continuations(t);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    stats_.running.fetch_add(1, std::memory_order_relaxed);
    stats_.wait_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - t->submit_time).count(), std::memory_order_relaxed);

    CancellationToken null_token;
    TaskContext ctx{t->token ? *t->token : null_token,
                    static_cast<std::uint32_t>(current_worker_index()),
                    name_.c_str()};
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

    schedule_continuations(t);
}

void ThreadPool::schedule_continuations(Task* finished) {
    Task* cur = finished->continuation;
    while (cur) {
        Task* next = cur->continuation;
        cur->continuation = nullptr;
        int prev = cur->pending_deps.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            submit_continuation_task(cur);
        } else {
            // pending_deps > 1 means another predecessor will pick it up;
            // we don't own `cur` here, so we MUST NOT delete it.
        }
        cur = next;
    }
}

void ThreadPool::submit_continuation_task(Task* t) {
    TaskPtr owned(t);
    submit(std::move(owned));
}

// ============================================================================
// Work-stealing deque / injected queue
// ============================================================================
Task* ThreadPool::pop_local_or_steal() {
    const std::size_t n = n_threads_;
    const std::size_t self = current_worker_index();

    if (self < n) {
        if (Task* raw = deques_[self].pop()) return raw;
    }

    if (Task* raw = pop_injected()) return raw;

    if (cfg_.enable_stealing && n > 1) {
        const std::size_t start = rng_next() % n;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t victim = (start + k) % n;
            if (victim == self) continue;
            Task* raw = nullptr;
            if (deques_[victim].steal(raw) == StealResult::Taken && raw) {
                stats_.stolen.fetch_add(1, std::memory_order_relaxed);
                return raw;
            }
        }
    }
    return nullptr;
}

// ============================================================================
// Injected queue
// ============================================================================
bool ThreadPool::push_injected(TaskPtr task, bool from_worker) {
    const std::size_t bound = cfg_.queue_capacity;
    std::unique_lock<std::mutex> lk(mu_);

    const std::size_t total = injected_[0].size() + injected_[1].size() +
                              injected_[2].size();
    if (total >= bound) {
        switch (cfg_.backpressure) {
            case Backpressure::Block:
                // P0 #1: a worker of THIS pool must not Block here — it would
                // deadlock if all workers are blocked on the same cv_ that
                // only a worker would unblock. Fall through to Reject.
                if (from_worker) {
                    stats_.rejected.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                queued_cv_.wait(lk, [&] {
                    return shutting_down_.load(std::memory_order_acquire) ||
                           (injected_[0].size() + injected_[1].size() +
                            injected_[2].size()) < bound;
                });
                if (shutting_down_.load(std::memory_order_acquire)) return false;
                break;

            case Backpressure::DropOldest:
                if (!injected_[2].empty())      injected_[2].pop_front();
                else if (!injected_[1].empty()) injected_[1].pop_front();
                else                            injected_[0].pop_front();
                stats_.expired.fetch_add(1, std::memory_order_relaxed);
                break;

            case Backpressure::Reject:
            default:
                stats_.rejected.fetch_add(1, std::memory_order_relaxed);
                return false;
        }
    }

    auto& q = injected_[static_cast<std::size_t>(task->priority)];
    q.push_back(task.release());
    const std::size_t cur = injected_[0].size() + injected_[1].size() +
                            injected_[2].size();
    stats_.submitted.fetch_add(1, std::memory_order_relaxed);
    stats_.queued.store(cur, std::memory_order_relaxed);
    update_peak(cur);
    lk.unlock();
    wake_one();
    return true;
}

Task* ThreadPool::pop_injected() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& q : injected_) {
        if (!q.empty()) {
            Task* raw = q.front();
            q.pop_front();
            const std::size_t cur = injected_[0].size() + injected_[1].size() +
                                    injected_[2].size();
            stats_.queued.store(cur, std::memory_order_relaxed);
            if (cfg_.backpressure == Backpressure::Block) queued_cv_.notify_one();
            return raw;
        }
    }
    return nullptr;
}

void ThreadPool::update_peak(std::size_t cur) noexcept {
    auto prev = stats_.peak_queued.load(std::memory_order_relaxed);
    while (cur > prev) {
        if (stats_.peak_queued.compare_exchange_weak(
                prev, cur, std::memory_order_acq_rel)) break;
    }
}

// ============================================================================
// Worker loop
// ============================================================================
void ThreadPool::worker_loop(std::size_t idx) {
    fprintf(stderr, "[wp %s/%zu] enter\n", name_.c_str(), idx);
    worker_idx_storage_[idx] = idx;
    tls_worker_index_ = &worker_idx_storage_[idx];
    tls_pool_ = this;

    set_thread_name(name_.c_str(), idx);

    {
        std::lock_guard<std::mutex> lk(start_mu_);
        started_count_.fetch_add(1, std::memory_order_acq_rel);
    }
    fprintf(stderr, "[wp %s/%zu] started_count=%d\n", name_.c_str(), idx, started_count_.load());
    start_cv_.notify_one();

    // Synchronization barrier: all workers wait until the ctor flips ready_.
    {
        std::unique_lock<std::mutex> lk(start_mu_);
        start_cv_.wait(lk, [this] { return ready_.load(std::memory_order_acquire); });
    }
    fprintf(stderr, "[wp %s/%zu] past ready\n", name_.c_str(), idx);

    while (!shutting_down_.load(std::memory_order_acquire)) {
        Task* t = pop_local_or_steal();
        if (t) {
            run_task(t);
            continue;
        }

        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] {
            return shutting_down_.load(std::memory_order_acquire) ||
                   !injected_[0].empty() || !injected_[1].empty() ||
                   !injected_[2].empty();
        });
    }
}

void ThreadPool::wake_one() noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    cv_.notify_one();
}

void ThreadPool::cpu_relax() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_ia32_pause();
#endif
}

std::size_t ThreadPool::rng_next() noexcept {
    return static_cast<std::size_t>(rng_state_.fetch_add(1, std::memory_order_relaxed));
}

void ThreadPool::set_thread_name(const char* base, std::size_t idx) noexcept {
#if TP_PLATFORM_LINUX
    std::string name = "tp/";
    name += base;
    name += "-";
    name += std::to_string(idx);
    if (name.size() >= kMaxThreadName) name.resize(kMaxThreadName);
    ::pthread_setname_np(::pthread_self(), name.c_str());
#elif TP_PLATFORM_MACOS
    std::string name = "tp/";
    name += base;
    name += "-";
    name += std::to_string(idx);
    if (name.size() >= kMaxThreadName) name.resize(kMaxThreadName);
    ::pthread_setname_np(name.c_str());
#elif TP_PLATFORM_WINDOWS
    (void)base; (void)idx;
    // Windows lacks a portable per-thread name setter short of RaiseException
    // with a magic cookie; the debugger sees "tp-WorkerPool-N" via the
    // thread handle but tools like Process Explorer / VS see no friendly name.
#endif
}

}  // namespace vistella::tp
