# user: 用户管理 (2026-09-02 阶段 0 第 4 步新增)
#   本地 SQLite (UserDatabase) + 状态机 (UserManager)
#   阶段 0 占位: login/registerLocal 是 NoOp + 模拟成功, 走 SQLite 存 user
#   阶段 5+ 接 OAuth 第三方 (QQ/微信/手机号/GitHub) — src/network/AuthClient
add_library(vistella_user STATIC
    UserDatabase.cpp
    UserDatabase.h
    UserManager.cpp
    UserManager.h
    # User.h 纯头文件
)
target_include_directories(vistella_user PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_user PUBLIC
    Qt6::Core Qt6::Gui Qt6::Sql
    vistella_logger
)
