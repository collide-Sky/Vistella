#ifndef AIMANAGER_H
#define AIMANAGER_H

// =============================================================
// AIManager — AI 模型管理器 (阶段 0 第 3 步)
//
// 职责:
//   1. 注册 / 注销所有 IAIModel 实例 (Worker 启动时注册, 关闭时注销)
//   2. 按 modelId 路由推理请求
//   3. 后端选择 (阶段 0 写死 "onnxruntime", 后续可换, IAIModel 内部不感知)
//
// 跟 MediaDispatcher 的关系:
//   MediaDispatcher 路由"打开文件" → 对应 IModule (Worker)
//   Worker 内部用 AIManager 调 AI 模型
//   两个都是单例, 各管一摊
// =============================================================

#include "IAIModel.h"

#include <QObject>
#include <QList>
#include <QHash>
#include <QString>

class AIManager : public QObject
{
    Q_OBJECT
public:
    static AIManager &instance();

    // ----- 注册/注销 (Worker 启动/关闭时调) -----
    void registerModel(IAIModel *m);
    void unregisterModel(IAIModel *m);
    void unregisterAll();

    // ----- 查询 -----
    QList<IAIModel *> models() const { return m_models; }
    IAIModel *model(const QString &id) const;
    int modelCount() const { return m_models.size(); }

    // ----- 后端 (阶段 0 写死 ONNX Runtime, 后续可换 TensorRT / OpenVINO) -----
    QString backendName() const { return m_backendName; }
    void setBackendName(const QString &name);

    // ----- 模型路径解析 -----
    //   阶段 0: 简单拼接 (config/models/<id>.onnx)
    //   阶段 4+: 接 SettingsDialog 配置, 支持多模型目录
    QString resolveModelPath(const QString &modelId) const;

    // ----- 便利: 按 id 提交异步推理 -----
    //   找不到模型返回空 string
    QString submitInference(const QString &modelId, const QVariant &input);

signals:
    void modelsChanged();
    void backendChanged(const QString &name);

private:
    explicit AIManager(QObject *parent = nullptr);
    AIManager(const AIManager &) = delete;
    AIManager &operator=(const AIManager &) = delete;

    QList<IAIModel *> m_models;
    QHash<QString, IAIModel *> m_idMap;
    QString m_backendName = QStringLiteral("onnxruntime");
};

#endif // AIMANAGER_H
