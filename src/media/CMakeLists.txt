# media: 图像/视频处理模块
# 本次只做图像 (image). 视频以后再加.
add_library(vistella_media STATIC
    imageprocessor.h
    imageprocessor.cpp

    imagewindow.h
    imagewindow.cpp
    imagewindow.ui

    # P0-1: 5 split-out components. The .h files are listed even when
    # unused by ImageWindow in P0-1.1 (component instances are still nullptr
    # at that stage); P0-1.2 instantiates them and migrates methods.
    imagewindow/ImageCanvas.h
    imagewindow/ImageCanvas.cpp
    imagewindow/MosaicTool.h
    imagewindow/MosaicTool.cpp
    imagewindow/TextOverlayController.h
    imagewindow/TextOverlayController.cpp
    imagewindow/ImageIOController.h
    imagewindow/ImageIOController.cpp

    # P0-3.2 (2026-09-08): PS 风格色彩调整 UI - 5 tab (Curves/Levels/HSL/B&W/ChannelMixer)
    #   AdjustmentPanel.h 内嵌 CurveEditor (自定义 QWidget, 8 控制点拖动)
    imagewindow/AdjustmentPanel.h
    imagewindow/AdjustmentPanel.cpp

    graphicstextitem.h
    graphicstextitem.cpp   # 主流做法的可编辑文字对象 (QGraphicsTextItem 派生)

    # P0-8.3 (2026-09-15): ICC profile (LCMS2 wrapper)
    icc/IccProfile.h
    icc/IccProfile.cpp
    # P0-8.3 (2026-09-15): ICC profile embed to PNG (iCCP chunk) + JPEG (APP2 marker)
    icc/IccPngEmbed.h
    icc/IccPngEmbed.cpp
    icc/IccJpegEmbed.h
    icc/IccJpegEmbed.cpp
    # P0-8.4 (2026-09-15): ICC profile embed to TIFF (ICCProfile tag in IFD)
    icc/IccTiffEmbed.h
    icc/IccTiffEmbed.cpp

    # P1.1 (2026-09-15): Brush full implementation (PS 同款)
    #   数据层: BrushSettings/BrushShape/BrushDynamics + BrushPreset/Manager
    brushes/BrushSettings.h
    brushes/BrushDynamics.h
    brushes/BrushShape.h
    brushes/BrushPreset.h
    brushes/BrushPreset.cpp
    #   引擎层: stamp 生成 + paint + scatter/texture/dual
    brushes/BrushEngine.h
    brushes/BrushEngine.cpp
    #   UX 层: 压感曲线 + 笔触平滑 + PickerDialog
    brushes/PressureCurve.h
    brushes/PressureCurve.cpp
    brushes/StrokeSmoother.h
    brushes/StrokeSmoother.cpp
    brushes/BrushPickerDialog.h
    brushes/BrushPickerDialog.cpp
    #   ABR (Adobe Brush) 导入 - P1.1 (ABRv6 computed brush)
    brushes/AbrParser.h
    brushes/AbrParser.cpp

    # F-B1 (2026-09-08): Mediator 模式拆 3 个, ToolMediator 负责工具切换
    #   一个类一个方向, 避免 god class (user 拍板)
    mediators/ToolMediator.h
    mediators/ToolMediator.cpp

    # F-B2 (2026-09-08): WorkspaceMediator 负责工作区切换 + 3 dock 内容配置
    mediators/WorkspaceMediator.h
    mediators/WorkspaceMediator.cpp

    # F-B3 (2026-09-08): DialogMediator 负责非模态 dialog 调度 (PS 同款 close=hide)
    mediators/DialogMediator.h
    mediators/DialogMediator.cpp

    # F-C (2026-09-09): State 模式 + 2 工具实例
    #   ToolState 抽象 + ToolContext 状态机管理 + MoveTool + EyedropperTool
    tools/ToolState.h
    tools/ToolContext.h
    tools/ToolContext.cpp
    tools/MoveTool.h
    tools/MoveTool.cpp
    tools/EyedropperTool.h
    tools/EyedropperTool.cpp
    # F-L (2026-09-10): 补 6 工具 + optionPage 接入 ImageOptionBar
    tools/RectSelect.h
    tools/RectSelect.cpp
    tools/Lasso.h
    tools/Lasso.cpp
    tools/MagicWand.h
    tools/MagicWand.cpp
    tools/Crop.h
    tools/Crop.cpp
    tools/Text.h
    tools/Text.cpp
    tools/Brush.h
    tools/Brush.cpp
    # P0-6.9 (2026-09-14): TransformTool (Ctrl+T 自由变换)
    tools/TransformTool.h
    tools/TransformTool.cpp
    # P0-9.1 (2026-09-15): ShapeTool (PS 同款 Rectangle/Ellipse/Line/Polygon/Custom)
    tools/ShapeTool.h
    tools/ShapeTool.cpp
    # P0-9.2 (2026-09-15): PenTool (PS 同款钢笔)
    tools/PenTool.h
    tools/PenTool.cpp
    # P0-9.3 (2026-09-15): 修复工具 (Clone/Heal/Patch/RedEye)
    tools/CloneTool.h
    tools/CloneTool.cpp
    tools/HealTool.h
    tools/RedEyeTool.h
    tools/RedEyeTool.cpp
    tools/PatchTool.h
    tools/PatchTool.cpp

    # F-D (2026-09-09): LeftToolBar PS 风格 2 列图标 (2 工具)
    tools/LeftToolBar.h
    tools/LeftToolBar.cpp
    # F-P.3 (2026-09-09): LeftToolBar 转 .ui (PS 风格 2 列图标)
    tools/LeftToolBar.ui
    # P1.3.4 (2026-09-17): ToolStateFactory - 12 工具 switch 集中点
    #   LeftToolBar + ToolContext 都调 createToolState(id) 拿 ToolState*
    tools/ToolStateFactory.h
    tools/ToolStateFactory.cpp
    # F-E (2026-09-09): ImageOptionBar 二级工具栏 QStackedWidget 8 工具 page
    tools/ImageOptionBar.h
    tools/ImageOptionBar.cpp
    tools/ImageOptionBar.ui

    # P1.1 (2026-09-15): Brush full implementation - Brush 二级工具栏 (PS 风格 options panel)
    tools/BrushOptionsPanel.h
    tools/BrushOptionsPanel.cpp
    tools/BrushOptionsPanel.ui

    # F-G (2026-09-09): RightPanelStack 右侧 panel 3 dock (颜色/属性/图层)
    docks/RightPanelStack.h
    docks/RightPanelStack.cpp
    # F-G.3 Fix (2026-09-10): RightPanelDock 1 widget 4+1 tab 浮动 panel (替代 3 分 dock + addDockWidget)
    docks/RightPanelDock.h
    docks/RightPanelDock.cpp
    docks/ColorDock.h
    docks/ColorDock.cpp
    # F-P.3 (2026-09-09): ColorDock 转 .ui
    # F-M (2026-09-10): 加渐变 (gradient) 编辑器 + 图案 (pattern) 选择器
    docks/ColorDock.ui
    docks/PropertiesDock.h
    docks/PropertiesDock.cpp
    docks/LayersDock.h
    docks/LayersDock.cpp
    # F-M.3 (2026-09-10): ChannelPathPanel (LayersDock tab 1 通道+路径)
    docks/ChannelPathPanel.h
    docks/ChannelPathPanel.cpp
    docks/ChannelPathPanel.ui
    # P1.4.6 (2026-09-17): Unified transform dialog (Scale + Rotate + Translate
    #   + Link + Reset) replacing P1.4.5's chained QInputDialog.
    docks/TransformDialog.h
    docks/TransformDialog.cpp
    # P0-8.1 (2026-09-15): HistoryDock (PS 同款 history panel, RightPanelDock 第 6 tab)
    docks/HistoryDock.h
    docks/HistoryDock.cpp

    # F-K (2026-09-09): AdjustDialogBase + DialogFactory + 5 dialog (1 实装 + 4 stub)
    # P0-3.2 v2 (2026-09-18): 4 stub 全部实装 (Curves/Levels/B&W/ChannelMixer)
    dialogs/AdjustDialogBase.h
    dialogs/AdjustDialogBase.cpp
    dialogs/DialogFactory.h
    dialogs/DialogFactory.cpp
    dialogs/HslAdjustDialog.h
    dialogs/HslAdjustDialog.cpp
    dialogs/CurvesAdjustDialog.h
    dialogs/CurvesAdjustDialog.cpp
    dialogs/LevelsAdjustDialog.h
    dialogs/LevelsAdjustDialog.cpp
    dialogs/BlackWhiteAdjustDialog.h
    dialogs/BlackWhiteAdjustDialog.cpp
    dialogs/ChannelMixerAdjustDialog.h
    dialogs/ChannelMixerAdjustDialog.cpp
    # P0-8.2 (2026-09-15): 多格式导出对话框 (PS 同款 Save For Web 风格)
    dialogs/ExportDialog.h
    dialogs/ExportDialog.cpp

    # P0-4 (2026-09-10): 选区系统 (SelectionModel + 4 Strategy + MarchingAnts + Command)
    selection/SelectionModel.h
    selection/SelectionModel.cpp
    selection/SelectionStrategy.h
    selection/SelectionStrategy.cpp
    selection/MarchingAnts.h
    selection/MarchingAnts.cpp
    selection/SelectionCommand.h
    selection/SelectionCommand.cpp

    # P0-5 (2026-09-10): 滤镜系统 (20 滤镜 PS 同款: FilterStrategy + Factory + Command + Dialog)
    filters/FilterStrategy.h
    filters/FilterStrategy.cpp
    filters/FilterFactory.h
    filters/FilterFactory.cpp
    filters/FilterCommand.h
    filters/FilterCommand.cpp
    filters/FilterDialog.h
    filters/FilterDialog.cpp

    # P0-6 (2026-09-14): 变换系统 (PS 风格 8 handle 自由变换: TransformBox + TransformMath + TransformCommand)
    transform/TransformBox.h
    transform/TransformBox.cpp
    transform/TransformMath.h
    transform/TransformMath.cpp
    transform/TransformCommand.h
    transform/TransformCommand.cpp

    # P1.2.1 (2026-09-16): Liquify mesh data structure (网格变形场 + 6 工具数学)
    filters/liquify/LiquifyMesh.h
    filters/liquify/LiquifyMesh.cpp
    # P1.2.2 (2026-09-16): Liquify backing store + engine (反向 warp + freeze + 增量渲染)
    filters/liquify/LiquifyBackingStore.h
    filters/liquify/LiquifyBackingStore.cpp
    filters/liquify/LiquifyEngine.h
    filters/liquify/LiquifyEngine.cpp
    # P1.2.5+6 (2026-09-16): Liquify UI (OptionsPanel + Canvas + Dialog + 冻结画笔)
    filters/liquify/LiquifyOptionsPanel.h
    filters/liquify/LiquifyOptionsPanel.cpp
    filters/liquify/LiquifyCanvas.h
    filters/liquify/LiquifyCanvas.cpp
    filters/liquify/LiquifyDialog.h
    filters/liquify/LiquifyDialog.cpp
    # P1.2.8 (2026-09-16): Liquify undo (LiquifyCommand + ImageEditCommand 集成)
    filters/liquify/LiquifyCommand.h
    filters/liquify/LiquifyCommand.cpp
    # P1.2.7 (2026-09-16): Liquify face-aware (Haar cascade + 启发式特征点 + 6 sliders 径向 warp)
    filters/liquify/FaceDetector.h
    filters/liquify/FaceDetector.cpp
    filters/liquify/LiquifyFaceAware.h
    filters/liquify/LiquifyFaceAware.cpp
    # P1.3.1 (2026-09-16): LayerMask 数据结构 (pixel + vector + density + feather + invert)
    imageworker/layers/LayerMask.h
    imageworker/layers/LayerMaskRenderer.h
    imageworker/layers/LayerMaskRenderer.cpp
    # P1.3.2+8 (2026-09-16): LayerStack 扩展接口 + LayerMaskCommand undo
    imageworker/layers/LayerMaskCommand.h
    imageworker/layers/LayerMaskCommand.cpp
    # P1.3.4 (2026-09-16): MaskBrushTool + MaskOptionsPanel + MaskBrushCommand
    tools/MaskBrushTool.h
    tools/MaskBrushTool.cpp
    tools/MaskOptionsPanel.h
    tools/MaskOptionsPanel.cpp
    tools/MaskBrushCommand.h
    tools/MaskBrushCommand.cpp
    # P1.3.7 (2026-09-16): ColorRange 颜色 -> 蒙版 (HSV 距离 + Fuzziness 阈值)
    masks/ColorRange.h
    masks/ColorRange.cpp
    # P1.3.6 (2026-09-16): Refine Edge 4 sliders (smooth/feather/contrast/shift)
    masks/RefineEdge.h
    masks/RefineEdge.cpp
)

target_include_directories(vistella_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_media
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        # F-P.1 logger 统一: mediators/tools #include "logger.h" 走 PUBLIC include path
        vistella_logger
        # P0-8.3 (2026-09-15): LCMS2 for ICC profile (EmbedProfileToPng/Jpeg/Tiff)
        vistella_lcms2
        # P0-8.3: zlib symbols are bundled in Qt6Core (no separate link needed)
        #   #include <QtZlib/zlib.h> works as long as Qt6::Core is linked
    PRIVATE
        # imagewindow 用了 RecentManager::instance()
        vistella_core
)

# OpenCV include + lib: PUBLIC 因为 imagewindow.h 直接 #include <opencv2/core.hpp>
# 阶段 1 前置优化 (2026-09-03): 改用 imported target 而非 ${OpenCV_LIBS} 变量
#   背景: 本机 OpenCV 4.12.0 是 BUILD_opencv_world=ON 模式, 只导出 `opencv_world`
#         target, 没有 OpenCV::core / OpenCV::imgproc 等子模块 target.
#         ${OpenCV_LIBS} 变量指向 opencv_world4120.lib (世界库, 127MB debug DLL)
#   现状: 用 `opencv_world` target, 等同于 ${OpenCV_LIBS}, 但形式更显式
#   未来: 如果重新 build OpenCV 关掉 BUILD_opencv_world=OFF (几小时, 拆模块),
#         把 `opencv_world` 改成 `OpenCV::core OpenCV::imgproc OpenCV::imgcodecs
#         OpenCV::highgui OpenCV::videoio` 即可按需 link, 不需要改其他代码
if(vistella_WITH_OPENCV)
    target_include_directories(vistella_media PUBLIC ${OpenCV_INCLUDE_DIRS})
    if(TARGET opencv_world)
        # OpenCV 4.12 world build 模式
        target_link_libraries(vistella_media PUBLIC opencv_world)
    else()
        # OpenCV 4.x split build 模式 (未来如果重 build OpenCV)
        target_link_libraries(vistella_media PUBLIC
            OpenCV::core
            OpenCV::imgproc
            OpenCV::imgcodecs
            OpenCV::highgui
            OpenCV::videoio
        )
    endif()
endif()
