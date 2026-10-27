// SPDX-License-Identifier: MIT
//
// ToolMediator 实现
//
#include "ToolMediator.h"
#include "logger.h"

#include <QHash>
#include <QVariantMap>

namespace mediators {

ToolMediator::ToolMediator(QObject* parent) : QObject(parent)
{
    // F-B1 初始化为空
    //   F-C 阶段 ToolState 实装时, 会从 QSettings load 上次配置
}

QVariantMap ToolMediator::currentConfig(ToolId id) const
{
    return m_configs.value(id);
}

void ToolMediator::switchTool(ToolId id)
{
    // F-B1 简单版: 同 id 不重复 emit
    if (m_current == id) return;
    LOG_INFO("[ToolMed] switchTool: {} -> {}", static_cast<int>(m_current), static_cast<int>(id));
    m_current = id;
    emit toolSwitched(id);
}

// TODO(P8.5): the tool-config APIs have no caller, so per-tool settings reset on every tool switch -- see docs/DEBT.md#A-TOOLCONFIG
void ToolMediator::applyToolConfig(ToolId id, const QVariantMap& cfg)
{
    LOG_DEBUG("[ToolMed] applyToolConfig: id={} keys={}", static_cast<int>(id), cfg.size());
    m_configs[id] = cfg;
    emit toolConfigApplied(id, cfg);
}

void ToolMediator::resetToolConfig(ToolId id)
{
    LOG_DEBUG("[ToolMed] resetToolConfig: id={}", static_cast<int>(id));
    m_configs.remove(id);
    emit toolConfigReset(id);
}

} // namespace mediators
