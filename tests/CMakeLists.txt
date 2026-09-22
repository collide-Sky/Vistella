# =============================================================================
#  tests/ — 单元测试 (阶段 1 前置优化, 2026-09-03)
#  成熟方案: Qt Test (QTest) 官方框架
#
#  覆盖目标 (阶段 0 已存在代码, 0 阶段代码回归保护):
#    - FileExtensionRegistry (纯逻辑, namespace 静态函数, 测试简单)
#    - FileTreeModel        (QAbstractItemModel 子类, 需 QApplication)
#    - MediaDispatcher      (QObject 单例, 需 QCoreApplication, 模块注册)
#
#  后续阶段加测试: 每加一个 worker / 新 class, 在 tests/ 加一个 tst_X.cpp
#                  命名规范 tst_<ClassName>.cpp
#                  每个测试 = 一个独立 exe, 出错不影响其他测试
# =============================================================================
cmake_minimum_required(VERSION 3.19)

# 顶层已经 find_package Qt6, 这里只 Test + Widgets + QuickWidgets (B.2 QQuickWidget demo 需要)
# C.1 还需要 Concurrent (QtConcurrent)
find_package(Qt6 6.5 REQUIRED COMPONENTS Test QuickWidgets Concurrent)

# Qt Test 通用设置 (PRIVATE 是测试只内部用, 不传到外)
qt_standard_project_setup()

# ---- 工具函数: 给每个 tst_X.cpp 配一个独立测试 target ----
function(add_vistella_test NAME)
    qt_add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::QuickWidgets   # B.2: tst_QQuickWidgetDemo 用
        Qt6::Concurrent    # C.1: tst_Async (QtConcurrent::run) 用
        vistella_core       # FileExtensionRegistry / MediaDispatcher / IModule / Result / Async
        vistella_ui         # FileTreeModel / FileTreeItem 在 src/ui
        vistella_imageworker  # W1: tst_ImageWorker 用 (ImageWorker / ImageWorkspace)
        vistella_media      # W1 + W2: ImageWindow / ImageProcessor / Graphicstextitem / ImageInfoPanel
        vistella_logger
    )
    # 暴露 src 子目录头, 让 #include "../src/..." 能找到
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    # 中文 UTF-8
    if(MSVC)
        target_compile_options(${NAME} PRIVATE /utf-8)
    endif()
    # 统一警告等级
    if(MSVC)
        target_compile_options(${NAME} PRIVATE /W3)
    else()
        target_compile_options(${NAME} PRIVATE -Wall -Wextra)
    endif()
    # 注册到 ctest, 用 -o 输出 xml 给 CI 解析
    add_test(NAME ${NAME} COMMAND ${NAME} -o ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.xml)
    # 给测试多一点时间 (MediaDispatcher 测试可能涉及信号循环)
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 30)
endfunction()

# ---- 16 个测试目标 (W4.4 Phase 5 2026-09-04 加 tst_SmartObject) ----
add_vistella_test(tst_FileExtensionRegistry)
add_vistella_test(tst_FileTreeModel)
add_vistella_test(tst_MediaDispatcher)
add_vistella_test(tst_QQuickWidgetDemo)
add_vistella_test(tst_Async)
add_vistella_test(tst_ImageWorker)
add_vistella_test(tst_ImageProcessor)
add_vistella_test(tst_ImageEditCommand)
add_vistella_test(tst_ImageProcessorAsync)
add_vistella_test(tst_ColorAdjust)
add_vistella_test(tst_LayerStack)
add_vistella_test(tst_LayerCommand)
add_vistella_test(tst_ToolMediator)   # F-B1 (2026-09-08): Mediator 模式单测
add_vistella_test(tst_WorkspaceMediator)   # F-B2 (2026-09-08): 工作区切换单测
add_vistella_test(tst_DialogMediator)   # F-B3 (2026-09-08): 非模态 dialog 调度单测
add_vistella_test(tst_ToolState)         # F-C (2026-09-09): State 模式 + 2 工具单测
add_vistella_test(tst_LeftToolBar)        # F-D (2026-09-09): PS 风格 LeftToolBar 2 列图标单测
add_vistella_test(tst_BlendMode)
add_vistella_test(tst_LayerKindRender)
add_vistella_test(tst_LayerMask)
add_vistella_test(tst_SmartObject)
# 2026-09-04 阶段 1+: 公共 ThreadPool 测试
add_vistella_test(tst_ThreadPool)
# P0-3.1 (2026-09-08): 8 个色彩调整 LUT 函数 + applyLut 同步/异步测试
add_vistella_test(tst_AdjustmentLut)
# F-E (2026-09-09): ImageOptionBar QStackedWidget 9 page (None + 8 工具)
add_vistella_test(tst_ImageOptionBar)
# F-F (2026-09-09): mainwindow 集成 WorkspaceMediator 4 工作区
add_vistella_test(tst_F_F_MainWindowWorkspace)
# F-G (2026-09-09): RightPanelStack 3 dock (颜色/属性/图层) 跟 WorkspaceMediator 联动
add_vistella_test(tst_RightPanelStack)
# F-J (2026-09-09): 6 主菜单 + 子菜单结构 (图像/图层/文字/选择/滤镜 5 个新菜单)
add_vistella_test(tst_F_J_MainMenuStructure)
# F-K (2026-09-09): AdjustDialogBase + DialogFactory + 5 dialog (1 HSL 实装 + 4 stub)
add_vistella_test(tst_F_K_AdjustDialog)
# F-M (2026-09-10): ColorDock 渐变 (5 类型 + 8 预设) + 图案 (6 预设) + ChannelPathPanel (6 通道 + 1 路径)
add_vistella_test(tst_F_M_Dock_GradientPatternChannel)
# F-N (2026-09-10): 8 工具 cursor (Move/SizeAll + Text/IBeam + 6 cross) + ToolContext 事件转发
add_vistella_test(tst_F_N_ToolCursor)
# F-O (2026-09-10): Throttle 通用节流器 (200ms trailing) + PropertiesDock 集成
add_vistella_test(tst_F_O_Throttle)
# P0-4 (2026-09-10): 选区系统 (SelectionModel 9 op + 3 Strategy + MarchingAnts + Command + PropertiesDock bbox)
add_vistella_test(tst_P0_4_Selection)
# P0-5 (2026-09-10): 20 滤镜 PS 同款 (Factory + 20 strategy + Command + Dialog + 集成)
add_vistella_test(tst_P0_5_FilterSystem)
# P0-6 (2026-09-14): 8 handle 自由变换系统 (TransformBox + 4 mode + TransformCommand + ImageProcessor flip/warp)
add_vistella_test(tst_P0_6_TransformSystem)
# P0-7 (2026-09-14): 文字图层 (GraphicsTextItem 几何独立 + TextOverlayController Bold/Italic + PropertiesDock 6 行)
add_vistella_test(tst_P0_7_TextSystem)
# P0-8 (2026-09-15): 历史/导出/色彩空间 (HistoryDock + ExportDialog + IccProfile LCMS2)
add_vistella_test(tst_P0_8_System)
# P0-9 (2026-09-15): 形状/矢量 + 修复工具 (ShapeTool + PenTool + Clone/Heal/Patch/RedEye)
add_vistella_test(tst_P0_9_System)
# P1.1 (2026-09-15): Brush full implementation - 数据层测试 (JSON serdes + builtins + manager)
add_vistella_test(tst_BrushDataLayer)
# P1.1 (2026-09-15): Brush engine + smoother + pressure curve 单元测试
add_vistella_test(tst_BrushEngine)
# P1.1 (2026-09-15): ABR parser 单元测试 (ABRv6 computed brush)
add_vistella_test(tst_AbrParser)
# P1.2.1 (2026-09-16): Liquify mesh 数据结构 (24 用例: build/displacement/findTriangle/6 tools)
add_vistella_test(tst_LiquifyMesh)
# P1.2.2 (2026-09-16): Liquify engine + backing store (反向 warp + bilinear + freeze + 增量渲染)
add_vistella_test(tst_LiquifyEngine)
# P1.2.7 (2026-09-16): Liquify face-aware (Haar cascade + 启发式特征点 + 6 sliders warp)
add_vistella_test(tst_LiquifyFace)
# P1.3.1 (2026-09-16): LayerMask 数据 + Renderer (12 用例: pixel/vector/density/feather/invert)
add_vistella_test(tst_LayerMaskStruct)
# P1.3.2+8 (2026-09-16): LayerStack mask 集成 + LayerMaskCommand undo
add_vistella_test(tst_LayerMaskIntegration)
# P1.3.4 (2026-09-16): MaskBrushTool 单元测试 (stamp falloff + reveal/hide + bounds)
add_vistella_test(tst_MaskBrush)
# P1.3.7 (2026-09-16): ColorRange HSV 距离 -> alpha (8 用例: full/empty/far/fuzziness/invert)
add_vistella_test(tst_ColorRange)
# P1.3.6 (2026-09-16): Refine Edge 4 sliders (smooth/feather/contrast/shift)
add_vistella_test(tst_RefineEdge)
# P1.3.5 (2026-09-16): Vector mask path -> alpha (8 用例: empty/interior/exterior/union/density/feather/invert/apply)
add_vistella_test(tst_VectorMask)
# P1.4.1 (2026-09-17): SmartObject Convert / Rasterize / Transform / Watcher (10 用例)
add_vistella_test(tst_SmartObject_v2)

# P1.5.2 (2026-09-21): LayerStack Group/Ungroup (mergeIntoGroup + flattenGroup)
add_vistella_test(tst_LayerGroup)
# P1.4.2 (2026-09-17): SmartObject UI/菜单集成 (Convert / Rasterize / Relink + Watcher resync)
add_vistella_test(tst_SmartObject_v3)
# P1.4.3 (2026-09-17): LayerPanel 实例化 + 关键信号接通 (creation / selection signal / SmartObject page)
add_vistella_test(tst_LayerPanelIntegration)
# P1.4.4 (2026-09-17): SmartFilter chain (append / remove / move / enable / render / undo)
add_vistella_test(tst_SmartFilter)
# P1.4.5 (2026-09-17): SmartObject non-destructive transform 主菜单 + 撤销栈集成
#   (set/clear transform persistence, rasterize with transform applied, undo + re-apply)
add_vistella_test(tst_SmartObjectTransform)
# P1.4.6 (2026-09-17): P1.4 UI wrap-up tests (TransformDialog 4 cases; the 26
#   LayerPanel signal wires and QMessageBox prompt are QSKIPed with reasons).
add_vistella_test(tst_P1_4_6_Integration)
# P1.4.7 (2026-09-18): WindowMode architecture regression test (4 cases
#   verifying the 3-channel entry-point unification: loadWindowState +
#   main.cpp + setMode all flow through setMode via applyWindowMode()).
add_vistella_test(tst_WindowMode)
# P0-3.2 v2 (2026-09-18): 4 standalone PS-style adjust dialogs
#   (Curves/Levels/B&W/ChannelMixer) + DialogFactory dispatch + apply flow
add_vistella_test(tst_AdjustmentDialogs)

# P1.5.1 (2026-09-18): AdjustmentPanel::setStandaloneParams dispatcher
#   Routing dialogId -> applyXxxFromArgs, params struct updates, UI slider refresh
add_vistella_test(tst_AdjustmentDispatcher)

# P2.1 (2026-09-22): Vector selection tools full implementation
#   MagicWand: contiguous / non-contiguous / tolerance sweep
#   Lasso: feathering (integral-image box blur) + anti-alias
#   MarchingAnts phase cycle
add_vistella_test(tst_LassoMagicWand)
