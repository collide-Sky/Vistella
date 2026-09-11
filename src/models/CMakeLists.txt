# models: Model/View 数据层
add_library(vistella_models STATIC
    recentlistmodel.cpp
    recentlistmodel.h
)
target_include_directories(vistella_models PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_models
    PUBLIC  Qt6::Core
    PRIVATE vistella_core
)
