// =============================================================================
//  Logger.h - Vistella 独立日志模块 (动态库导出头文件)
//
//  架构:
//    - 内部用 spdlog 1.15 (header-only, fmt bundle) + 自定义 sink
//    - 公开两套 API: Qt 版 (QString, 可选) + std C++ 版 (const char* / std::string)
//    - 业务线程只 push 到 spdlog async queue, 不阻塞
//    - 同一进程内多线程安全; 跨进程不保证
//
//  切分规则:
//    1. 按大小: 默认 4MB; 写之前先估算
//       - 本条 < 剩余空间一半 -> 先开新文件, 再写
//       - 本条 >= 剩余空间一半 -> 直接写, 写完再开新文件
//    2. 按日期: 跨日时, 当前条写完再开新一天的文件
//    3. 文件名: "模块名-YYYY-MM-DD.log" / "模块名-YYYY-MM-DD_N.log"
//    4. 跨日 bug 兜底: 每次新开文件写完后 stat 验证, 0 字节就重写
//
//  链接方式:
//    - 业务项目 link vistella_logger.dll (Windows) / libvistella_logger.so (Linux)
//    - runtime 需要 Qt6Core.dll (Windows, 因为 link 了 Qt6::Core)
//    - 头文件路径: src/logger/Logger.h
//    - CMake: target_link_libraries(your_app PRIVATE vistella_logger)
//
//  使用示例:
//    // 1. 启动时 init (std C++ 版)
//    vistella::Logger::init("MyApp", "./logs", LogLevel::Debug, 4*1024*1024);
//
//    // 2. 业务日志 (std C++ 版, 宏自动加 __FILE__/__LINE__)
//    LOG_INFO("加载文件: {}", filePath);    // fmt 风格 format
//    LOG_ERROR("打开文件失败: {}", path);
//
//    // 3. 运行时改配置 (设置面板里用)
//    vistella::Logger::setLogDir("D:/new_logs");
//    vistella::Logger::setLevel(LogLevel::Warn);
//
//    // 4. Qt 项目里也可以用 (在 CMake 里定义 LOGGER_ENABLE_QT_INTEGRATION)
//    vistella::initQt("MyApp", "./logs", LogLevel::Debug);
//    LOG_INFO_QT(QString("加载文件: %1").arg(filePath));
// =============================================================================

#pragma once

#include <string>
#include <cstdarg>
#include <cstddef>
#include <vector>

// spdlog 1.15 把 fmt 重新导出 (因为内置 fmt bundle)
#include <spdlog/spdlog.h>

// =============================================================================
//  动态库导出宏
// =============================================================================
#if defined(_WIN32) || defined(_WIN64)
    #ifdef vistella_logger_EXPORTS
        #define LOGGER_API __declspec(dllexport)
    #else
        #define LOGGER_API __declspec(dllimport)
    #endif
#else
    #define LOGGER_API __attribute__((visibility("default")))
#endif

// =============================================================================
//  日志级别 (与 spdlog 兼容, 数值一致)
// =============================================================================
namespace vistella {
enum class LogLevel : int {
    Trace    = 0,
    Debug    = 1,
    Info     = 2,
    Warn     = 3,
    Error    = 4,
    Critical = 5,
    Off      = 6
};

// =============================================================================
//  业务日志宏 (推荐, 自动加 __FILE__/__LINE__)
//  宏直接展开成 vistella::log(...) / vistella::logF(...), 比类成员更轻
// =============================================================================
#define LOG_TRACE(...)    ::vistella::log(::vistella::LogLevel::Trace,    __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...)    ::vistella::log(::vistella::LogLevel::Debug,    __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)     ::vistella::log(::vistella::LogLevel::Info,     __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)     ::vistella::log(::vistella::LogLevel::Warn,     __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)    ::vistella::log(::vistella::LogLevel::Error,    __FILE__, __LINE__, __VA_ARGS__)
#define LOG_CRITICAL(...) ::vistella::log(::vistella::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)

// 异常处理宏
#define LOG_EXCEPTION(level, ctx, ex)  ::vistella::logException(level, ctx, ex)

// =============================================================================
//  主接口: Logger 类 (静态方法, 不需要实例化)
// =============================================================================
class LOGGER_API Logger {
public:
    // -------- 初始化 --------
    // moduleName:  文件名第一段, 例如 "MyApp"
    // logDir:      日志目录, 留空用默认 (./logs/ 相对 exe)
    // level:       初始级别, 默认 Debug
    // maxFileSize: 单文件上限, 默认 4MB
    // 返回 false 表示初始化失败 (例如目录无法创建)
    static bool init(const std::string& moduleName = "app",
                     const std::string& logDir = "",
                     LogLevel level = LogLevel::Debug,
                     std::size_t maxFileSize = 4 * 1024 * 1024);

    // -------- 运行时修改 (设置面板里用) --------
    static void setLevel(LogLevel level);
    static bool setLevelByName(const std::string& name);
    static bool setLogDir(const std::string& newLogDir);
    static bool setMaxFileSize(std::size_t bytes);
    static bool setModuleName(const std::string& name);

    // -------- 控制 --------
    static void flush();   // 强制刷盘 (业务线程同步等 ~600ms)
    static void shutdown();

    // -------- 状态查询 --------
    static std::string currentLogDir();
    static std::string currentModuleName();
    static std::size_t currentMaxFileSize();
    static LogLevel    currentLevel();
    static std::string currentLevelName();

    // 今天的日志文件列表 (按时间排序, 绝对路径)
    static std::vector<std::string> logFilesToday();

    // 诊断: 累计 sink_it_ 调用次数 (监控日志写入量)
    static long long sinkCallCount();

    // 内部: 取 spdlog::logger 指针 (给模板 logImpl 用, 业务代码一般不调)
    static std::shared_ptr<spdlog::logger> get();
};

// =============================================================================
//  自由函数 (给宏 LOG_INFO(...) 调用, 不带类前缀更短)
//  模板版: fmt 风格, 编译期校验参数类型
// =============================================================================
namespace detail {
// 编译期校验 fmt 参数类型 (在 .cpp 里实现, 跨 dll 用)
// 实际就是把 fmt::format_string 解析后的 format_args 转给 spdlog
LOGGER_API void logImpl(LogLevel level, const char* file, int line,
                        fmt::string_view fmt, fmt::format_args args);
}  // namespace detail

// 模板版: fmt 风格, {} 占位 (推荐用这个, 编译期校验类型)
// 模板必须 inline 在头文件 (避免跨 dll 实例化)
template <typename... Args>
inline void log(LogLevel level, const char* file, int line,
                fmt::format_string<Args...> fmt, Args&&... args) {
    auto lg = Logger::get();
    if (!lg) return;
    const spdlog::level::level_enum spdLvl = [] (LogLevel l) {
        switch (l) {
            case LogLevel::Trace:    return spdlog::level::trace;
            case LogLevel::Debug:    return spdlog::level::debug;
            case LogLevel::Info:     return spdlog::level::info;
            case LogLevel::Warn:     return spdlog::level::warn;
            case LogLevel::Error:    return spdlog::level::err;
            case LogLevel::Critical: return spdlog::level::critical;
            case LogLevel::Off:      return spdlog::level::off;
            default:                 return spdlog::level::info;
        }
    }(level);
    if (!lg->should_log(spdLvl)) return;
    // 调 spdlog 的 log overload (编译期 fmt 校验, 类型安全)
    lg->log(spdlog::source_loc{file, line, ""}, spdLvl, fmt, std::forward<Args>(args)...);
}

// 简化版 (不带 file/line)
LOGGER_API void trace(const std::string& msg);
LOGGER_API void debug(const std::string& msg);
LOGGER_API void info(const std::string& msg);
LOGGER_API void warn(const std::string& msg);
LOGGER_API void error(const std::string& msg);
LOGGER_API void critical(const std::string& msg);

// 异常处理便利 (catch 块里直接用, 自动记录异常类型 + message)
LOGGER_API void logException(LogLevel level, const char* context, const std::exception& e);

}  // namespace vistella

// =============================================================================
//  Qt 集成 (可选, 在 CMake 里定义 LOGGER_ENABLE_QT_INTEGRATION 才会编译)
// =============================================================================
#ifdef LOGGER_ENABLE_QT_INTEGRATION
#include <QString>
#include <QStringList>

namespace vistella {

// Qt 版 init
LOGGER_API bool initQt(const QString& moduleName,
                        const QString& logDir = QString(),
                        LogLevel level = LogLevel::Debug,
                        std::size_t maxFileSize = 4 * 1024 * 1024);

// Qt 版运行时修改
LOGGER_API void setLevelQt(LogLevel level);
LOGGER_API bool setLogDirQt(const QString& newLogDir);
LOGGER_API bool setModuleNameQt(const QString& name);

// Qt 版状态查询
LOGGER_API QString currentLogDirQt();
LOGGER_API QString currentModuleNameQt();
LOGGER_API QString currentLevelNameQt();
LOGGER_API QStringList logFilesTodayQt();

// Qt 版日志 (中文 QString 直接写, 内部 toUtf8 走 UTF-8 字节流)
LOGGER_API void logQt(LogLevel level, const char* file, int line, const QString& msg);

}  // namespace vistella

// Qt 版宏 (在 Qt 项目里用, 比 std 版省一次 QString->std::string 转换)
#define LOG_TRACE_QT(...)    ::vistella::logQt(::vistella::LogLevel::Trace,    __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG_QT(...)    ::vistella::logQt(::vistella::LogLevel::Debug,    __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO_QT(...)     ::vistella::logQt(::vistella::LogLevel::Info,     __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN_QT(...)     ::vistella::logQt(::vistella::LogLevel::Warn,     __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR_QT(...)    ::vistella::logQt(::vistella::LogLevel::Error,    __FILE__, __LINE__, __VA_ARGS__)
#define LOG_CRITICAL_QT(...) ::vistella::logQt(::vistella::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)

#endif  // LOGGER_ENABLE_QT_INTEGRATION
