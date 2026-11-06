# ui: 窗口类 (含 .ui)
# 集中所有 QWidget 子类, 不放业务逻辑
add_library(vistella_ui STATIC
    # M2-7b: 文件树换成 QFileSystemModel + QSortFilterProxyModel
    #   FileTreeProxyModel  扩展名过滤 + 空目录的展开箭头
    FileTreeProxyModel.h
    FileTreeProxyModel.cpp

    infotreedock.h
    infotreedock.cpp
    infotreedock.ui

    homepage.h
    homepage.cpp
    homepage.ui

    docwindow.h
    docwindow.cpp
    docwindow.ui

    settingsdialog.h
    settingsdialog.cpp
    settingsdialog.ui

    # P8.3.x (2026-10-19): GPU / 渲染后端 settings panel
    #   3 radio (Auto / Force GPU / Force CPU) + Apply + Probe OpenGL.
    #   Embedded into SettingsDialog as a new stack page.
    settingsgpupanel.h
    settingsgpupanel.cpp

    themegallerydialog.h
    themegallerydialog.cpp
    themegallerydialog.ui

    logindialog.h
    logindialog.cpp
    logindialog.ui
)

target_include_directories(vistella_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_ui
    PUBLIC
        Qt6::Core
        Qt6::Widgets
    PRIVATE
        vistella_core
        vistella_models
)

# P7.6.x v3 commit 2 (2026-10-14): PS-style draggable tab + GPU/CPU placeholders.
#   Keep dock as a separate sub-library so commit 3/4 (VTabPageBase + RightDock
#   nested sub-dock) can extend it without churning the rest of vistella_ui.
add_subdirectory(dock)
target_link_libraries(vistella_ui PUBLIC vistella_ui_dock)

# 让 AUTOMOC / AUTOUIC 看到 .ui
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
