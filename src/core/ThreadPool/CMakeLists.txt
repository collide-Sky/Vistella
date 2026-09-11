# ThreadPool: 公共 ThreadPool v2 (2026-09-07)
#   Directory: src/core/ThreadPool/ (no include/ nesting)
#   Namespace: vistella::tp
#   STATIC lib, 6 pools + 1 GPU serial + DAG + cancel tree
#
# File suffix rule (2026-09-07, per user):
#   - Templated code  -> .hpp (must inline template body)
#     * work_stealing_queue.hpp  (ChaseLevDeque<T, Capacity>)
#     * parallel_for.hpp          (ParallelForRunner<Index, Body>)
#     * task_graph.hpp            (make_chain<Pool, Fns...>)
#   - Simple / header-only -> .hpp
#     * config / tls / stats / executor / cancellation / backends/qt_bridge
#   - Large non-template -> .h + .cpp split
#     * thread_pool / serial_executor / realtime_thread / engine_context
#
# IMPORTANT (2026-09-07, per user): every .h/.hpp/.cpp that is part of the
# library MUST be listed in SOURCES, otherwise the IDE file tree cannot
# show it (and refactor/grep/jump-to-definition break).
add_library(vistella_threadpool STATIC
    # Headers (.hpp) -- listed so IDE can show them in the file tree
    config.hpp
    tls.hpp
    stats.hpp
    executor.hpp
    cancellation.hpp
    work_stealing_queue.hpp
    parallel_for.hpp
    task_graph.hpp
    backends/qt_bridge.hpp

    # Headers (.h) -- interface for non-template implementations
    thread_pool.h
    serial_executor.h
    realtime_thread.h
    engine_context.h

    # Sources (.cpp) -- non-template implementations
    thread_pool.cpp
    serial_executor.cpp
    realtime_thread.cpp
    engine_context.cpp
)

target_include_directories(vistella_threadpool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vistella_threadpool PUBLIC cxx_std_17)
target_link_libraries(vistella_threadpool PUBLIC Qt6::Core)
