// SPDX-License-Identifier: MIT
//
// MagicWand implementation - F-L (2026-09-10) + P0-4.5 (2026-09-10)
//
#include "MagicWand.h"
#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "../selection/SelectionStrategy.h"
#include "logger.h"

#include <QCursor>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

MagicWand::MagicWand(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::MagicWandSelectionStrategy>();
}

MagicWand::~MagicWand() = default;

void MagicWand::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[MagicWand] onEnter");
}

void MagicWand::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    LOG_DEBUG("[MagicWand] onExit");
}

void MagicWand::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->begin(scenePos);
    LOG_DEBUG("[MagicWand] onMousePress: scene=({}, {})", scenePos.x(), scenePos.y());
}

void MagicWand::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->update(scenePos);
}

void MagicWand::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strategy || !host) return;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return;
    auto* sel = host->selectionModel();
    if (!sel) return;
    auto mode = selection::SelectionModel::modifierKeyToMode(e->modifiers());
    sel->setMask(mask, mode);
    LOG_INFO("[MagicWand] onMouseRelease: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
}

mediators::ToolId MagicWand::id() const { return mediators::ToolId::MagicWand; }

QString MagicWand::pageTitle() const { return QStringLiteral("MagicWand"); }

QCursor MagicWand::cursor() const
{
    // F-N (2026-09-10): PS MagicWand uses crosshair
    return Qt::CrossCursor;
}

QWidget* MagicWand::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(QStringLiteral("MagicWand (W) options (F-L stub)"), page);
    layout->addWidget(label);
    layout->addStretch(1);
    LOG_DEBUG("[MagicWand] optionPage created");
    return page;
}

} // namespace tools
