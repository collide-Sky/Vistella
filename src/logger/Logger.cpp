// =============================================================================
//  Logger.cpp - Vistella 独立日志模块实现
//
//  架构: spdlog async_logger + 自定义 sink (双维度切分)
//  API: 暴露 std C++ 主接口, Qt 集成在 #ifdef LOGGER_ENABLE_QT_INTEGRATION
// =============================================================================

#include "Logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/async.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <algorithm>
#include <exception>
#include <sys/stat.h>

#ifdef _WIN32
    #include <direct.h>  // _mkdir
    #define MKDIR(p) _mkdir(p)
#else
    #include <sys/stat.h>
    #define MKDIR(p) mkdir(p, 0755)
#endif

// =============================================================================
//  MultiMediaSizeDailySink (双维度切分, 跨日 bug 兜底)
// =============================================================================
namespace vistella {

template <typename Mutex>
class SizeDailySink : public spdlog::sinks::base_sink<Mutex> {
public:
    SizeDailySink(std::string moduleName,
                  std::string logDir,
                  std::size_t maxFileSizeBytes)
        : moduleName_(std::move(moduleName))
        , logDir_(std::move(logDir))
        , maxFileSize_(maxFileSizeBytes < 1024 ? 1024 : maxFileSizeBytes)
    {
        ensureLogDir_();
        openNewFile_();
    }

    ~SizeDailySink() override {
        std::lock_guard<std::mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        flush_();
        closeFile_();
    }

    void updateConfig(const std::string& moduleName,
                      const std::string& logDir,
                      std::size_t maxFileSize) {
        std::lock_guard<std::mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        moduleName_ = moduleName.empty() ? "app" : moduleName;
        logDir_ = logDir;
        maxFileSize_ = std::max<std::size_t>(maxFileSize, 1024);
        ensureLogDir_();
        // 强制下次写时重开文件
        currentFilePath_.clear();
        currentFileSize_ = 0;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        ++sinkCalls_;

        // 0. 先调 base_sink 的 formatter 把 pattern 应用上
        // (spdlog 1.15 改了 base_sink::log 不再自动 format, 参考 basic_file_sink)
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

        // 0.5 暂存 formatted (含 eol), verify 失败时重写
        lastPayload_.assign(formatted.data(), formatted.size());
        lastPayloadWithEol_ = lastPayload_;

        // 1. 跨日检测
        checkDateRollover_();

        // 2. 切分决策
        const std::size_t msgSize = lastPayload_.size();
        const std::size_t remaining = (currentFileSize_ >= maxFileSize_) ? 0 : (maxFileSize_ - currentFileSize_);
        const bool overflows = (currentFileSize_ + msgSize) > maxFileSize_;

        if (overflows) {
            if (msgSize < remaining / 2) {
                openNewFile_();  // 小条 -> 先开新文件再写
            }
            // else: 大条 -> 直接写, 写完再开新文件
        }

        // 3. 写
        writeAndTrack_();

        // 4. 写完后判断是否要开新文件
        if (currentFileSize_ > maxFileSize_) {
            openNewFile_();
        }

        // 5. 跨日 bug 兜底
        verifyAndRewrite_();
    }

    void flush_() override {
        fileHelper_.flush();
    }

public:
    // 诊断接口
    long long sinkCalls() const { return sinkCalls_.load(); }

private:
    // 状态
    std::string                   moduleName_;
    std::string                   logDir_;
    std::size_t                   maxFileSize_;
    std::string                   currentDate_;
    std::string                   currentFilePath_;
    std::size_t                   currentFileSize_ = 0;
    int                           splitIndex_ = 0;
    bool                          justCreated_ = false;
    spdlog::details::file_helper  fileHelper_;

    // 暂存
    std::string                   lastPayload_;
    std::string                   lastPayloadWithEol_;

    mutable std::atomic<long long> sinkCalls_{0};

    static constexpr int kMaxRewriteRetries = 3;

    void ensureLogDir_() {
        if (logDir_.empty()) return;
        // 简单 mkdir (单层; 实际项目通常用 std::filesystem 多层)
        // 这里为避免多带一个 C++17 依赖, 用 platform mkdir + 循环创建父目录
        std::string acc;
        for (char c : logDir_) {
            acc.push_back(c);
            if (c == '/' || c == '\\') {
                if (!acc.empty()) MKDIR(acc.c_str());
            }
        }
        if (!acc.empty()) MKDIR(acc.c_str());
    }

    static std::string todayString_() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return buf;
    }

    std::string computeFileName_(int splitIdx) const {
        if (splitIdx == 0) {
            return moduleName_ + "-" + currentDate_ + ".log";
        }
        return moduleName_ + "-" + currentDate_ + "_" + std::to_string(splitIdx) + ".log";
    }

    std::string currentTargetFile_() const {
        // 简单路径拼接 (假设 logDir 末尾无 /, 内部统一用 /)
        std::string dir = logDir_;
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
        return dir + computeFileName_(splitIndex_);
    }

    int findExistingMaxSplit_() const {
        // 用 spdlog 的 os::directory_iterator 等价物
        // 这里偷懒: 只看 file_helper::size 不能拿 list, 改用 spdlog::details::os::ls_files
        // 简化做法: 用 spdlog 提供的 details::os::directory 遍历
        namespace fs = spdlog::details::os;
        int maxIdx = -1;
        const std::string prefix = moduleName_ + "-" + currentDate_;

        // 第 0 个
        std::string f0 = logDir_;
        if (!f0.empty() && f0.back() != '/' && f0.back() != '\\') f0 += '/';
        f0 += prefix + ".log";
        if (fs::path_exists(f0)) maxIdx = 0;

        // 第 N 个
        for (int n = 1; n < 10000; ++n) {
            std::string fn = logDir_;
            if (!fn.empty() && fn.back() != '/' && fn.back() != '\\') fn += '/';
            fn += prefix + "_" + std::to_string(n) + ".log";
            if (!fs::path_exists(fn)) break;
            maxIdx = n;
        }
        return maxIdx;
    }

    void checkDateRollover_() {
        const std::string today = todayString_();
        if (today != currentDate_) {
            closeFile_();
            currentDate_ = today;
            splitIndex_ = 0;
            openNewFile_();
        }
    }

    void openNewFile_() {
        closeFile_();
        if (currentDate_.empty()) currentDate_ = todayString_();
        const int existingMax = findExistingMaxSplit_();

        // 2026-09-03 修复: 智能切分 (避免每次启动新开文件)
        //   旧逻辑: 总是 splitIndex_ = existingMax + 1 (新开 N+1)
        //   新逻辑: 检查 latest 文件大小
        //     - 未达到 maxFileSize -> 继续追加到 latest (splitIndex_ = existingMax)
        //     - 已达到 maxFileSize -> 新开 N+1 (splitIndex_ = existingMax + 1)
        if (existingMax >= 0) {
            // 本地算 path (currentTargetFile_() 内部用 splitIndex_, 不接受参数)
            splitIndex_ = existingMax;  // 临时设置让 currentTargetFile_ 返回正确路径
            const std::string path = currentTargetFile_();
            const std::size_t size = fileSize_(path);
            if (size < maxFileSize_) {
                // 没达到切分条件, 追加模式打开
                splitIndex_ = existingMax;
                fileHelper_.open(path, false);  // false = 追加, 不 truncate
                currentFilePath_ = path;
                currentFileSize_ = size;
                justCreated_ = false;  // 已有文件, 不需要 verify
                return;
            }
        }

        // 达到切分条件 / 还没有任何文件, 新开
        splitIndex_ = std::max(existingMax + 1, 0);
        const std::string path = currentTargetFile_();
        fileHelper_.open(path, true);
        currentFilePath_ = path;
        currentFileSize_ = 0;
        justCreated_ = true;
    }

    // 取文件大小, 失败返回 0
    static std::size_t fileSize_(const std::string& path) {
        namespace fs = spdlog::details::os;
        if (!fs::path_exists(path)) return 0;
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0) return 0;
        return static_cast<std::size_t>(st.st_size);
    }

    void closeFile_() {
        flush_();
        fileHelper_.close();
        currentFilePath_.clear();
        currentFileSize_ = 0;
    }

    void writeAndTrack_() {
        if (currentFilePath_.empty()) openNewFile_();
        spdlog::memory_buf_t buf;
        buf.append(lastPayloadWithEol_.data(),
                   lastPayloadWithEol_.data() + lastPayloadWithEol_.size());
        fileHelper_.write(buf);
        currentFileSize_ += lastPayloadWithEol_.size();
    }

    void verifyAndRewrite_() {
        if (!justCreated_) return;
        justCreated_ = false;
        if (currentFilePath_.empty() || lastPayloadWithEol_.empty()) return;

        fileHelper_.flush();
        struct stat st {};
        if (::stat(currentFilePath_.c_str(), &st) != 0) return;
        if (st.st_size > 0) return;

        for (int i = 0; i < kMaxRewriteRetries; ++i) {
            fileHelper_.close();
            fileHelper_.open(currentFilePath_, true);
            currentFileSize_ = 0;
            spdlog::memory_buf_t buf;
            buf.append(lastPayloadWithEol_.data(),
                       lastPayloadWithEol_.data() + lastPayloadWithEol_.size());
            fileHelper_.write(buf);
            fileHelper_.flush();
            if (::stat(currentFilePath_.c_str(), &st) == 0 && st.st_size > 0) {
                currentFileSize_ = static_cast<std::size_t>(st.st_size);
                return;
            }
        }
        std::fprintf(stderr, "[Logger] verify failed after %d retries: %s is 0 bytes\n",
                     kMaxRewriteRetries, currentFilePath_.c_str());
    }
};

using SizeDailySinkMt = SizeDailySink<std::mutex>;

// =============================================================================
//  全局状态
// =============================================================================
namespace {

struct LogState {
    std::shared_ptr<spdlog::logger>           logger;
    std::shared_ptr<SizeDailySinkMt>          sink;
    std::string                               moduleName = "app";
    std::string                               logDir;
    std::size_t                               maxFileSize = 4 * 1024 * 1024;
    LogLevel                                  level = LogLevel::Debug;
    std::once_flag                            asyncInitedFlag;
};

LogState& state() {
    static LogState s;
    return s;
}

const char* levelName(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace:    return "trace";
        case LogLevel::Debug:    return "debug";
        case LogLevel::Info:     return "info";
        case LogLevel::Warn:     return "warn";
        case LogLevel::Error:    return "error";
        case LogLevel::Critical: return "critical";
        case LogLevel::Off:      return "off";
        default:                 return "info";
    }
}

LogLevel parseLevelName(const std::string& name) {
    if (name == "trace")             return LogLevel::Trace;
    if (name == "debug")             return LogLevel::Debug;
    if (name == "info")              return LogLevel::Info;
    if (name == "warn" || name == "warning") return LogLevel::Warn;
    if (name == "error" || name == "err")    return LogLevel::Error;
    if (name == "critical")          return LogLevel::Critical;
    if (name == "off")               return LogLevel::Off;
    return LogLevel::Info;
}

spdlog::level::level_enum toSpdlogLevel(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace:    return spdlog::level::trace;
        case LogLevel::Debug:    return spdlog::level::debug;
        case LogLevel::Info:     return spdlog::level::info;
        case LogLevel::Warn:     return spdlog::level::warn;
        case LogLevel::Error:    return spdlog::level::err;
        case LogLevel::Critical: return spdlog::level::critical;
        case LogLevel::Off:      return spdlog::level::off;
        default:                 return spdlog::level::info;
    }
}

std::string defaultLogDir() {
    // 跨平台日志目录 (成熟方案: 环境变量 fallback, 不依赖 Qt, 任何平台都能跑)
    //   Windows: %LOCALAPPDATA%/Vistella/Vistella/logs/  (Vista+)
    //   Linux:   $XDG_DATA_HOME/Vistella/Vistella/logs/  或 $HOME/.local/share/...
    //   macOS:   $HOME/Library/Application Support/Vistella/Vistella/logs/
    //   Fallback: ./logs (相对当前工作目录, 跟以前一样)
    // 注: Logger.cpp 是不依赖 Qt 的独立 SHARED 库, 所以这里用 std::getenv
    //     不用 QStandardPaths (避免把 Qt 拉进 logger DLL)
    std::string dir;
#ifdef _WIN32
    // Windows: %LOCALAPPDATA% (用户级), fallback %APPDATA%
    const char *local = std::getenv("LOCALAPPDATA");
    const char *app   = std::getenv("APPDATA");
    const char *base  = (local && *local) ? local : (app ? app : nullptr);
    if (base) {
        dir = std::string(base) + "\\Vistella\\Vistella\\logs";
    } else {
        dir = "./logs";
    }
#else
    // POSIX (Linux/macOS): $XDG_DATA_HOME (XDG Base Dir 规范)
    const char *xdg = std::getenv("XDG_DATA_HOME");
    const char *home = std::getenv("HOME");
    if (xdg && *xdg) {
        dir = std::string(xdg) + "/Vistella/Vistella/logs";
    } else if (home && *home) {
        dir = std::string(home) + "/.local/share/Vistella/Vistella/logs";
    } else {
        dir = "./logs";
    }
#endif
    return dir;
}

std::shared_ptr<spdlog::logger> buildLogger() {
    auto& s = state();
    auto sink = std::make_shared<SizeDailySinkMt>(s.moduleName, s.logDir, s.maxFileSize);
    s.sink = sink;

    // 重建前等旧 logger 排空
    if (s.logger) {
        s.logger->flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        s.logger.reset();
    }

    // thread pool 只初始化一次
    std::call_once(s.asyncInitedFlag, []() {
        spdlog::init_thread_pool(8192, 1);
        spdlog::flush_every(std::chrono::milliseconds(500));
    });

    auto logger = std::make_shared<spdlog::async_logger>(
        "vistella_logger",
        sink,
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    // 格式: [时间][级别][线程][模块] 内容\n\n
    //   2026-09-03 修: 之前用 %n%n 当 newline — 错!
    //   spdlog pattern 的 %n 是 logger name (这里是 "vistella_logger"), 不是换行符
    //   newline 必须用 literal \n 字符在 pattern 字符串里
    //   末尾 \n\n: 每条日志之间一个空行, 视觉更清晰
    logger->set_pattern(std::string("[%Y-%m-%d %H:%M:%S.%e] [%l] [t:%t] [") +
                        s.moduleName + "] %v\n");
    logger->set_level(toSpdlogLevel(s.level));
    logger->flush_on(spdlog::level::warn);

    return logger;
}

}  // namespace

// =============================================================================
//  Logger 静态方法实现
// =============================================================================
bool Logger::init(const std::string& moduleName,
                  const std::string& logDir,
                  LogLevel level,
                  std::size_t maxFileSize) {
    auto& s = state();
    s.moduleName = moduleName.empty() ? "app" : moduleName;
    s.logDir = logDir;  // 空 = 默认
    s.level = level;
    s.maxFileSize = maxFileSize;

    try {
        s.logger = buildLogger();
    } catch (const spdlog::spdlog_ex& ex) {
        std::fprintf(stderr, "[Logger] init failed: %s\n", ex.what());
        return false;
    }

    s.logger->info("Logger init OK. module={}, dir={}, maxFileSize={} bytes, level={}",
                   s.moduleName, s.logDir, s.maxFileSize, levelName(s.level));
    return true;
}

void Logger::setLevel(LogLevel level) {
    auto& s = state();
    s.level = level;
    if (s.logger) {
        s.logger->set_level(toSpdlogLevel(level));
        s.logger->flush_on(toSpdlogLevel(level) >= spdlog::level::warn
                          ? toSpdlogLevel(level) : spdlog::level::warn);
    }
}

bool Logger::setLevelByName(const std::string& name) {
    setLevel(parseLevelName(name));
    return true;
}

bool Logger::setLogDir(const std::string& newLogDir) {
    auto& s = state();
    if (newLogDir == s.logDir) return true;
    s.logDir = newLogDir;
    try {
        if (s.logger) s.logger->flush();
        s.logger = buildLogger();
        s.logger->info("Log dir changed to {}", s.logDir);
    } catch (const spdlog::spdlog_ex& ex) {
        std::fprintf(stderr, "[Logger] setLogDir failed: %s\n", ex.what());
        return false;
    }
    return true;
}

bool Logger::setMaxFileSize(std::size_t bytes) {
    auto& s = state();
    if (bytes == s.maxFileSize) return true;
    s.maxFileSize = bytes;
    try {
        if (s.logger) s.logger->flush();
        s.logger = buildLogger();
        s.logger->info("Max file size changed to {} bytes", bytes);
    } catch (const spdlog::spdlog_ex& ex) {
        std::fprintf(stderr, "[Logger] setMaxFileSize failed: %s\n", ex.what());
        return false;
    }
    return true;
}

bool Logger::setModuleName(const std::string& name) {
    auto& s = state();
    if (name.empty() || name == s.moduleName) return true;
    s.moduleName = name;
    try {
        if (s.logger) s.logger->flush();
        s.logger = buildLogger();
        s.logger->info("Module name changed to {}", s.moduleName);
    } catch (const spdlog::spdlog_ex& ex) {
        std::fprintf(stderr, "[Logger] setModuleName failed: %s\n", ex.what());
        return false;
    }
    return true;
}

void Logger::flush() {
    auto& s = state();
    if (s.logger) {
        s.logger->flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }
}

void Logger::shutdown() {
    auto& s = state();
    if (s.logger) {
        s.logger->flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        s.logger.reset();
    }
    if (s.sink) s.sink.reset();
    // 不调 spdlog::shutdown() — 见 logger_test 里的教训
}

std::string Logger::currentLogDir()   { return state().logDir; }
std::string Logger::currentModuleName() { return state().moduleName; }
std::size_t Logger::currentMaxFileSize() { return state().maxFileSize; }
LogLevel    Logger::currentLevel()      { return state().level; }
std::string Logger::currentLevelName()  { return levelName(state().level); }

long long Logger::sinkCallCount() {
    auto& s = state();
    return s.sink ? s.sink->sinkCalls() : 0;
}

std::shared_ptr<spdlog::logger> Logger::get() {
    return state().logger;
}

std::vector<std::string> Logger::logFilesToday() {
    auto& s = state();
    std::vector<std::string> result;
    if (s.logDir.empty()) return result;
    namespace fs = spdlog::details::os;
    const std::string today = [](){
        std::time_t t = std::time(nullptr);
        std::tm tm{};
    #ifdef _WIN32
        localtime_s(&tm, &t);
    #else
        localtime_r(&t, &tm);
    #endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return std::string(buf);
    }();
    const std::string prefix = s.moduleName + "-" + today;
    std::string dir = s.logDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

    std::string f0 = dir + prefix + ".log";
    if (fs::path_exists(f0)) result.push_back(f0);

    for (int n = 1; n < 10000; ++n) {
        std::string fn = dir + prefix + "_" + std::to_string(n) + ".log";
        if (!fs::path_exists(fn)) break;
        result.push_back(fn);
    }
    return result;
}

// =============================================================================
//  自由函数 (给宏调用)
// =============================================================================
// 注: log(level, file, line, fmt, args...) 的模板版本在头文件 inline 展开
// 这里只提供 log(string) 重载 (用于 trace/debug/info 等简化版)
void log(LogLevel level, const char* file, int line, const std::string& msg) {
    auto& s = state();
    if (!s.logger) return;
    const spdlog::level::level_enum spdLvl = toSpdlogLevel(level);
    if (!s.logger->should_log(spdLvl)) return;
    s.logger->log(spdlog::source_loc{file, line, ""}, spdLvl, "{}", msg);
}

void trace(const std::string& msg)    { log(LogLevel::Trace,    "", 0, msg); }
void debug(const std::string& msg)    { log(LogLevel::Debug,    "", 0, msg); }
void info(const std::string& msg)     { log(LogLevel::Info,     "", 0, msg); }
void warn(const std::string& msg)     { log(LogLevel::Warn,     "", 0, msg); }
void error(const std::string& msg)    { log(LogLevel::Error,    "", 0, msg); }
void critical(const std::string& msg) { log(LogLevel::Critical, "", 0, msg); }

void logException(LogLevel level, const char* context, const std::exception& e) {
    // 异常统一记录: context + 异常类型名 (typeid) + message
    log(level, "", 0, std::string("[Exception] ") + (context ? context : "unknown")
              + " | type=" + typeid(e).name() + " | what=" + e.what());
}

}  // namespace vistella

// =============================================================================
//  Qt 集成实现
// =============================================================================
#ifdef LOGGER_ENABLE_QT_INTEGRATION

namespace vistella {

bool initQt(const QString& moduleName,
            const QString& logDir,
            LogLevel level,
            std::size_t maxFileSize) {
    return Logger::init(moduleName.toStdString(),
                        logDir.toStdString(),
                        level,
                        maxFileSize);
}

void setLevelQt(LogLevel level) {
    Logger::setLevel(level);
}

bool setLogDirQt(const QString& newLogDir) {
    return Logger::setLogDir(newLogDir.toStdString());
}

bool setModuleNameQt(const QString& name) {
    return Logger::setModuleName(name.toStdString());
}

QString currentLogDirQt() {
    return QString::fromStdString(Logger::currentLogDir());
}

QString currentModuleNameQt() {
    return QString::fromStdString(Logger::currentModuleName());
}

QString currentLevelNameQt() {
    return QString::fromStdString(Logger::currentLevelName());
}

QStringList logFilesTodayQt() {
    QStringList list;
    for (const auto& f : Logger::logFilesToday()) {
        list << QString::fromStdString(f);
    }
    return list;
}

void logQt(LogLevel level, const char* file, int line, const QString& msg) {
    // QString 转 UTF-8 std::string, 避免中文 Windows locale 乱码
    log(level, file, line, msg.toUtf8().constData());
}

}  // namespace vistella

#endif  // LOGGER_ENABLE_QT_INTEGRATION
