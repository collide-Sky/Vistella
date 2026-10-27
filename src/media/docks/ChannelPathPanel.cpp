// SPDX-License-Identifier: MIT
//
// ChannelPathPanel implementation
//
#include "ChannelPathPanel.h"
#include "ui_ChannelPathPanel.h"
#include "logger.h"

#include <QCoreApplication>
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
    // wire selection action buttons
    if (ui->saveSelBtn) {
        connect(ui->saveSelBtn, &QPushButton::clicked,
                this, &ChannelPathPanel::onSaveSelectionClicked);
    }
    if (ui->loadSelBtn) {
        connect(ui->loadSelBtn, &QPushButton::clicked,
                this, &ChannelPathPanel::onLoadSelectionClicked);
    }
    if (ui->quickMaskBtn) {
        connect(ui->quickMaskBtn, &QPushButton::toggled,
                this, &ChannelPathPanel::onQuickMaskClicked);
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

// Selection action handlers — emit signals for ImageWindow
//   to route. Selection state itself stays in SelectionModel; this panel
//   only surfaces the trigger.
void ChannelPathPanel::onSaveSelectionClicked()
{
    LOG_INFO("[ChannelPathPanel] saveSelectionRequested");
    emit saveSelectionRequested();
}

void ChannelPathPanel::onLoadSelectionClicked()
{
    LOG_INFO("[ChannelPathPanel] loadSelectionRequested");
    emit loadSelectionRequested();
}

void ChannelPathPanel::onQuickMaskClicked(bool on)
{
    LOG_INFO("[ChannelPathPanel] quickMaskToggled: {}", on);
    emit quickMaskToggled(on);
}

void ChannelPathPanel::setQuickMask(bool on)
{
    if (!ui || !ui->quickMaskBtn) return;
    // Block signals to avoid echo when AppBar drives the toggle and vice-versa.
    const bool wasBlocked = ui->quickMaskBtn->blockSignals(true);
    ui->quickMaskBtn->setChecked(on);
    ui->quickMaskBtn->blockSignals(wasBlocked);
}

void ChannelPathPanel::setupChannels()
{
    if (!ui || !ui->channelList) return;
    // PS standard channel list (composite + 3 color + alpha + quickmask)
    // all channel names go through QCoreApplication::translate
    //   (baselines are English, but Chinese localization will replace at runtime)
    struct ChannelEntry { const char* tag; const char* fallback; ChannelKind k; };
    static const ChannelEntry kChannels[] = {
        { "RGB",         "RGB",            ChannelKind::Composite },
        { "Red",         "Red",            ChannelKind::Color     },
        { "Green",       "Green",          ChannelKind::Color     },
        { "Blue",        "Blue",           ChannelKind::Color     },
        { "Alpha",       "Alpha",          ChannelKind::Alpha     },
        { "QuickMask",   "Quick Mask",     ChannelKind::QuickMask },
    };
    for (const auto& entry : kChannels) {
        QString label = QCoreApplication::translate("docks::ChannelPathPanel", entry.fallback);
        auto* item = new QListWidgetItem(label, ui->channelList);
        // Store original tag for setSelectedChannel lookup
        item->setData(Qt::UserRole + 1, QString::fromUtf8(entry.tag));
        item->setData(Qt::UserRole, static_cast<int>(entry.k));
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
    auto* workPath = new QListWidgetItem(
        QCoreApplication::translate("docks::ChannelPathPanel", "Work Path"),
        ui->pathList);
    workPath->setData(Qt::UserRole + 1, QStringLiteral("WorkPath"));
    (void)workPath;   // reserved for future role data
    // Default selection: work path
    if (ui->pathList->count() > 0) {
        ui->pathList->setCurrentRow(0);
    }
}

} // namespace docks
