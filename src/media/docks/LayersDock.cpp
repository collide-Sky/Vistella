// SPDX-License-Identifier: MIT
//
// LayersDock implementation - F-G.2 (2026-09-09)
//
#include "LayersDock.h"
#include "logger.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QTabWidget>

namespace docks {

LayersDock::LayersDock(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);
    m_layout->addWidget(m_tabs);

    // Tab 0: 图层 (F-G.3 集成 LayerPanel)
    QWidget* layerTab = new QWidget(this);
    QVBoxLayout* layerLayout = new QVBoxLayout(layerTab);
    layerLayout->setContentsMargins(2, 2, 2, 2);
    m_layerPlaceholder = new QLabel(QStringLiteral("Layer Panel (F-G.3)"), layerTab);
    m_layerPlaceholder->setAlignment(Qt::AlignCenter);
    layerLayout->addWidget(m_layerPlaceholder);
    layerLayout->addStretch(1);
    m_tabs->addTab(layerTab, QStringLiteral("Layers"));

    // Tab 1: 通道/路径 (F-M 阶段实装)
    QWidget* channelTab = new QWidget(this);
    QVBoxLayout* channelLayout = new QVBoxLayout(channelTab);
    channelLayout->setContentsMargins(2, 2, 2, 2);
    m_channelPlaceholder = new QLabel(QStringLiteral("Channels/Paths (F-M)"), channelTab);
    m_channelPlaceholder->setAlignment(Qt::AlignCenter);
    channelLayout->addWidget(m_channelPlaceholder);
    channelLayout->addStretch(1);
    m_tabs->addTab(channelTab, QStringLiteral("Channels/Paths"));
}

void LayersDock::setContentWidget(int tabIndex, QWidget* w)
{
    if (tabIndex < 0 || tabIndex >= m_tabs->count()) {
        LOG_WARN("[LayersDock] setContentWidget tabIndex={} out of range (count={})", tabIndex, m_tabs->count());
        return;
    }
    QWidget* tab = m_tabs->widget(tabIndex);
    if (!tab) return;
    // 清空 tab 旧内容, 装新 widget
    QLayout* oldLayout = tab->layout();
    if (oldLayout) {
        // 删旧 children (除 layout 自身)
        QLayoutItem* item = nullptr;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->setParent(nullptr);
                item->widget()->deleteLater();
            }
            delete item;
        }
    }
    if (w) {
        w->setParent(tab);
        oldLayout->addWidget(w);
    }
    LOG_INFO("[LayersDock] setContentWidget tab={} widget={}", tabIndex, w ? w->metaObject()->className() : "null");
}

} // namespace docks
