#pragma once

#include "config.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace vistella::tp {

struct TP_CACHELINE PoolStats {
    // Cumulative counters (monotonically increasing)
    std::atomic<std::uint64_t> submitted{0};
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> expired{0};
    std::atomic<std::uint64_t> cancelled{0};
    std::atomic<std::uint64_t> stolen{0};

    // Instantaneous
    std::atomic<std::uint64_t> queued{0};
    std::atomic<std::uint64_t> running{0};

    // Time accumulators (microseconds)
    std::atomic<std::uint64_t> wait_us{0};
    std::atomic<std::uint64_t> exec_us{0};

    // Peak values (key for OOM / stall diagnosis)
    std::atomic<std::uint64_t> peak_queued{0};
    std::atomic<std::uint64_t> peak_running{0};

    // Exceptions
    std::atomic<std::uint64_t> exceptions{0};

    void reset() noexcept {
        submitted = completed = rejected = expired = cancelled = stolen = 0;
        queued = running = 0;
        wait_us = exec_us = 0;
        peak_queued = peak_running = 0;
        exceptions = 0;
    }

    // Derived metrics
    [[nodiscard]] double avg_wait_us() const noexcept {
        auto c = completed.load(std::memory_order_relaxed);
        return c ? double(wait_us.load(std::memory_order_relaxed)) / double(c) : 0.0;
    }
    [[nodiscard]] double avg_exec_us() const noexcept {
        auto c = completed.load(std::memory_order_relaxed);
        return c ? double(exec_us.load(std::memory_order_relaxed)) / double(c) : 0.0;
    }
    // steal_ratio: too low = load imbalance (tasks too large);
    //             too high = scheduling overhead (tasks too small). Healthy: 0.05-0.4
    [[nodiscard]] double steal_ratio() const noexcept {
        auto c = completed.load(std::memory_order_relaxed);
        return c ? double(stolen.load(std::memory_order_relaxed)) / double(c) : 0.0;
    }
    // reject_ratio > 0 sustained = pool is bottleneck, scale or split
    [[nodiscard]] double reject_ratio() const noexcept {
        auto s = submitted.load(std::memory_order_relaxed);
        auto r = rejected.load(std::memory_order_relaxed);
        return (s + r) ? double(r) / double(s + r) : 0.0;
    }

    // Health check. Returns empty string if OK; non-empty = warnings.
    [[nodiscard]] std::string health_check() const {
        std::string warnings;
        if (reject_ratio() > 0.05) {
            warnings += "[WARN] reject_ratio=";
            warnings += std::to_string(reject_ratio());
            warnings += " (sustained >0.05 means pool is bottleneck)\n";
        }
        if (avg_wait_us() > 1000.0 && submitted.load() > 100) {
            warnings += "[WARN] avg_wait_us=";
            warnings += std::to_string(avg_wait_us());
            warnings += " (interactive pool expects < 1ms)\n";
        }
        if (exceptions.load() > 0) {
            warnings += "[WARN] exceptions=";
            warnings += std::to_string(exceptions.load());
            warnings += " (uncaught exception in task)\n";
        }
        return warnings;
    }
};

}  // namespace vistella::tp
