
//


//




#pragma once

#include <cstddef>
#include <cstdint>
#include <thread>


#if defined(_WIN32) || defined(_WIN64)
#  define TP_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#  define TP_PLATFORM_MACOS 1
#  include <TargetConditionals.h>
#elif defined(__linux__)
#  define TP_PLATFORM_LINUX 1
#else
#  define TP_PLATFORM_UNKNOWN 1
#endif



#if !defined(TP_USE_TBB)
#  define TP_USE_TBB 0
#endif
#if !defined(TP_USE_ASIO)
#  define TP_USE_ASIO 0
#endif
#if !defined(TP_USE_QT)
#  define TP_USE_QT 0
#endif




#if defined(__has_include)
#  if __has_include(<coroutine>) && defined(__cpp_impl_coroutine) && __cplusplus >= 202002L
#    define TP_HAS_COROUTINE_HEADER 1
#  else
#    define TP_HAS_COROUTINE_HEADER 0
#  endif
#else
#  define TP_HAS_COROUTINE_HEADER 0
#endif

#if !defined(TP_ENABLE_COROUTINES)
#  define TP_ENABLE_COROUTINES 0
#endif


#if defined(__GNUC__) || defined(__clang__)
#  define TP_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define TP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define TP_CACHELINE alignas(64)
#  define TP_FORCE_INLINE __attribute__((always_inline)) inline
#  define TP_NEVER_INLINE __attribute__((noinline))
#else
#  define TP_LIKELY(x)   (x)
#  define TP_UNLIKELY(x) (x)
#  define TP_CACHELINE alignas(std::hardware_destructive_interference_size)
#  define TP_FORCE_INLINE inline
#  define TP_NEVER_INLINE
#endif


namespace vistella::tp {



constexpr std::size_t kLocalDequeCapacity = 16384;


constexpr std::size_t kDefaultQueueBound = 4096;


constexpr std::size_t kMaxThreadName = 15;

// Backpressure — 队列满时的策略 (P0 ThreadPool v2 设计, 2026-09-10 补上)
//   * Block: 提交线程阻塞直到有空间
//   * DropOldest: 丢最早入队的任务, 接纳新任务
//   * Reject: 直接返回 false, 提交失败
//   * Coalesce: 合并同 key 任务 (P1+ 实现)
enum class Backpressure : std::uint8_t {
    Block      = 0,
    DropOldest = 1,
    Reject     = 2,
    Coalesce   = 3,
};





constexpr std::size_t kTargetTaskMicros = 1000;


constexpr std::size_t kMaxWorkers = 128;

inline unsigned hardware_concurrency() noexcept {
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 4 : n;
}

}  // namespace vistella::tp


#include <cassert>
#define TP_ASSERT(x) assert(x)
#define TP_ASSERT_MSG(x, msg) assert((x) && (msg))
