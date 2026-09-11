#ifndef IAMODEL_H
#define IAMODEL_H

// =============================================================
// IAIModel — AI 模型统一接口 (阶段 0 第 3 步)
//
// 设计原则:
//   - 后端无关 (阶段 0 写 ONNX Runtime, 后续可换 TensorRT / OpenVINO / 自研)
//   - 不依赖具体模型 (YOLOv8 / SAM / ESRGAN 都实现这个接口)
//   - 同步推理 (小任务) + 异步推理 (大任务) 两种模式
//   - 异步用 signal 返回, 不阻塞 UI 线程
//
// 注意: src/ai/ 不依赖 src/image/src/audio/..., 任何 Worker 都能 include
//   QVariant 作为通用输入输出容器, 阶段 4 需要为 cv::Mat 等自定义类型
//   调 Q_DECLARE_METATYPE + qRegisterMetaType 才能塞进 signal 参数
// =============================================================

#include <QObject>
#include <QString>
#include <QVariant>

class IAIModel : public QObject
{
    Q_OBJECT
public:
    enum Status {
        NotLoaded,     // 模型未加载
        Loading,       // 正在加载到内存/GPU
        Ready,         // 加载完成, 可推理
        Inferring,     // 正在推理
        Error          // 出错, 看 lastError()
    };
    Q_ENUM(Status)

    virtual ~IAIModel() = default;

    // ----- 模型标识 -----
    virtual QString modelId() const = 0;        // 全局唯一, e.g. "yolov8n"
    virtual QString displayName() const = 0;     // UI 显示, e.g. "YOLOv8 Nano"
    virtual QString modelType() const = 0;      // "detection" / "segmentation" / "super_resolution" / "denoise"
    virtual QString deviceType() const = 0;     // "cpu" / "cuda" / "directml"

    // ----- 模型文件路径 (.onnx / .pt / .engine 等) -----
    virtual QString modelPath() const = 0;
    virtual void setModelPath(const QString &path) = 0;

    // ----- 状态 -----
    virtual Status status() const = 0;
    virtual bool isLoaded() const { return status() == Ready || status() == Inferring; }
    virtual QString lastError() const = 0;

    // ----- 同步推理 (小任务, 立即返回结果) -----
    //   返回 QVariant 包装的结果 (输出格式由具体子类定义)
    //   失败返回无效 QVariant, 调 lastError() 看原因
    virtual QVariant inferSync(const QVariant &input) = 0;

    // ----- 异步推理 (大任务, 完成后通过 inferFinished 信号返回) -----
    //   返回 requestId, 后续信号都带这个 id 用于匹配请求
    //   失败立即 emit inferFailed
    virtual QString submitAsync(const QVariant &input) = 0;

    // 取消某个请求 (如果还没开始)
    virtual void cancel(const QString &requestId) = 0;

    // 显式加载/卸载 (默认 submitAsync 时按需加载)
    virtual bool load() = 0;
    virtual void unload() = 0;

signals:
    // 状态变化
    void statusChanged(IAIModel::Status status);
    void modelLoaded();
    void modelLoadFailed(const QString &reason);
    void modelUnloaded();

    // 异步推理生命周期
    void inferStarted(const QString &requestId);
    void inferProgress(const QString &requestId, int percent);   // 0-100
    void inferFinished(const QString &requestId, const QVariant &output);
    void inferFailed(const QString &requestId, const QString &reason);
};

#endif // IAMODEL_H
