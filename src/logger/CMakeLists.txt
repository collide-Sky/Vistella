# =============================================================================
#  vistella_logger - 独立日志模块 (SHARED 库)
#  输出: vistella_logger.dll / libvistella_logger.dll.a (Windows)
#        libvistella_logger.so  (Linux)
#        libvistella_logger.dylib (Mac)
# =============================================================================
cmake_minimum_required(VERSION 3.19)

# ---- 库名 ----
set(LOGGER_TARGET vistella_logger)
set(LOGGER_SOVERSION 1)

# ---- spdlog 路径 (放在项目 third_party/spdlog) ----
set(LOGGER_SPDLOG_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")

if(NOT EXISTS "${LOGGER_SPDLOG_INCLUDE}/spdlog/spdlog.h")
    message(FATAL_ERROR
        "spdlog not found: ${LOGGER_SPDLOG_INCLUDE}/spdlog/spdlog.h missing. "
        "请先下载 spdlog 1.15+ header-only 到 third_party/spdlog/include")
endif()

# ---- Qt 集成选项 (默认 OFF — 让 dll 纯 std C++, 减少依赖; 集成层放到主项目) ----
# 如果用户的项目需要 Qt 集成, 可以在自己的主项目里加一个 wrapper, 不要在 dll 里强制 link Qt
option(LOGGER_ENABLE_QT "Enable Qt integration (Qt5/Qt6 Core)" OFF)
option(LOGGER_BUILD_TESTS "Build logger test" OFF)

# ---- Qt 集成时才需要 Qt ----
if(LOGGER_ENABLE_QT)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
endif()

# =============================================================================
#  库目标
# =============================================================================
add_library(${LOGGER_TARGET} SHARED
    Logger.cpp
    Logger.h
)

# ---- 版本 ----
set_target_properties(${LOGGER_TARGET} PROPERTIES
    VERSION   ${LOGGER_SOVERSION}
    SOVERSION ${LOGGER_SOVERSION}
    # Windows: 输出 vistella_logger.dll (无 .lib 后缀)
)

# ---- 导出宏 ----
target_compile_definitions(${LOGGER_TARGET} PRIVATE vistella_logger_EXPORTS)

# ---- Qt 集成开关 ----
if(LOGGER_ENABLE_QT)
    target_compile_definitions(${LOGGER_TARGET} PUBLIC LOGGER_ENABLE_QT_INTEGRATION)
    target_link_libraries(${LOGGER_TARGET} PUBLIC Qt6::Core)
    # MOC 不用 (没 Q_OBJECT), 但 ui_*.h 之类的如果用 Qt 就需要
endif()

# ---- 头文件路径 ----
# PUBLIC 暴露给 link ${LOGGER_TARGET} 的目标, 让它们能 include "Logger.h"
# 用 generator expression 区分 BUILD (source dir) 和 INSTALL (安装后路径)
target_include_directories(${LOGGER_TARGET}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# ---- spdlog (header-only, 因为 Logger.h 用了 fmt::format_string, 必须 PUBLIC 暴露) ----
# 用 generator expression 区分 BUILD 和 INSTALL
target_include_directories(${LOGGER_TARGET}
    PUBLIC
        $<BUILD_INTERFACE:${LOGGER_SPDLOG_INCLUDE}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/spdlog>
)

# ---- Windows: 不定义 MIN/MAX 宏 (与 spdlog 冲突) ----
if(MSVC)
    target_compile_definitions(${LOGGER_TARGET} PRIVATE NOMINMAX)
endif()

# ---- UTF-8 源 (中文直接写在代码里) ----
if(MSVC)
    target_compile_options(${LOGGER_TARGET} PRIVATE /utf-8)
endif()

# ---- C++ 标准 ----
target_compile_features(${LOGGER_TARGET} PUBLIC cxx_std_17)

# ---- 编译选项 ----
if(MSVC)
    target_compile_options(${LOGGER_TARGET} PRIVATE /W3)
else()
    target_compile_options(${LOGGER_TARGET} PRIVATE -Wall -Wextra)
endif()

# ---- Windows: 让 dll 也能找到依赖的 Qt6Core.dll (运行时) ----
if(WIN32 AND LOGGER_ENABLE_QT)
    # 用 Qt 的 windeployqt 或复制 dll — 这里只 install, 不主动部署
    # 用户用 windeployqt 或者自己 PATH 设置
    set_target_properties(${LOGGER_TARGET} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# ---- Linux/Mac: 默认有 rpath ----
if(UNIX)
    set_target_properties(${LOGGER_TARGET} PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
endif()

# =============================================================================
#  安装规则 (make install 时)
# =============================================================================
include(GNUInstallDirs)
install(TARGETS ${LOGGER_TARGET}
    EXPORT ${LOGGER_TARGET}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}     # dll / so
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}     # so / dylib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}     # .lib / .a
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES Logger.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ${LOGGER_TARGET}Targets
    FILE ${LOGGER_TARGET}Targets.cmake
    NAMESPACE vistella::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LOGGER_TARGET}/
)

# =============================================================================
#  可选测试
# =============================================================================
if(LOGGER_BUILD_TESTS)
    add_executable(${LOGGER_TARGET}_test test_main.cpp)
    target_link_libraries(${LOGGER_TARGET}_test PRIVATE ${LOGGER_TARGET})
endif()
