// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — Chase-Lev work stealing deque
// Owner pushes/pops from bottom, thieves steal from top.
// Capacity MUST be a power of two (compile-time enforced).
//
#pragma once

#include "config.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace vistella::tp {

enum class StealResult { Empty, Lost, Taken };

template <typename T, std::size_t Capacity = kLocalDequeCapacity>
class ChaseLevDeque {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static constexpr std::size_t kMask = Capacity - 1;

public:
    ChaseLevDeque() {
        for (auto& s : slots_) s.store(nullptr, std::memory_order_relaxed);
    }

    bool push(T item) noexcept {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed);
        const std::int64_t t = top_.load(std::memory_order_acquire);
        if (b - t >= static_cast<std::int64_t>(Capacity)) return false;  // full

        slots_[b & kMask].store(item, std::memory_order_relaxed);
        bottom_.store(b + 1, std::memory_order_release);
        return true;
    }

    T pop() noexcept {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        const std::int64_t t = top_.load(std::memory_order_relaxed);
        T item = nullptr;

        if (t > b) {
            // Deque was empty.
            bottom_.store(b + std::int64_t{1}, std::memory_order_relaxed);
            return nullptr;
        }

        item = slots_[b & kMask].load(std::memory_order_relaxed);

        if (t == b) {
            // Last element: race with stealer on top_.
            std::int64_t expected = b;
            if (!bottom_.compare_exchange_strong(
                    expected, b + std::int64_t{1},
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                // Lost: a stealer took it via CAS on top_.
                item = nullptr;
            }
            top_.store(b + std::int64_t{1}, std::memory_order_relaxed);
        } else {
            // More than one element: safe pop.
            bottom_.store(b + std::int64_t{1}, std::memory_order_relaxed);
        }
        return item;
    }

    StealResult steal(T& out) noexcept {
        std::int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::int64_t b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            T item = slots_[t & kMask].load(std::memory_order_relaxed);
            if (!top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return StealResult::Lost;  // lost race with owner or another stealer
            }
            out = item;
            return StealResult::Taken;
        }
        return StealResult::Empty;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed);
        const std::int64_t t = top_.load(std::memory_order_relaxed);
        return b > t ? static_cast<std::size_t>(b - t) : 0;
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

private:
    TP_CACHELINE std::atomic<std::int64_t> top_{0};
    TP_CACHELINE std::atomic<std::int64_t> bottom_{0};

    std::array<std::atomic<T>, Capacity> slots_{};
};

}  // namespace vistella::tp
