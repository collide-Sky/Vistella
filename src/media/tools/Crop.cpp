// SPDX-License-Identifier: MIT
//
// Crop implementation - F-L (2026-09-10)
//
#include "Crop.h"
#include "logger.h"

#include <QCursor>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

Crop::Crop(QWidget* /*parent*/) : ToolState() {}

void Crop::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Crop] onEnter");
}

void Crop::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Crop] onExit");
}

mediators::ToolId Crop::id() const { return mediators::ToolId::Crop; }

QString Crop::pageTitle() const { return QStringLiteral("Crop"); }

QCursor Crop::cursor() const
{
    // F-N (2026-09-10): PS Crop uses crosshair (crop frame icon reserved)
    return Qt::CrossCursor;
}

QWidget* Crop::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(QStringLiteral("Crop (C) options (F-L stub)"), page);
    layout->addWidget(label);
    layout->addStretch(1);
    LOG_DEBUG("[Crop] optionPage created");
    return page;
}

} // namespace tools
