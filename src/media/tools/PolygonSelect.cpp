// SPDX-License-Identifier: MIT
//
// PolygonSelect implementation
//
#include "PolygonSelect.h"

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

PolygonSelect::PolygonSelect(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::PolygonSelectionStrategy>();
}

PolygonSelect::~PolygonSelect() = default;

void PolygonSelect::onEnter(ImageWindow* host)
{
    m_selectionModel = host ? host->selectionModel() : nullptr;
    LOG_DEBUG("[PolygonSelect] onEnter: selectionModel={}",
              m_selectionModel ? "bound" : "none");
}

void PolygonSelect::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    m_selectionModel = nullptr;
}

void PolygonSelect::setSelectionMode(selection::SelectionModel::Mode m)
{
    if (m_selectionModel) {
        m_selectionModel->setSelectionMode(m);
        return;
    }
    m_detachedMode = m;
}

selection::SelectionModel::Mode PolygonSelect::selectionMode() const
{
    if (m_selectionModel) return m_selectionModel->selectionMode();
    return m_detachedMode;
}

int PolygonSelect::vertexCount() const
{
    return m_strategy ? m_strategy->vertexCount() : 0;
}

bool PolygonSelect::isClosed() const
{
    return m_strategy && m_strategy->isClosed();
}

void PolygonSelect::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(host);
    if (!m_strategy) return;
    if (e->button() != Qt::LeftButton) return;

    // A click adds a vertex. The FIRST click starts the polygon; every
    // click after that adds to it.
    if (!m_strategy->isClosed() && m_strategy->vertexCount() == 0) {
        m_strategy->begin(scenePos);
        return;
    }
    m_strategy->addVertex(scenePos);
}

void PolygonSelect::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host); Q_UNUSED(scenePos);
    // The polygon has discrete vertices -- moving the mouse adds none.
}

void PolygonSelect::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host); Q_UNUSED(scenePos);
    // The release does nothing: the polygon is committed on the double
    // click, not on the last single click.
}

void PolygonSelect::onMouseDoubleClick(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(scenePos);
    if (!m_strategy) return;
    if (e->button() != Qt::LeftButton) return;

    // The double click's own press already added a vertex, so the
    // polygon is closed here.
    if (!m_strategy->close()) {
        LOG_WARN("[PolygonSelect] double click with fewer than 3 vertices, ignore");
        return;
    }
    commitPolygon(host, e->modifiers());
}

bool PolygonSelect::commitPolygon(ImageWindow* host, Qt::KeyboardModifiers mods)
{
    if (!m_strategy || !host) return false;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return false;
    auto* sel = host->selectionModel();
    if (!sel) return false;

    auto mode = selection::SelectionModel::modifierKeyToMode(mods);
    if (mode == selection::SelectionModel::Mode::Replace) {
        mode = selectionMode();
    }

    const QImage before = sel->mask();
    sel->setMask(mask, mode);
    const QImage after = sel->mask();
    if (auto* stack = host->undoStack()) {
        stack->push(selection::SelectionCommand::makeSetMask(
            sel, before, after, QStringLiteral("Polygon select")));
    }
    LOG_INFO("[PolygonSelect] committed: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
    return true;
}

QCursor PolygonSelect::cursor() const
{
    return Qt::CrossCursor;
}

QWidget* PolygonSelect::optionPage(QWidget* parent)
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
