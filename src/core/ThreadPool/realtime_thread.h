
//




//










//




#pragma once

#include "config.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#if TP_PLATFORM_LINUX
#  include <pthread.h>
#  include <sched.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#elif TP_PLATFORM_MACOS
#  include <mach/mach.h>
#  include <mach/thread_policy.h>
#  include <pthread.h>
#  include <sys/mman.h>
#elif TP_PLATFORM_WINDOWS
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <avrt.h>
#endif

namespace vistella::tp {

// ============================================================================

// ============================================================================




///


template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static constexpr std::size_t kMask = Capacity - 1;

public:


    template <class U>
    bool try_push(U&& v) noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;      // full
        buf_[w & kMask] = std::forward<U>(v);
        write_.store(w + 1, std::memory_order_release);
        return true;
    }



    T try_pop() noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        if (r == w) return T{};                  // empty
        T v = std::move(buf_[r & kMask]);
        read_.store(r + 1, std::memory_order_release);
        return v;
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_.load(std::memory_order_acquire) ==
               write_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return write_.load(std::memory_order_acquire) -
               read_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
    T buf_[Capacity]{};
};

// ============================================================================

// ============================================================================
class RealtimeThread {
public:
    struct Config {
        std::string name = "realtime";

        std::chrono::microseconds period{2667};

        bool software_clock = true;

        bool apply_realtime_priority = true;

        std::function<void()> on_thread_start;

        std::function<void(std::chrono::steady_clock::time_point)> on_tick;
        std::function<void()> on_thread_stop;

    };

    explicit RealtimeThread(Config cfg);
    ~RealtimeThread();

    RealtimeThread(const RealtimeThread&) = delete;
    RealtimeThread& operator=(const RealtimeThread&) = delete;

    void stop();

private:
    void run_loop();
    void apply_realtime_attrs() noexcept;

    Config cfg_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
};

}  // namespace vistella::tp
