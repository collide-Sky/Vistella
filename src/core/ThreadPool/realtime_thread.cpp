// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — RealtimeThread
// Dedicated thread that runs `on_tick` at a fixed period. P0 #3 doc states
// this is for UI/professional audio (Pro Tools / Cubase plugin hosting) where
// steady cadence matters more than raw throughput. NOT meant for general use:
// the thread burns CPU when no work is pending, so prefer ThreadPool for
// any work that tolerates ~1ms jitter.
//
// Platform behaviour:
//   * Linux: SCHED_FIFO @ priority 80 + mlockall(MCL_CURRENT|MCL_FUTURE).
//     Caller must run as root or have CAP_SYS_NICE.
//   * macOS: thread_time_constraint_policy via thread_policy_set — a soft
//     real-time guarantee honoured by the audio HAL.
//   * Windows: MMCSS "Pro Audio" task via AvSetMmThreadCharacteristicsW.
//     Priority boost applied automatically by the OS while the handle is open.
//
#include "realtime_thread.h"

#include <chrono>
#include <string>

namespace vistella::tp {

RealtimeThread::RealtimeThread(Config cfg) : cfg_(std::move(cfg)) {
    thread_ = std::thread([this] { run_loop(); });
}

RealtimeThread::~RealtimeThread() { stop(); }

void RealtimeThread::stop() {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true)) return;
    if (thread_.joinable()) thread_.join();
}

void RealtimeThread::run_loop() {
    if (cfg_.apply_realtime_priority) apply_realtime_attrs();
    if (cfg_.on_thread_start) {
        try { cfg_.on_thread_start(); } catch (...) {}
    }

    auto next = std::chrono::steady_clock::now() + cfg_.period;
    while (!stopping_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_until(next);
        if (cfg_.on_tick) {
            try { cfg_.on_tick(next); } catch (...) {}
        }
        next += cfg_.period;

        // Drift catch-up: if the system stalled us (debugger, scheduler
        // contention, long syscall in on_tick), don't fire a burst of
        // catch-up ticks. Skip ahead to "now + one period" so we recover
        // cadence instead of trying to replay history.
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now + cfg_.period;
    }

    if (cfg_.on_thread_stop) {
        try { cfg_.on_thread_stop(); } catch (...) {}
    }
}

void RealtimeThread::apply_realtime_attrs() noexcept {
    std::string name = "tp/";
    name += cfg_.name;
    if (name.size() >= kMaxThreadName) name.resize(kMaxThreadName);

#if TP_PLATFORM_LINUX
    ::pthread_setname_np(::pthread_self(), name.c_str());
    if (cfg_.apply_realtime_priority) {
        struct sched_param sp;
        sp.sched_priority = 80;  // 1..99 for SCHED_FIFO; 80 leaves headroom
        ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &sp);
        ::mlockall(MCL_CURRENT | MCL_FUTURE);
    }
#elif TP_PLATFORM_MACOS
    ::pthread_setname_np(name.c_str());
    if (cfg_.apply_realtime_priority) {
        // 80% of period budget for computation, 100% for constraint.
        // These are best-effort hints; the audio HAL treats them strictly.
        thread_time_constraint_policy_data_t info;
        info.period = 0;             // let OS derive from cadence
        info.computation = static_cast<uint32_t>(cfg_.period.count() * 8 / 10);
        info.constraint = static_cast<uint32_t>(cfg_.period.count());
        info.preemptible = 1;
        thread_port_t mach_thread = pthread_mach_thread_np(::pthread_self());
        ::thread_policy_set(mach_thread, THREAD_TIME_CONSTRAINT_POLICY,
                            (thread_policy_t)&info,
                            THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    }
#elif TP_PLATFORM_WINDOWS
    ::SetThreadDescription(::GetCurrentThread(),
                           std::wstring(name.begin(), name.end()).c_str());
    if (cfg_.apply_realtime_priority) {
        // AvSetMmThreadCharacteristicsW("Pro Audio", &taskIndex)
        // returns a HANDLE; closing it reverts priority. We don't store
        // the handle — the OS keeps the boost until thread exit, which
        // is the right lifetime for this class.
        HANDLE task_handle = ::AvSetMmThreadCharacteristicsW(L"Pro Audio", nullptr);
        if (task_handle) {
            // Boost task priority within MMCSS to the highest band.
            ::AvSetMmThreadPriority(task_handle, AVRT_PRIORITY_CRITICAL);
        }
    }
#endif
}

}  // namespace vistella::tp
