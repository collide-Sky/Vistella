# ui: 窗口类 (含 .ui)
# 集中所有 QWidget 子类, 不放业务逻辑
add_library(vistella_ui STATIC
    # 决策 5 重构 (2026-09-02): 文件树 Model/View
    #   FileTreeNodeData 纯 header POD
    #   FileTreeItem      树节点 + 父子关系 + lazy load
    #   FileTreeModel     QAbstractItemModel + 扩展名过滤
    FileTreeNodeData.h
    FileTreeItem.h
    FileTreeItem.cpp
    FileTreeModel.h
    FileTreeModel.cpp

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

# 让 AUTOMOC / AUTOUIC 看到 .ui
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
