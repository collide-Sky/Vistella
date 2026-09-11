#include "AIManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

// =============================================================
// AIManager 单例实现 (阶段 0 占位)
// 阶段 4+ 才会真正实现:
//   - registerModel 真正注册 YOLOv8 / SAM / ESRGAN 等模型
//   - backendName 切换 (ONNX <-> TensorRT <-> OpenVINO)
//   - resolveModelPath 走 SettingsDialog 配置
// 阶段 0 只做骨架, 所有方法空实现, 让上层能编译通过 + 接口定型
// =============================================================

AIManager &AIManager::instance()
{
    static AIManager inst;
    return inst;
}

AIManager::AIManager(QObject *parent)
    : QObject(parent)
{
}

void AIManager::registerModel(IAIModel *m)
{
    if (!m) return;
    const QString id = m->modelId();
    if (id.isEmpty()) {
        qWarning("[AIManager] registerModel: empty id, skip");
        return;
    }
    if (m_idMap.contains(id)) {
        qWarning("[AIManager] duplicate model id '%s', replacing", qPrintable(id));
        IAIModel *old = m_idMap.value(id);
        m_models.removeAll(old);
        // 不主动 delete old, 交给 owner (Worker 关闭时统一释放)
    }
    m_models.append(m);
    m_idMap.insert(id, m);
    emit modelsChanged();
}

void AIManager::unregisterModel(IAIModel *m)
{
    if (!m) return;
    if (!m_models.contains(m)) return;
    m_models.removeAll(m);
    m_idMap.remove(m->modelId());
    emit modelsChanged();
}

void AIManager::unregisterAll()
{
    if (m_models.isEmpty()) return;
    m_models.clear();
    m_idMap.clear();
    emit modelsChanged();
}

IAIModel *AIManager::model(const QString &id) const
{
    return m_idMap.value(id);
}

void AIManager::setBackendName(const QString &name)
{
    if (name == m_backendName) return;
    m_backendName = name;
    emit backendChanged(name);
}

QString AIManager::resolveModelPath(const QString &modelId) const
{
    // 阶段 0 占位: 拼 <AppLocalData>/models/<id>.onnx (跨平台, Qt 6 标准)
    // 阶段 4+: 走 SettingsDialog 配置, 支持多目录 / 多设备 / 用户自定义
    //   用 AppLocalDataLocation (非漫游) 跟 logs/data 统一, 都在 %LOCALAPPDATA% 下
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + QStringLiteral("/models");
    QDir().mkpath(dir);  // 确保目录存在
    return QDir(dir).filePath(modelId + QStringLiteral(".onnx"));
}

QString AIManager::submitInference(const QString &modelId, const QVariant &input)
{
    IAIModel *m = model(modelId);
    if (!m) {
        qWarning("[AIManager] submitInference: model '%s' not registered", qPrintable(modelId));
        return QString();
    }
    return m->submitAsync(input);
}
