// SPDX-License-Identifier: MIT
//
// RectSelect implementation - F-L (2026-09-10) + P0-4.5 (2026-09-10)
//
#include "RectSelect.h"
#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "../selection/SelectionStrategy.h"
#include "logger.h"

#include <QComboBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

RectSelect::RectSelect(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::RectSelectionStrategy>();
}

RectSelect::~RectSelect() = default;

void RectSelect::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[RectSelect] onEnter");
}

void RectSelect::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    LOG_DEBUG("[RectSelect] onExit");
}

void RectSelect::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (m_strategy) m_strategy->begin(scenePos);
    LOG_DEBUG("[RectSelect] onMousePress: scene=({}, {})", scenePos.x(), scenePos.y());
}

void RectSelect::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->update(scenePos);
}

void RectSelect::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strategy || !host) return;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return;
    auto* sel = host->selectionModel();
    if (!sel) return;
    // P0-4.6: modifier key mode (Shift/Alt/Shift+Alt)
    auto mode = selection::SelectionModel::modifierKeyToMode(e->modifiers());
    sel->setMask(mask, mode);
    LOG_INFO("[RectSelect] onMouseRelease: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
}

mediators::ToolId RectSelect::id() const { return mediators::ToolId::RectSelect; }

QString RectSelect::pageTitle() const { return QStringLiteral("RectSelect"); }

QCursor RectSelect::cursor() const
{
    // F-N (2026-09-10): PS RectSelect uses crosshair (marching ants reserve)
    return Qt::CrossCursor;
}

QWidget* RectSelect::optionPage(QWidget* parent)
{
    // P0-4.8 (2026-09-10): Mode combo (Replace/Add/Subtract/Intersect)
    //   Default mode is set on click, modified by Shift/Alt/Shift+Alt keys
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(new QLabel(QStringLiteral("Mode:"), page));
    auto* modeCombo = new QComboBox(page);
    modeCombo->addItem(QStringLiteral("New (Replace)"),   0);
    modeCombo->addItem(QStringLiteral("Add (Shift)"),     1);
    modeCombo->addItem(QStringLiteral("Subtract (Alt)"),  2);
    modeCombo->addItem(QStringLiteral("Intersect (Shift+Alt)"), 3);
    modeCombo->setCurrentIndex(0);
    layout->addWidget(modeCombo);
    layout->addStretch(1);
    LOG_DEBUG("[RectSelect] optionPage created");
    return page;
}

} // namespace tools
