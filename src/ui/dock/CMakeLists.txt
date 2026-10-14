# ui/dock: VDraggableTabWidget + VRenderPath/VCompositeStrategy placeholders
#   P7.6.x v3 commit 2 (2026-10-14): PS-style draggable tab container + GPU/CPU
#   interface placeholders. Real GPU/CPU wiring lives in P8.2 / P8.4.
add_library(vistella_ui_dock STATIC
    VRenderPath.h
    VCompositeStrategy.h
    VDropIndicator.h
    VDropIndicator.cpp
    VDragManager.h
    VDragManager.cpp
    VDraggableTabWidget.h
    VDraggableTabWidget.cpp
    VTabPageBase.h
    VTabPageBase.cpp
    VPages.h
    VPages.cpp
)

target_include_directories(vistella_ui_dock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_ui_dock
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    PRIVATE
        vistella_core       # Throttle.h (PropertiesDock uses it)
        vistella_media      # ColorTab / SwatchTab / LayersDock / PropertiesDock / ChannelPathPanel / AdjustmentPanel
)

set(CMAKE_AUTOMOC ON)
