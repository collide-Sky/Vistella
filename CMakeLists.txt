cmake_minimum_required(VERSION 3.19)
project(Vistella LANGUAGES CXX)

# 源代码全部 UTF-8 (中文直接写在 tr() / QStringLiteral 里)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---- 第三方库（按需开关） ----
#   3D 模型模块已移除 (2026-09-02), 移除关联: Eigen3 / Assimp / Qt3D
#   保留: OpenCV (图像处理) — 后续扩展音频/视频/图像识别
option(vistella_WITH_OPENCV  "Enable OpenCV for image/video docs"   ON)

# ---- Qt6 基础 ----
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql Quick Qml LinguistTools)

# ---- OpenCV (成熟方案: 多源 fallback 链) ----
#   优先级: OpenCV_DIR 环境变量 > 缓存变量 > vcpkg > 常见 Windows 路径
#   用 CONFIG 模式优先 (官方推荐), MODULE 模式兜底
#   vcpkg 不可用时 (网络限制), 仍能 fallback 到手动安装路径
if(vistella_WITH_OPENCV)
    # 1. 用户环境变量 OPENCV_DIR 优先 (vcpkg / 跨平台部署时常用)
    if(DEFINED ENV{OPENCV_DIR} AND NOT DEFINED OpenCV_DIR)
        set(OpenCV_DIR $ENV{OPENCV_DIR})
        message(STATUS "OpenCV: using OPENCV_DIR env = ${OpenCV_DIR}")
    endif()

    # 2. vcpkg 路径 (vcpkg install opencv:x64-windows)
    if(NOT DEFINED OpenCV_DIR AND DEFINED CMAKE_TOOLCHAIN_FILE)
        if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
            set(_vcpkg_opencv "${CMAKE_TOOLCHAIN_FILE}/../../installed/x64-windows/share/opencv")
            if(EXISTS "${_vcpkg_opencv}/OpenCVConfig.cmake")
                set(OpenCV_DIR "${_vcpkg_opencv}")
                message(STATUS "OpenCV: auto-detected from vcpkg = ${OpenCV_DIR}")
            endif()
        endif()
    endif()

    # 3. 常见 Windows 安装路径 (本机手动下载的 OpenCV)
    if(NOT DEFINED OpenCV_DIR)
        foreach(_path
            "D:/Collide/opencv/build"              # 用户当前手动下载
            "C:/opencv/build"
            "C:/Program Files/opencv/build"
            "C:/Program Files (x86)/opencv/build"
            "$ENV{HOME}/opencv/build"
            "/usr/local/opencv"
            "/opt/opencv"
        )
            if(EXISTS "${_path}/OpenCVConfig.cmake")
                set(OpenCV_DIR "${_path}")
                message(STATUS "OpenCV: auto-detected = ${OpenCV_DIR}")
                break()
            endif()
        endforeach()
    endif()

    # 4. 设置默认值 (用户覆盖用)
    if(NOT DEFINED OpenCV_DIR)
        set(OpenCV_DIR "D:/Collide/opencv/build" CACHE PATH "OpenCV config dir (auto-detect failed)")
    endif()

    # 5. 找 OpenCV (CONFIG 模式优先, 官方 OpenCVConfig.cmake)
    #   阶段 1 前置优化 (2026-09-03): 移除 REQUIRED 关键字中的 components
    #   本机 OpenCV 4.12.0 是 world build 模式, 没有 core / imgproc 等子模块 target,
    #   REQUIRED COMPONENTS 会让 cmake 失败. 改用普通 REQUIRED, COMPONENTS 留给 split build 模式.
    #   需要先找包进行初始化，然后再设置使用哪一种
    find_package(OpenCV 4 CONFIG QUIET)
    if(NOT OpenCV_FOUND)
        find_package(OpenCV 4 CONFIG REQUIRED COMPONENTS core imgproc imgcodecs highgui videoio)
    endif()
    if(TARGET opencv_world)
        set(OPENCV_TARGET opencv_world)
    else()
        set(OPENCV_TARGET ${OpenCV_LIBS})
    endif()
    message(STATUS "OpenCV ${OpenCV_VERSION} found: ${OpenCV_DIR}")
endif()

# ---- LibRaw (Camera Raw 解码, P3.5 2026-09-23) ----
#   Optional: 没装 libraw 时 CameraRawLoader 提供 stub 接口, build 仍 pass,
#   UI 流程 (Camera Raw 菜单 + 扩展名注册) 完整. 真链接时自动启用解码路径.
#   安装: vcpkg install libraw:x64-windows (或 apt install libraw-dev)
find_package(LibRaw QUIET CONFIG)
if(LibRaw_FOUND)
    message(STATUS "LibRaw ${LibRaw_VERSION} found: ${LibRaw_DIR}")
    set(MULTIDOC_HAVE_LIBRAW ON CACHE BOOL "LibRaw library found, enable CameraRawLoader real path" FORCE)
    # 链接: VISIBLE 给 vistella_media 用
    set(VISTELLA_LIBS ${VISTELLA_LIBS} LibRaw::LibRaw)
else()
    message(STATUS "LibRaw not found: CameraRawLoader stub mode (vcpkg install libraw:x64-windows)")
    set(MULTIDOC_HAVE_LIBRAW OFF CACHE BOOL "LibRaw not found, CameraRawLoader stub only" FORCE)
endif()

qt_standard_project_setup()

# ---- 单元测试 (阶段 1 前置优化, 2026-09-03) ----
#   成熟方案: Qt Test (QTest) + ctest
#   enable_testing() 必须在 add_subdirectory(tests) 之前
#   include(CTest) 让 ctest 命令可用, CI 可跑
enable_testing()
include(CTest)

# ---- 启用子目录 ----
add_subdirectory(src/logger)    # 独立日志模块 (SHARED 库 vistella_logger.dll)
add_subdirectory(third_party/lcms2-2.18)   # P0-8.3 (2026-09-15): LCMS2 color management
add_subdirectory(src/core)
add_subdirectory(src/models)
add_subdirectory(src/ui)
add_subdirectory(src/media)
add_subdirectory(src/media/imageworker)   # 阶段 1 W1 新增: imageWorker 模块骨架 (2026-09-03)
add_subdirectory(src/ai)        # AI 模型管理 (阶段 0 第 3 步新增, 横向服务)
add_subdirectory(src/user)      # 用户管理 (阶段 0 第 4 步新增, 含 SQLite 本地存储)
add_subdirectory(src/network)   # 网络层 (阶段 0 第 9 步新增, AuthClient + HttpClient, 阶段 5+ 真实现)
# src/media3d 已删除 (3D 模型模块移除)
add_subdirectory(src/app)

# ---- 单元测试 (阶段 1 前置, 2026-09-03) ----
#   必须 add_subdirectory 在 enable_testing() 之后
#   阶段 1+ 加新 worker 时, 在 tests/ 加 tst_<NewWorker>.cpp
add_subdirectory(tests)

# ---- 主可执行文件 ----
qt_add_executable(Vistella
    WIN32 MACOSX_BUNDLE
    src/app/main.cpp
)
# P0 ThreadPool v2 (2026-09-10): 暂不 link vistella_threadpool, 等 Backpressure/PoolStats 补完
# target_link_libraries(Vistella PRIVATE vistella_app vistella_threadpool)
target_link_libraries(Vistella PRIVATE vistella_app)

# ---- QML 资源 (启动屏等) ----
qt_add_resources(Vistella "vistella_qml"
    PREFIX "/"
    FILES
        src/app/qml/splash_screen.qml
)

# ---- QSS 资源 (统一样式入口, ThemeManager 运行时替换主题色占位符) ----
qt_add_resources(Vistella "vistella_styles"
    PREFIX "/styles"
    BASE "src/app/styles"
    FILES
        src/app/styles/Vistella.qss
)

# 把 QML 复制到 build 目录 (跟 exe 同级), main.cpp 用 applicationDirPath() 加载,
# 避开 qrc 资源路径在不同 Qt 版本里变化的问题
add_custom_command(TARGET Vistella POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:Vistella>/qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/qml/splash_screen.qml
        $<TARGET_FILE_DIR:Vistella>/qml/splash_screen.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/styles/Vistella.qss
        $<TARGET_FILE_DIR:Vistella>/styles/Vistella.qss
)

# 2026-09-02 修复 QSqlDatabase "Driver not loaded" (QSQLITE 不可用):
#   根因: Qt6 SQL drivers 是 plugins, 运行时要从 exe 同级 sqldrivers/ 找 qsqlited.dll
#         之前 build 目录没 sqldrivers/ 子目录, QSqlDatabase 找不到驱动 → 启动失败
#   修法: 用 windeployqt 在 build 时自动复制所有 Qt 依赖 (sqldrivers, platforms, imageformats, tls 等)
#         windeployqt 知道每个 Qt 模块需要哪些 plugins, 比手抄安全
#   行为: 每次 build 完调一次 windeployqt, 已存在文件不复制 (add_custom_command 默认行为)
#         --no-translations: 不复制 .qm (我们用 qt_add_translations 走资源系统)
#         --no-system-d3d-compiler / --no-opengl-sw: 体积/启动快, 不常用
find_program(WINDEPLOYQT_EXE
    NAMES windeployqt.exe windeployqt
    HINTS ${Qt6_DIR}/../../../bin
    DOC "Qt6 deployment tool"
)
if(WINDEPLOYQT_EXE)
    # 2026-09-03 修复 DWrite 堆破坏 (Qt6Guid.dll release 跟 debug CRT 不兼容):
    #   根因: Qt SDK 同时有 release + debug 版同名 dll (Qt6Core.dll / Qt6Guid.dll / Qt6Qml.dll 等)
    #         windeployqt 看到 build 目录已有 "Qt6Guid.dll" (release 版, 28MB) 就跳过,
    #         但 Vistella 是 debug build, 应该用 Qt6Guidd.dll
    #         Windows loader 加载 release 版 → 跨 DLL CRT 不一致 → 堆破坏
    #   修法: windeployqt 之前先按 build 类型删 build 目录里 "不带 d" 的 Qt 主库 dll
    #         强制 windeployqt 复制正确版本
    set(_md_qt_mainlibs
        Qt6Core Qt6Gui Qt6Qml Qt6Sql Qt6Widgets Qt6Quick Qt6Network
        Qt6Concurrent Qt6OpenGL Qt6Svg Qt6ShaderTools Qt6VirtualKeyboard
        Qt6Lottie Qt6LottieVectorImageGenerator Qt6QmlMeta Qt6QmlModels
        Qt6QmlWorkerScript Qt6QuickVectorImageGenerator Qt6QuickWidgets
    )
    set(_md_qt_remove_cmd ${CMAKE_COMMAND} -E remove)
    foreach(_lib IN LISTS _md_qt_mainlibs)
        list(APPEND _md_qt_remove_cmd
            "$<TARGET_FILE_DIR:Vistella>/${_lib}.dll")
    endforeach()
    add_custom_command(TARGET Vistella POST_BUILD
        # 1. 先按 build 类型强制删除不匹配的 Qt 主库 dll
        #    Debug build: 删所有 "Qt6Xxx.dll" (不带 d, release 版)
        #    Release build: 删所有 "Qt6Xxxd.dll" (带 d, debug 版)
        COMMAND ${_md_qt_remove_cmd}
        # 2. 再跑 windeployqt 复制正确版本
        COMMAND ${WINDEPLOYQT_EXE}
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            "$<TARGET_FILE:Vistella>"
        COMMENT "Cleanup mismatched Qt DLLs + run windeployqt for Vistella"
    )
    message(STATUS "windeployqt: ${WINDEPLOYQT_EXE}")
else()
    message(WARNING "windeployqt not found; Qt plugins (sqldrivers, ...) not auto-deployed. "
                    "QSqlDatabase / QML / image formats may fail at runtime.")
endif()

# PhaseA.3 (2026-09-03): 自动复制 OpenCV runtime DLLs 到 build 目录
#   成熟方案: add_custom_command POST_BUILD 用 cmake -E copy_if_different
#   关键: link 用 opencv_world4120d.lib (debug) 或 opencv_world4120.lib (release)
#          但 build 目录通常只有一份 .dll — link 配置跟 dll 不一致会秒退
#          症状: EXE 启动 6ms 后 ExitProcess, 找不到 .dll
#   修法: 用 OpenCV_BIN_DIR 路径, 精确复制当前 build type 对应的 dll
if(vistella_WITH_OPENCV AND WINDEPLOYQT_EXE)
    if(DEFINED OpenCV_BIN_DIR)
        set(_md_opencv_bin "${OpenCV_BIN_DIR}")
    else()
        # 兜底: 常见路径 (x64/vc16/bin 是官方 Windows OpenCV 4.x 标准布局)
        set(_md_opencv_bin "${OpenCV_DIR}/x64/vc16/bin")
    endif()
    if(EXISTS "${_md_opencv_bin}")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_md_opencv_dlls
                "${_md_opencv_bin}/opencv_world4120d.dll"
                "${_md_opencv_bin}/opencv_videoio_ffmpeg4120_64.dll"
            )
        else()
            set(_md_opencv_dlls
                "${_md_opencv_bin}/opencv_world4120.dll"
                "${_md_opencv_bin}/opencv_videoio_ffmpeg4120_64.dll"
            )
        endif()
        add_custom_command(TARGET Vistella POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_md_opencv_dlls}
                "$<TARGET_FILE_DIR:Vistella>"
            COMMENT "Copying OpenCV ${CMAKE_BUILD_TYPE} runtime DLLs to build dir"
        )
        message(STATUS "OpenCV runtime DLLs: will copy from ${_md_opencv_bin} on POST_BUILD")
    endif()
endif()

# ============================================================
# 翻译文件 (translatefile/) - zh_CN / en_US / ja_JP
# 流程:
#   1. 编辑源码中的 tr() 字符串
#   2. 跑 cmake --build . 触发 lupdate 重新生成 .ts
#   3. 用 Qt Linguist / 文本编辑器 翻译 .ts
#   4. 跑 cmake --build . 触发 lrelease 生成 .qm
#   5. main.cpp 启动时根据 QSettings("App/language") 加载对应 .qm
# qt_add_translations 会自动:
#   - 加 lupdate target  (build 时扫描 tr() 生成 .ts)
#   - 加 lrelease target (build 时把 .ts 编成 .qm)
#   - 用 RESOURCE_PREFIX 把 .qm 嵌入到 rcc 资源里, 运行时 :/translatefile/... 访问
# ============================================================
qt_add_translations(Vistella
    TS_FILES
        translatefile/vistella_zh_CN.ts
        translatefile/vistella_en_US.ts
        translatefile/vistella_ja_JP.ts
    SOURCE_TARGETS
        vistella_core vistella_models vistella_ui vistella_media vistella_ai vistella_user vistella_network vistella_app
    RESOURCE_PREFIX "/translatefile"
)

# 关键: 让默认 build (`cmake --build .`) 自动跑 lupdate + lrelease
#   qt_add_translations 默认只生成 update_translations / release_translations phony target,
#   不会自动被 Vistella build 触发 — 必须显式 --target 才会跑, 不然 .ts 一直停留旧版本
#   必须把 update_translations + release_translations 都接进来:
#     - update_translations 依赖 vistella_lupdate (CUSTOM_COMMAND 实际跑 lupdate 扫源码 tr())
#     - release_translations 依赖 .qm (只 .ts 变化时才重 build, 但不会主动跑 lupdate)
#   翻译内容 (en_US / ja_JP) 仍需手动填, 但 .ts 时间戳永远跟源码同步
add_dependencies(Vistella update_translations release_translations)

# ---- 第三方库链接（可执行级） ----
#   3D 模块已移除, 不再 link Qt3D / assimp / Eigen3
#   OpenCV 由 vistella_media 间接拉过来

# OpenCV 由 vistella_media 间接拉过来

install(TARGETS Vistella
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Vistella
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
