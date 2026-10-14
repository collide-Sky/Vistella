# app: 入口 (main.cpp + MainWindow)
add_library(vistella_app STATIC
    mainwindow.h
    mainwindow.cpp
    mainwindow.ui
    main.cpp
)

target_include_directories(vistella_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# P7.6.x v3 (2026-10-14): mainwindow.h include imagewindow.h (QPointer<ImageWindow>
#   实例化需要完整 ImageWindow). imagewindow.h transitively 拉 opencv2/core.hpp
#   (MosaicTool.h 用 cv::Mat). vistella_media 已带 opencv include, 但 vistella_app
#   独立 target — 把 opencv include + library 传过来.
#   PUBLIC 而非 PRIVATE: mainwindow.h 是 vistella_app 的 public header, 任何 include
#   它的 target (Vistella 主 exe / 单元测试 / 未来其他 exe) 都隐式需要 opencv 头.
#   之前 PRIVATE 导致 Vistella target 编译 main.cpp 时找不到 opencv2/core.hpp.
if(vistella_WITH_OPENCV)
    target_include_directories(vistella_app PUBLIC ${OpenCV_INCLUDE_DIRS})
endif()
target_link_libraries(vistella_app
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick       # 启动屏 QML
        Qt6::Qml
        vistella_logger   # 独立日志模块 (SHARED 库)
    PRIVATE
        vistella_core
        vistella_models
        vistella_ui
        vistella_media
        vistella_ai
        vistella_user
        vistella_network
        vistella_imageworker  # 阶段 1 W1 (2026-09-03): ImageWorker / ImageWorkspace 实现
        # vistella_threadpool  # 阶段 1+ (2026-09-07): 公共 ThreadPool v2 (header-only)
        #   2026-09-10 暂不 link, 等 P0 ThreadPool v2 完整实装 (Backpressure/PoolStats 等)
        # vistella_media3d 已删除 (3D 模型模块移除, 2026-09-02)
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
