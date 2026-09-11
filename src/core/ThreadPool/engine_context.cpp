

#include "engine_context.h"

#include <cstring>
#include <utility>

namespace vistella::tp {

// ============================================================================

// ============================================================================
static EngineContext* s_current_ = nullptr;

EngineContext* EngineContext::current() noexcept { return s_current_; }
void EngineContext::set_current(EngineContext* ctx) noexcept { s_current_ = ctx; }
void EngineContext::reset_current() noexcept { s_current_ = nullptr; }

EngineContext::ScopedCurrent::ScopedCurrent(EngineContext* ctx)
    : prev_(s_current_) {
    s_current_ = ctx;
}
EngineContext::ScopedCurrent::~ScopedCurrent() { s_current_ = prev_; }

// ============================================================================

// ============================================================================
EngineContext::Config EngineContext::make_profile(Profile p, std::size_t hw) {
    Config c;
    c.profile = p;
    c.hw_threads = hw ? hw : hardware_concurrency();
    const std::size_t n = c.hw_threads;

    switch (p) {
    case Profile::ImageEditor:
        c.interactive_threads  = n > 1 ? n - 1 : 1;
        c.background_threads   = n;
        c.io_threads           = n * 2;
        c.ai_threads           = n > 2 ? 2 : 1;
        c.offline_threads      = n;
        c.interactive_bp       = Backpressure::DropOldest;
        c.interactive_capacity = 2048;
        break;

    case Profile::VideoEditor:
        c.interactive_threads  = n > 1 ? n - 1 : 1;
        c.background_threads   = n;
        c.io_threads           = n * 3;
        c.ai_threads           = 2;
        c.offline_threads      = n;
        c.interactive_bp       = Backpressure::DropOldest;
        c.interactive_capacity = 1024;
        c.io_bp                = Backpressure::Block;
        break;

    case Profile::AudioWorkstation:
        c.interactive_threads  = n > 2 ? 2 : 1;
        c.background_threads   = n;
        c.io_threads           = n * 2;
        c.ai_threads           = 2;
        c.offline_threads      = n;
        c.interactive_bp       = Backpressure::DropOldest;
        c.io_bp                = Backpressure::Block;
        break;

    case Profile::General:
    default:
        c.interactive_threads = n > 1 ? n - 1 : 1;
        c.background_threads  = n;
        c.io_threads          = n * 2;
        c.ai_threads          = 2;
        c.offline_threads     = n;
        break;
    }
    return c;
}

// ============================================================================

// ============================================================================
EngineContext::EngineContext(Config cfg) : cfg_(std::move(cfg)) {
    const std::size_t n = cfg_.hw_threads;
    fprintf(stderr, "[EC] start, n=%zu\n", n);

    fprintf(stderr, "[EC] interactive\n");
    interactive_ = std::make_unique<ThreadPool>(PoolConfig{
        Role::Interactive, "interactive",
        cfg_.interactive_threads ? cfg_.interactive_threads
                                 : default_thread_count(Role::Interactive),
        -2, cfg_.interactive_capacity, cfg_.interactive_bp, true, {}, 0});
    fprintf(stderr, "[EC] interactive done\n");

    fprintf(stderr, "[EC] background\n");
    background_ = std::make_unique<ThreadPool>(PoolConfig{
        Role::Background, "background",
        cfg_.background_threads ? cfg_.background_threads : n,
        2, cfg_.background_capacity, cfg_.background_bp, true, {}, 0});
    fprintf(stderr, "[EC] background done\n");

    fprintf(stderr, "[EC] io (n=%zu)\n", cfg_.io_threads ? cfg_.io_threads : n*2);
    io_ = std::make_unique<ThreadPool>(PoolConfig{
        Role::IO, "io",
        cfg_.io_threads ? cfg_.io_threads : n * 2,
        0, cfg_.io_capacity, cfg_.io_bp, false, {}, 0});
    fprintf(stderr, "[EC] io done\n");

    if (cfg_.enable_ai_pool) {
        fprintf(stderr, "[EC] ai\n");
        ai_ = std::make_unique<ThreadPool>(PoolConfig{
            Role::AI, "ai",
            cfg_.ai_threads ? cfg_.ai_threads : 2,
            4, cfg_.ai_capacity, cfg_.ai_bp, true, {}, 0});
        fprintf(stderr, "[EC] ai done\n");
    }

    if (cfg_.enable_offline_pool) {
        fprintf(stderr, "[EC] offline\n");
        offline_ = std::make_unique<ThreadPool>(PoolConfig{
            Role::Offline, "offline",
            cfg_.offline_threads ? cfg_.offline_threads : n,
            3, cfg_.offline_capacity, cfg_.offline_bp, true, {}, 0});
        fprintf(stderr, "[EC] offline done\n");
    }

    if (cfg_.enable_gpu_thread) {
        SerialExecutor::Config sc;
        sc.name = "gpu";
        sc.backpressure = Backpressure::Block;
        gpu_ = std::make_unique<SerialExecutor>(std::move(sc));
    }
}

EngineContext::~EngineContext() {

    if (ai_)        ai_->shutdown(false);
    if (gpu_)       gpu_->shutdown(false);
    if (offline_)   offline_->shutdown(false);
    if (io_)        io_->shutdown(false);
    if (background_) background_->shutdown(false);
    if (interactive_) interactive_->shutdown(false);
}

// ============================================================================

IExecutor& EngineContext::interactive() noexcept { return *interactive_; }
IExecutor& EngineContext::background()  noexcept { return *background_; }
IExecutor& EngineContext::io()          noexcept { return *io_; }
IExecutor& EngineContext::ai()          noexcept {
    return ai_ ? static_cast<IExecutor&>(*ai_) : *io_;
}
IExecutor& EngineContext::offline()     noexcept {
    return offline_ ? static_cast<IExecutor&>(*offline_) : *background_;
}
SerialExecutor& EngineContext::gpu()     noexcept { return *gpu_; }

PoolStats& EngineContext::stats_interactive() noexcept { return interactive_->stats(); }
PoolStats& EngineContext::stats_background()  noexcept { return background_->stats(); }
PoolStats& EngineContext::stats_io()          noexcept { return io_->stats(); }
PoolStats& EngineContext::stats_ai()          noexcept {
    return ai_ ? ai_->stats() : io_->stats();
}
PoolStats& EngineContext::stats_offline()     noexcept {
    return offline_ ? offline_->stats() : background_->stats();
}
PoolStats& EngineContext::stats_gpu()         noexcept { return gpu_->stats(); }

// ============================================================================

std::string EngineContext::stats_report() const {
    std::ostringstream os;
    os << "Engine stats (profile="
       << (cfg_.profile == Profile::ImageEditor ? "ImageEditor" :
           cfg_.profile == Profile::VideoEditor ? "VideoEditor" :
           cfg_.profile == Profile::AudioWorkstation ? "AudioWorkstation" : "General")
       << "):\n";
    struct Named { const char* name; PoolStats* s; } stats[] = {
        {"interactive", interactive_ ? &interactive_->stats() : nullptr},
        {"background",  background_  ? &background_->stats()  : nullptr},
        {"io",          io_          ? &io_->stats()          : nullptr},
        {"ai",          ai_          ? &ai_->stats()          : nullptr},
        {"offline",     offline_     ? &offline_->stats()     : nullptr},
        {"gpu",         gpu_         ? &gpu_->stats()         : nullptr},
    };
    for (auto& ns : stats) {
        if (!ns.s) continue;
        os << "  [" << ns.name << "] submitted=" << ns.s->submitted.load()
           << " completed=" << ns.s->completed.load()
           << " rejected=" << ns.s->rejected.load()
           << " expired=" << ns.s->expired.load()
           << " cancelled=" << ns.s->cancelled.load()
           << " exceptions=" << ns.s->exceptions.load()
           << " avg_wait_us=" << ns.s->avg_wait_us()
           << " avg_exec_us=" << ns.s->avg_exec_us()
           << "\n";
    }
    return os.str();
}

}  // namespace vistella::tp
