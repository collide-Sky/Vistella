// SPDX-License-Identifier: MIT
//
// Brush implementation - F-L (2026-09-10)
//
#include "Brush.h"
#include "logger.h"

#include <QCursor>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

Brush::Brush(QWidget* /*parent*/) : ToolState() {}

void Brush::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Brush] onEnter");
}

void Brush::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Brush] onExit");
}

mediators::ToolId Brush::id() const { return mediators::ToolId::Brush; }

QString Brush::pageTitle() const { return QStringLiteral("Brush"); }

QCursor Brush::cursor() const
{
    // F-N (2026-09-10): PS Brush uses circle cursor (custom 24x24 QBitmap; crosshair as fallback)
    return Qt::CrossCursor;
}

QWidget* Brush::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(QStringLiteral("Brush (B) options (F-L stub)"), page);
    layout->addWidget(label);
    layout->addStretch(1);
    LOG_DEBUG("[Brush] optionPage created");
    return page;
}

} // namespace tools
