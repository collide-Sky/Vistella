
//





//



#pragma once

#include "config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace vistella::tp {

// ============================================================================
// Backpressure is defined in config.hpp. This file only consumes it.
// ============================================================================

inline const char* backpressure_name(Backpressure bp) noexcept {
    switch (bp) {
        case Backpressure::Block:      return "Block";
        case Backpressure::Reject:     return "Reject";
        case Backpressure::DropOldest: return "DropOldest";
    }
    return "Unknown";
}

// ============================================================================

// ============================================================================


///





///

///   for (int i = 0; i < n; ++i) {
///       if (ctx.token->cancelled()) return Result::err(Cancelled, ...);
///       doWork();
///   }
class CancellationToken {
public:
    CancellationToken() = default;

    ~CancellationToken() {

        if (parent_ && parent_->children_) {
            std::lock_guard<std::mutex> lk(parent_->children_->mu);
            auto& v = parent_->children_->children;
            v.erase(std::remove(v.begin(), v.end(), this), v.end());
        }
    }


    void cancel() noexcept {
        bool was = flag_.exchange(true, std::memory_order_acq_rel);
        if (was) return;  // , 

        if (children_) {

            std::vector<CancellationToken*> snapshot;
            {
                std::lock_guard<std::mutex> lk(children_->mu);
                snapshot = children_->children;
            }
            for (auto* c : snapshot) {
                c->cancel();
            }
        }
    }


    [[nodiscard]] bool cancelled() const noexcept {
        return flag_.load(std::memory_order_acquire);
    }


    explicit operator bool() const noexcept { return cancelled(); }



    CancellationToken* create_child() {
        auto* child = new CancellationToken();
        child->parent_ = this;
        if (!children_) {
            children_ = std::make_shared<ChildrenList>();
        }
        {
            std::lock_guard<std::mutex> lk(children_->mu);
            children_->children.push_back(child);
        }
        return child;
    }


    [[nodiscard]] const CancellationToken* parent() const noexcept { return parent_; }

private:

    struct ChildrenList {
        std::mutex mu;
        std::vector<CancellationToken*> children;
    };

    std::atomic<bool> flag_{false};
    CancellationToken* parent_ = nullptr;
    std::shared_ptr<ChildrenList> children_;
};

// ============================================================================

// ============================================================================

class ScopedCancellation {
public:
    explicit ScopedCancellation(CancellationToken* root = nullptr) : root_(root) {}
    ~ScopedCancellation() { if (root_) root_->cancel(); }
    ScopedCancellation(const ScopedCancellation&) = delete;
    ScopedCancellation& operator=(const ScopedCancellation&) = delete;
    CancellationToken* root() const noexcept { return root_; }
private:
    CancellationToken* root_;
};

// ============================================================================

// ============================================================================
class Generation {
public:
    using Value = std::uint64_t;

    [[nodiscard]] Value current() const noexcept {
        return value_.load(std::memory_order_acquire);
    }


    void bump() noexcept {
        value_.fetch_add(1, std::memory_order_acq_rel);
    }


    class Snapshot {
    public:
        explicit Snapshot(const Generation& g) noexcept
            : gen_(&g), v_(g.current()) {}
        [[nodiscard]] bool expired() const noexcept { return gen_->current() != v_; }
        [[nodiscard]] Value value() const noexcept { return v_; }
        [[nodiscard]] static const Generation* owner_of(const Snapshot& s) noexcept {
            return s.gen_;
        }
    private:
        const Generation* gen_;
        Value             v_;
    };

    [[nodiscard]] Snapshot snapshot() const noexcept { return Snapshot(*this); }

private:
    std::atomic<Value> value_{0};
};

// ============================================================================

// ============================================================================
class Deadline {
public:
    Deadline() = default;
    static Deadline none() noexcept { return Deadline{}; }
    static Deadline microseconds(std::int64_t us) noexcept {
        Deadline d; d.us_ = us; return d;
    }
    template <class Rep, class Period>
    static Deadline from(std::chrono::duration<Rep, Period> d) noexcept {
        return microseconds(
            std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] bool has_deadline() const noexcept { return us_ >= 0; }
    [[nodiscard]] std::int64_t micros() const noexcept { return us_; }

    [[nodiscard]] bool expired_since(std::chrono::steady_clock::time_point t0)
        const noexcept {
        if (!has_deadline()) return false;
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        return elapsed > us_;
    }

private:
    std::int64_t us_ = -1;
};

}  // namespace vistella::tp
