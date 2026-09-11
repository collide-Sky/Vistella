# network: 网络层 (2026-09-02 阶段 0 第 9 步新增)
#   AuthClient  - 第三方登录 (Local/GitHub/QQ/WeChat/Phone) OAuth 入口
#   HttpClient  - 通用 HTTP/HTTPS 客户端 (阶段 5+ 用 QNetworkAccessManager 实现)
#   阶段 0: 接口 + 单例, 不发真请求, 全部走 stub
#   阶段 5+ 真正实现: HTTPS / Bearer Token / 自定义 CA / 超时 / 重试
#   横向服务, image/audio/video/vision Worker 都通过 HttpClient 拿数据
add_library(vistella_network STATIC
    AuthClient.cpp
    AuthClient.h
    HttpClient.cpp
    HttpClient.h
)
target_include_directories(vistella_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vistella_network PUBLIC
    Qt6::Core Qt6::Gui
    vistella_logger
)
