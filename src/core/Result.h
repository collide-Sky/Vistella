#ifndef vistella_RESULT_H
#define vistella_RESULT_H

// =============================================================
// Result<T> — 统一错误处理基础设施 (阶段 1 前置优化, 2026-09-03)
//
// 设计动机:
//   阶段 0 错误处理是分散的:
//     - 失败返 nullptr (e.g. MediaDispatcher::openFile)
//     - 失败 emit openFailed(path, reason) 信号
//     - 失败返 bool (e.g. IWorkspace::save)
//     - 失败 throw exception (main.cpp try/catch 兜底)
//
//   阶段 1+ worker (imageWorker / audioWorker / ...) 大量计算函数, 每步都失败:
//     - 文件 IO 失败
//     - 解码失败
//     - 内存不足
//     - 滤镜参数越界
//     - AI 模型加载失败
//
//   用 Result<T> 统一:
//     1. 调用方一眼看出"可能失败" (签名带 Result<T> 不是 bool)
//     2. 错误信息 (ErrorCode + 描述) 走类型系统, 不丢失
//     3. 不需要 throw, 性能可预测 (符合 Qt / 多媒体行业惯例, 不走 C++ exception)
//
// API 形式 (Rust 风格但适配 C++14/17):
//     auto r = worker->loadFile(path);
//     if (r.isErr()) {
//         qWarning() << r.error().code << r.error().message;
//         return r;  // 透传
//     }
//     cv::Mat img = r.value();
//
//     // 提供 isOk / value / error / valueOr / 隐式 bool (用 ! 测失败)
//
// 使用约束:
//   - header-only, 不需要 moc, 不需要 cpp
//   - 不依赖 Qt (用 std::string + std::move), 任何模块都能 include
//   - ErrorCode 是 enum class, 防止和 int 混
// =============================================================

#include <string>
#include <utility>
#include <type_traits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace vistella {

// 错误码 (阶段 0/1 全部错误来源, 后面阶段可加, 不删)
enum class ErrorCode {
    Ok = 0,                  // 没失败 (Result<T>::ok 用, 一般不直接返回)
    Unknown,                 // 未知错误 (兜底)
    InvalidArgument,         // 参数非法 (e.g. 负数宽度, 空字符串)
    FileNotFound,            // 路径不存在
    FileAccessDenied,        // 没权限
    FileCorrupted,           // 文件损坏 (解码/解析失败)
    UnsupportedFormat,       // 扩展名/格式不支持
    OutOfMemory,             // 内存不足
    Timeout,                 // 超时
    Cancelled,               // 用户取消
    NotImplemented,          // 暂未实现
    NotRegistered,           // 模块没注册 (MediaDispatcher 用)
    InvalidState,            // 状态非法 (e.g. 未初始化就调)
    NetworkError,            // 网络错误 (阶段 5+ AI 助手用)
    ModelNotFound,           // AI 模型文件不存在 (阶段 7+ AI 工具用)
    ModelLoadFailed,         // AI 模型加载失败
    Internal,                // 内部错误 (assert 失败, 不变量违反)
};

// 错误描述 (code + message)
//   message 是人类可读的英文/中文, 适合 log / UI 弹窗
struct Error {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string msg)
        : code(c), message(std::move(msg)) {}

    bool isOk() const { return code == ErrorCode::Ok; }
    bool isErr() const { return !isOk(); }

    // 短描述 (给 log / debug 用): "FILE_NOT_FOUND: foo.png"
    std::string toString() const {
        return std::string(codeName(code)) + ": " + message;
    }

    // 错误码的可读名 (do_case 给上面 toString + log)
    static const char *codeName(ErrorCode c) {
        switch (c) {
            case ErrorCode::Ok:               return "OK";
            case ErrorCode::Unknown:          return "UNKNOWN";
            case ErrorCode::InvalidArgument:  return "INVALID_ARGUMENT";
            case ErrorCode::FileNotFound:     return "FILE_NOT_FOUND";
            case ErrorCode::FileAccessDenied: return "FILE_ACCESS_DENIED";
            case ErrorCode::FileCorrupted:    return "FILE_CORRUPTED";
            case ErrorCode::UnsupportedFormat:return "UNSUPPORTED_FORMAT";
            case ErrorCode::OutOfMemory:      return "OUT_OF_MEMORY";
            case ErrorCode::Timeout:          return "TIMEOUT";
            case ErrorCode::Cancelled:        return "CANCELLED";
            case ErrorCode::NotImplemented:   return "NOT_IMPLEMENTED";
            case ErrorCode::NotRegistered:    return "NOT_REGISTERED";
            case ErrorCode::InvalidState:     return "INVALID_STATE";
            case ErrorCode::NetworkError:     return "NETWORK_ERROR";
            case ErrorCode::ModelNotFound:    return "MODEL_NOT_FOUND";
            case ErrorCode::ModelLoadFailed:  return "MODEL_LOAD_FAILED";
            case ErrorCode::Internal:         return "INTERNAL";
        }
        return "UNKNOWN";
    }
};

// Result<T> — 成功带 T, 失败带 Error
//   T = void 走 Result<void> 特化
template <typename T>
class Result {
public:
    // 构造成功 (隐式, 避免每次写 Result<T>::ok(...))
    //   但 T 应该是 movable 的, 避免无谓 copy
    Result(T value)
        : m_value(std::move(value)), m_error() {}

    // 构造失败
    Result(ErrorCode code, std::string message)
        : m_error(code, std::move(message)) {}

    // 显式失败 (从 Error)
    static Result<T> err(ErrorCode code, std::string message) {
        return Result<T>(code, std::move(message));
    }
    static Result<T> err(const Error &e) {
        Result<T> r(ErrorCode::Ok, std::string());
        r.m_error = e;
        return r;
    }

    bool isOk()  const { return m_error.isOk(); }
    bool isErr() const { return !isOk(); }
    explicit operator bool() const { return isOk(); }

    // 取值 (失败时 std::abort, 调用方应先 isOk 测)
    T &value() & {
        if (isErr()) std::abort();  // 调试器会停, 行为比 UB 友好
        return m_value;
    }
    const T &value() const & {
        if (isErr()) std::abort();
        return m_value;
    }
    T &&value() && {
        if (isErr()) std::abort();
        return std::move(m_value);
    }

    // 失败时返默认值
    T valueOr(T defaultValue) const & {
        return isOk() ? m_value : std::move(defaultValue);
    }
    T valueOr(T defaultValue) && {
        return isOk() ? std::move(m_value) : std::move(defaultValue);
    }

    const Error &error() const { return m_error; }

private:
    T      m_value;
    Error  m_error;
};

// void 特化 — Result<void> 表示"操作成功/失败, 不带值"
template <>
class Result<void> {
public:
    Result() : m_error() {}  // 默认成功
    Result(ErrorCode code, std::string message)
        : m_error(code, std::move(message)) {}

    static Result<void> ok() { return Result<void>(); }
    static Result<void> err(ErrorCode code, std::string message) {
        return Result<void>(code, std::move(message));
    }
    static Result<void> err(const Error &e) {
        Result<void> r;
        r.m_error = e;
        return r;
    }

    bool isOk()  const { return m_error.isOk(); }
    bool isErr() const { return !isOk(); }
    explicit operator bool() const { return isOk(); }

    const Error &error() const { return m_error; }

private:
    Error m_error;
};

// ---- 错误处理宏 (glue, 减少样板) ----
//
//   Q_TRY(expr)  — expr 是 Result<T>, 失败时 return 同样的 Error (从当前函数)
//                  用法:
//                      Q_TRY(worker->loadFile(path));
//                      // 上面这行成功才往下走
//                      cv::Mat img = worker->getImage();
//
//   Q_TRY_VAL(expr, target) — expr 是 Result<T>, 失败时赋值给 target 并 return
//                  target 必须是 Result<???> 类型
//                  用法:
//                      Result<cv::Mat> image = Q_TRY_VAL(worker->loadFile(path), ret);
//
//   注意: 宏依赖当前函数 return 类型能接收 Result<...>, 跨函数透传很方便
//
#define Q_TRY(expr) \
    do { \
        auto _r = (expr); \
        if (_r.isErr()) return _r.error(); \
    } while (0)

#define Q_TRY_VAL(expr, target) \
    ({ \
        auto _r = (expr); \
        if (_r.isErr()) { target = _r.error(); } \
        std::move(_r); \
    })

// 把 std::string 拼成 Error 的小工具
//   Err(ErrorCode::FileNotFound, "file not found: " + path) 太长, 用:
//     ErrF(ErrorCode::FileNotFound, "file not found: %s", path.c_str())
//   MSVC 不支持 __attribute__((format)), 用 ifdef 区分 GCC/Clang/MSVC
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
inline Error ErrF(ErrorCode code, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return Error(code, std::string(buf));
}

} // namespace vistella

#endif // vistella_RESULT_H
