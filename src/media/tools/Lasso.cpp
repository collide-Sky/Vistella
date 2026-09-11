// SPDX-License-Identifier: MIT
//
// Lasso implementation - F-L (2026-09-10) + P0-4.5 (2026-09-10)
//
#include "Lasso.h"
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

Lasso::Lasso(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::LassoSelectionStrategy>();
}

Lasso::~Lasso() = default;

void Lasso::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Lasso] onEnter");
}

void Lasso::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    LOG_DEBUG("[Lasso] onExit");
}

void Lasso::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->begin(scenePos);
    LOG_DEBUG("[Lasso] onMousePress: scene=({}, {})", scenePos.x(), scenePos.y());
}

void Lasso::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->update(scenePos);
}

void Lasso::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strategy || !host) return;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return;
    auto* sel = host->selectionModel();
    if (!sel) return;
    auto mode = selection::SelectionModel::modifierKeyToMode(e->modifiers());
    sel->setMask(mask, mode);
    LOG_INFO("[Lasso] onMouseRelease: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
}

mediators::ToolId Lasso::id() const { return mediators::ToolId::Lasso; }

QString Lasso::pageTitle() const { return QStringLiteral("Lasso"); }

QCursor Lasso::cursor() const
{
    // F-N (2026-09-10): PS Lasso uses crosshair
    return Qt::CrossCursor;
}

QWidget* Lasso::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(QStringLiteral("Lasso (L) options (F-L stub)"), page);
    layout->addWidget(label);
    layout->addStretch(1);
    LOG_DEBUG("[Lasso] optionPage created");
    return page;
}

} // namespace tools
