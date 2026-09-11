






//


#pragma once

#include "config.hpp"
#include "cancellation.hpp"
#include "executor.hpp"
#include "stats.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#if TP_PLATFORM_LINUX || TP_PLATFORM_MACOS
#  include <pthread.h>
#  include <sched.h>
#endif

namespace vistella::tp {

class SerialExecutor final : public IExecutor {
public:
    struct Config {
        std::string name = "serial";
        std::size_t queue_capacity = 1024;
        Backpressure backpressure = Backpressure::Reject;


        std::function<void()> on_thread_start;

        std::function<void()> on_thread_stop;



        int affinity_cpu = -1;


        bool idle_yield = true;
    };

    explicit SerialExecutor(Config cfg);
    ~SerialExecutor() override;

    bool submit(TaskPtr task) override;
    void shutdown(bool cancel_pending = false) override;
    void attach_continuation(Task* parent, TaskPtr cont) override;

    [[nodiscard]] bool is_shutting_down() const noexcept override;
    [[nodiscard]] PoolStats& stats() noexcept override { return stats_; }
    [[nodiscard]] std::size_t thread_count() const noexcept override { return 1; }
    [[nodiscard]] const char* name() const noexcept override { return name_.c_str(); }




    void run_sync(std::function<void()> fn);

    [[nodiscard]] bool is_serial_thread() const noexcept {
        return std::this_thread::get_id() == thread_.get_id();
    }

private:
    void run_loop();

    Config cfg_;
    std::string name_;
    std::thread thread_;

    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable space_cv_;
    std::condition_variable done_cv_;
    std::deque<Task*> queue_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> joined_{false};
    std::atomic<std::uint64_t> next_task_id_{0};

    PoolStats stats_;
    std::exception_ptr last_exception_;
};

}  // namespace vistella::tp
