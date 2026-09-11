// SPDX-License-Identifier: MIT
//
// ChannelPathPanel implementation - F-M.3 (2026-09-10)
//
#include "ChannelPathPanel.h"
#include "ui_ChannelPathPanel.h"
#include "logger.h"

#include <QListWidget>
#include <QListWidgetItem>

namespace docks {

ChannelPathPanel::ChannelPathPanel(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::ChannelPathPanel;
    ui->setupUi(this);

    setupChannels();
    setupPaths();

    // Forward QListWidget selection -> our signals
    if (ui->channelList) {
        connect(ui->channelList, &QListWidget::itemSelectionChanged,
                this, [this]() {
            auto* item = ui->channelList->currentItem();
            QString name = item ? item->text() : QString();
            LOG_DEBUG("[ChannelPathPanel] channelSelected: {}", name.toStdString());
            emit channelSelected(name);
        });
    }
    if (ui->pathList) {
        connect(ui->pathList, &QListWidget::itemSelectionChanged,
                this, [this]() {
            auto* item = ui->pathList->currentItem();
            QString name = item ? item->text() : QString();
            LOG_DEBUG("[ChannelPathPanel] pathSelected: {}", name.toStdString());
            emit pathSelected(name);
        });
    }
}

ChannelPathPanel::~ChannelPathPanel()
{
    delete ui;
}

QString ChannelPathPanel::selectedChannel() const
{
    if (!ui || !ui->channelList) return QString();
    auto* item = ui->channelList->currentItem();
    return item ? item->text() : QString();
}

QString ChannelPathPanel::selectedPath() const
{
    if (!ui || !ui->pathList) return QString();
    auto* item = ui->pathList->currentItem();
    return item ? item->text() : QString();
}

void ChannelPathPanel::setSelectedChannel(const QString& name)
{
    if (!ui || !ui->channelList) return;
    for (int i = 0; i < ui->channelList->count(); ++i) {
        auto* item = ui->channelList->item(i);
        if (item && item->text() == name) {
            ui->channelList->setCurrentItem(item);
            return;
        }
    }
}

void ChannelPathPanel::setSelectedPath(const QString& name)
{
    if (!ui || !ui->pathList) return;
    for (int i = 0; i < ui->pathList->count(); ++i) {
        auto* item = ui->pathList->item(i);
        if (item && item->text() == name) {
            ui->pathList->setCurrentItem(item);
            return;
        }
    }
}

void ChannelPathPanel::setupChannels()
{
    if (!ui || !ui->channelList) return;
    // PS standard channel list (composite + 3 color + alpha + quickmask)
    static const char* kChannels[] = {
        "RGB",          // 0
        "Red",          // 1
        "Green",        // 2
        "Blue",         // 3
        "Alpha",        // 4
        "快速蒙版",     // 5
    };
    for (const char* name : kChannels) {
        auto* item = new QListWidgetItem(QString::fromUtf8(name), ui->channelList);
        // Tag kind for future API (P0)
        if (qstrcmp(name, "RGB") == 0) {
            item->setData(Qt::UserRole, static_cast<int>(ChannelKind::Composite));
        } else if (qstrcmp(name, "Alpha") == 0) {
            item->setData(Qt::UserRole, static_cast<int>(ChannelKind::Alpha));
        } else if (qstrcmp(name, "快速蒙版") == 0) {
            item->setData(Qt::UserRole, static_cast<int>(ChannelKind::QuickMask));
        } else {
            item->setData(Qt::UserRole, static_cast<int>(ChannelKind::Color));
        }
    }
    // Default selection: RGB
    if (ui->channelList->count() > 0) {
        ui->channelList->setCurrentRow(0);
    }
}

void ChannelPathPanel::setupPaths()
{
    if (!ui || !ui->pathList) return;
    // PS standard path list
    auto* workPath = new QListWidgetItem(QString::fromUtf8("工作路径"), ui->pathList);
    (void)workPath;   // reserved for future role data
    // Default selection: 工作路径
    if (ui->pathList->count() > 0) {
        ui->pathList->setCurrentRow(0);
    }
}

} // namespace docks
