# core: 业务逻辑层, 纯 C++/Qt (no UI)
# 任何 ui / media 子目录都能 link 这个
# 日志模块已独立到 src/logger (vistella_logger SHARED 库)
# 公共 ThreadPool 已独立到 src/core/ThreadPool (vistella_threadpool STATIC 库)
add_subdirectory(ThreadPool)
add_library(vistella_core STATIC
    recentmanager.cpp
    recentmanager.h
    ThemeManager.cpp
    ThemeManager.h
    SessionManager.cpp
    SessionManager.h
    LanguageManager.cpp
    LanguageManager.h
    # IModule / IWorkspace 接口 (2026-09-02 阶段 0 新增, 纯头文件)
    IModule.h
    # MediaDispatcher 中央文件分发器 (2026-09-02 阶段 0 新增)
    MediaDispatcher.cpp
    MediaDispatcher.h
    # FileExtensionRegistry 扩展名注册表 (决策 5, 2026-09-02)
    FileExtensionRegistry.cpp
    FileExtensionRegistry.h

    # Throttle (F-O 2026-09-10): generic trailing-edge debouncer
    Throttle.h
    Throttle.cpp
)
target_include_directories(vistella_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# P0 ThreadPool v2 (2026-09-10): vistella_threadpool link (DAG 是 header-only 模板, 不需实装 .cpp)
target_link_libraries(vistella_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets vistella_logger vistella_threadpool)
