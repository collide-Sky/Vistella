# app: 入口 (main.cpp + MainWindow)
add_library(vistella_app STATIC
    mainwindow.h
    mainwindow.cpp
    mainwindow.ui
    main.cpp
)

target_include_directories(vistella_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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
