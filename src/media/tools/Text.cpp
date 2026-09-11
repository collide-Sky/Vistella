// SPDX-License-Identifier: MIT
//
// Text implementation - F-L (2026-09-10)
//
#include "Text.h"
#include "logger.h"

#include <QCursor>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

Text::Text(QWidget* /*parent*/) : ToolState() {}

void Text::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Text] onEnter");
}

void Text::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Text] onExit");
}

mediators::ToolId Text::id() const { return mediators::ToolId::Text; }

QString Text::pageTitle() const { return QStringLiteral("Text"); }

QCursor Text::cursor() const
{
    // F-N (2026-09-10): Text tool uses I-beam cursor (text insertion)
    return Qt::IBeamCursor;
}

QWidget* Text::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(QStringLiteral("Text (T) options (F-L stub)"), page);
    layout->addWidget(label);
    layout->addStretch(1);
    LOG_DEBUG("[Text] optionPage created");
    return page;
}

} // namespace tools
