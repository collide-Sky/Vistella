# ai: AI 模型管理 (2026-09-02 阶段 0 新增)
#   横向服务, 4 大 Worker (image/audio/video/vision) 都通过 AIManager 调 AI 模型
#   阶段 0 只有接口 + 单例, 没有具体模型实现
#   阶段 4+ 才会加 YOLOv8 / SAM / ESRGAN 等具体模型 + ONNX Runtime link
add_library(vistella_ai STATIC
    AIManager.cpp
    AIManager.h
    # IAIModel.h 纯头文件, 不需要编译
)
target_include_directories(vistella_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_ai PUBLIC Qt6::Core Qt6::Gui vistella_logger)
