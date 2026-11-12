// SPDX-License-Identifier: MIT
//
// PolygonSelect implementation
//
#include "EllipseSelect.h"

#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "../selection/SelectionStrategy.h"
#include "../selection/SelectionCommand.h"
#include "logger.h"

#include <QComboBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QWidget>

namespace tools {

EllipseSelect::EllipseSelect(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::EllipseSelectionStrategy>();
}

EllipseSelect::~EllipseSelect() = default;

void EllipseSelect::onEnter(ImageWindow* host)
{
    // Bind the document's selection model: it owns the persistent
    //   selection mode, so the mode survives a tool switch.
    m_selectionModel = host ? host->selectionModel() : nullptr;
    LOG_DEBUG("[EllipseSelect] onEnter: selectionModel={}",
              m_selectionModel ? "bound" : "none");
}

void EllipseSelect::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    m_selectionModel = nullptr;
}

void EllipseSelect::setSelectionMode(selection::SelectionModel::Mode m)
{
    if (m_selectionModel) {
        m_selectionModel->setSelectionMode(m);
        return;
    }
    m_detachedMode = m;
}

selection::SelectionModel::Mode EllipseSelect::selectionMode() const
{
    if (m_selectionModel) return m_selectionModel->selectionMode();
    return m_detachedMode;
}

void EllipseSelect::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (m_strategy) m_strategy->begin(scenePos);
}

void EllipseSelect::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->update(scenePos);
}

void EllipseSelect::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(scenePos);
    if (!m_strategy || !host) return;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return;
    auto* sel = host->selectionModel();
    if (!sel) return;

    auto mode = selection::SelectionModel::modifierKeyToMode(e->modifiers());
    if (mode == selection::SelectionModel::Mode::Replace) {
        mode = selectionMode();
    }

    // The gesture is undoable, like every other selection change.
    const QImage before = sel->mask();
    sel->setMask(mask, mode);
    const QImage after = sel->mask();
    if (auto* stack = host->undoStack()) {
        stack->push(selection::SelectionCommand::makeSetMask(
            sel, before, after, QStringLiteral("Ellipse select")));
    }
    LOG_INFO("[EllipseSelect] onMouseRelease: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
}

QCursor EllipseSelect::cursor() const
{
    return Qt::CrossCursor;
}

QWidget* EllipseSelect::optionPage(QWidget* parent)
{
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
    return page;
}

} // namespace tools
