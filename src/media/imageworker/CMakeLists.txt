# =============================================================================
#  vistella_imageworker - 图像处理模块 (阶段 1 W1 骨架, 2026-09-03)
#  输出: libvistella_imageworker.lib (STATIC, 跟现有 media lib 平级)
#
#  跟 vistella_media 的关系:
#    - vistella_media: 现有 ImageWindow / imageprocessor (不依赖 imageworker, 阶段 0 已存在)
#    - vistella_imageworker: 阶段 1 新增, 包含 ImageWorker (IModule) + ImageWorkspace (IWorkspace)
#                            link vistella_media (用 ImageWindow) + vistella_core (用 IModule)
#
#  设计: imageworker 是 media 的"模块化包装", 未来 audioWorker / videoWorker / visionWorker
#        同样模式: 在 src/{audio,video,vision}worker/ 加对应模块, 不改现有 MFC/Qt 类
# =============================================================================
add_library(vistella_imageworker STATIC
    ImageWorker.h
    ImageWorker.cpp
    ImageWorkspace.h
    ImageWorkspace.cpp
    # 阶段 1 W2.3 (2026-09-03): 异步滤镜 facade
    ImageProcessorAsync.h
    ImageProcessorAsync.cpp
    # 阶段 1 W4.1-W4.2 (2026-09-04): 像素图层
    layers/Layer.h
    layers/LayerStack.h
    layers/LayerStack.cpp
    # 阶段 1 W4.3 Phase 1 (2026-09-04): 图层撤销栈
    layers/LayerCommand.h
    layers/LayerCommand.cpp
    # 阶段 1 W4.3 Phase 1 (2026-09-04): 图层 dock panel
    layers/LayerPanel.h
    layers/LayerPanel.cpp
    # 阶段 1 W4.3 Phase 2 (2026-09-04): 11 种 PS 混合模式
    layers/BlendMode.h
    layers/BlendMode.cpp
    # P1.4.1 (2026-09-17): SmartObject file watcher
    #   QFileSystemWatcher 包装 + 2s mtime poll fallback. 监听所有
    #   SmartObject layer 源, emit sourceFileChanged(index, path) 供 UI 弹提示.
    layers/SmartObjectWatcher.h
    layers/SmartObjectWatcher.cpp
)

target_include_directories(vistella_imageworker
    PUBLIC
        # 让下游 (MainWindow 等) 能 #include "imageworker/ImageWorker.h"
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        # 自己内部 include 用 (跟 media 同级, .. 是 src/media)
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(vistella_imageworker
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        vistella_core      # IModule / IWorkspace / MediaDispatcher
        vistella_media     # ImageWindow
        vistella_logger
)

# 中文 UTF-8 (跟其他模块一致)
if(MSVC)
    target_compile_options(vistella_imageworker PRIVATE /utf-8)
endif()
