
//





//

#pragma once

#include "config.hpp"
#include "executor.hpp"
#include "serial_executor.h"
#include "thread_pool.h"

#include <memory>
#include <sstream>
#include <string>

namespace vistella::tp {

class EngineContext {
public:
    // ====================================================================

    enum class Profile {

        ImageEditor,

        VideoEditor,

        AudioWorkstation,

        General,
    };

    struct Config {
        Profile profile = Profile::General;
        std::size_t hw_threads = hardware_concurrency();


        std::size_t interactive_threads = 0;
        std::size_t background_threads  = 0;
        std::size_t io_threads          = 0;
        std::size_t ai_threads          = 0;
        std::size_t offline_threads     = 0;


        Backpressure interactive_bp = Backpressure::DropOldest;
        Backpressure background_bp  = Backpressure::DropOldest;
        Backpressure io_bp          = Backpressure::Block;
        Backpressure ai_bp          = Backpressure::Reject;
        Backpressure offline_bp     = Backpressure::Block;

        std::size_t interactive_capacity = 2048;
        std::size_t background_capacity  = 4096;
        std::size_t io_capacity          = 4096;
        std::size_t ai_capacity          = 512;
        std::size_t offline_capacity     = 8192;

        bool enable_gpu_thread   = true;
        bool enable_ai_pool      = true;
        bool enable_offline_pool = true;
    };


    static Config make_profile(Profile p,
                               std::size_t hw = hardware_concurrency());

    explicit EngineContext(Config cfg);
    ~EngineContext();

    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;


    [[nodiscard]] IExecutor& interactive() noexcept;
    [[nodiscard]] IExecutor& background()  noexcept;
    [[nodiscard]] IExecutor& io()          noexcept;
    [[nodiscard]] IExecutor& ai()          noexcept;
    [[nodiscard]] IExecutor& offline()     noexcept;
    [[nodiscard]] SerialExecutor& gpu()     noexcept;

    [[nodiscard]] PoolStats& stats_interactive() noexcept;
    [[nodiscard]] PoolStats& stats_background()  noexcept;
    [[nodiscard]] PoolStats& stats_io()          noexcept;
    [[nodiscard]] PoolStats& stats_ai()          noexcept;
    [[nodiscard]] PoolStats& stats_offline()     noexcept;
    [[nodiscard]] PoolStats& stats_gpu()         noexcept;

    // One-line per-pool snapshot. Empty optional fields are skipped.
    [[nodiscard]] std::string stats_report() const;





    static EngineContext* current() noexcept;
    static void set_current(EngineContext* ctx) noexcept;
    static void reset_current() noexcept;


    struct ScopedCurrent {
        EngineContext* prev_;
        explicit ScopedCurrent(EngineContext* ctx);
        ~ScopedCurrent();
        ScopedCurrent(const ScopedCurrent&) = delete;
        ScopedCurrent& operator=(const ScopedCurrent&) = delete;
    };

private:
    Config cfg_;
    std::unique_ptr<ThreadPool> interactive_;
    std::unique_ptr<ThreadPool> background_;
    std::unique_ptr<ThreadPool> io_;
    std::unique_ptr<ThreadPool> ai_;
    std::unique_ptr<ThreadPool> offline_;
    std::unique_ptr<SerialExecutor> gpu_;
};

}  // namespace vistella::tp
