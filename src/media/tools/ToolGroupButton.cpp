// SPDX-License-Identifier: MIT
//
// ToolGroupButton implementation
//
// See ToolGroupButton.h for the why.
#include "ToolGroupButton.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

namespace tools {

const int ToolGroupButton::LONG_PRESS_MS;
const int ToolGroupButton::LONG_PRESS_SLOP_PX;

ToolGroupButton::ToolGroupButton(QWidget* parent)
    : QToolButton(parent)
{
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(16, 16));
    setPopupMode(QToolButton::DelayedPopup);

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    m_longPressTimer->setInterval(LONG_PRESS_MS);
    connect(m_longPressTimer, &QTimer::timeout, this, &ToolGroupButton::openPopup);
}

void ToolGroupButton::setGroup(const QList<int>& toolIds, const QStringList& icons)
{
    m_toolIds = toolIds;
    m_icons = icons;
    if (m_current < 0 && !m_toolIds.isEmpty()) {
        m_current = m_toolIds.first();
        m_active = m_current;
    }
    applyCurrentIcon();
}

void ToolGroupButton::setCurrentToolId(int id)
{
    if (m_current == id) return;
    m_current = id;
    applyCurrentIcon();
}

void ToolGroupButton::setActiveToolId(int id)
{
    m_active = id;
}

void ToolGroupButton::applyCurrentIcon()
{
    const int idx = m_toolIds.indexOf(m_current);
    if (idx < 0 || idx >= m_icons.size()) return;
    setIcon(QIcon(QStringLiteral(":/icons/tools/") + m_icons.at(idx)));
    setToolTip(m_toolIds.size() > 1
                   ? tr("%1 (hold to see the other tools)").arg(m_icons.at(idx))
                   : m_icons.at(idx));
}

void ToolGroupButton::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressActive = true;
        m_pressPos = e->pos();
        // The long press opens the popup. The timer is only armed when
        // the group really has siblings -- a group with one tool has
        // nothing to pop up.
        if (hasSiblings()) {
            m_longPressTimer->start();
        }
        e->accept();
        return;
    }
    QToolButton::mousePressEvent(e);
}

void ToolGroupButton::mouseMoveEvent(QMouseEvent* e)
{
    if (m_pressActive) {
        // Moving far enough away from the press point cancels the long
        // press -- the user is dragging, not holding.
        const QPoint delta = e->pos() - m_pressPos;
        if (std::abs(delta.x()) > LONG_PRESS_SLOP_PX
            || std::abs(delta.y()) > LONG_PRESS_SLOP_PX) {
            m_longPressTimer->stop();
        }
        e->accept();
        return;
    }
    QToolButton::mouseMoveEvent(e);
}

void ToolGroupButton::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_pressActive) {
        m_pressActive = false;
        m_longPressTimer->stop();

        // If the popup is already open the long press handled the
        // release -- the popup stays open.
        if (m_menu && m_menu->isVisible()) {
            e->accept();
            return;
        }

        if (hasSiblings()) {
            // A short click on a group opens the popup straight away
            // (the Photoshop behaviour when the triangle is clicked;
            // without the triangle this is the friendliest variant).
            openPopup();
        } else if (m_current >= 0) {
            emit toolActivated(m_current);
        }
        e->accept();
        return;
    }
    QToolButton::mouseReleaseEvent(e);
}

void ToolGroupButton::openPopup()
{
    if (!hasSiblings()) return;

    if (!m_menu) {
        m_menu = new QMenu(this);
    } else {
        m_menu->clear();
    }

    for (int i = 0; i < m_toolIds.size(); ++i) {
        const int id = m_toolIds.at(i);
        QAction* action = m_menu->addAction(
            id == m_active ? QIcon(QStringLiteral(":/icons/tools/") + m_icons.at(i))
                           : QIcon(),
            m_icons.at(i));
        action->setCheckable(true);
        action->setChecked(id == m_active);
        connect(action, &QAction::triggered, this, [this, id]() {
            // The picked tool becomes the button's new default -- the
            // Photoshop behaviour.
            m_active = id;
            setCurrentToolId(id);
            emit toolActivated(id);
        });
    }

    m_menu->popup(mapToGlobal(rect().bottomLeft()));
}

void ToolGroupButton::paintEvent(QPaintEvent* e)
{
    QToolButton::paintEvent(e);

    // The little triangle in the bottom right corner -- the marker
    // that says "there are more tools in this group".
    if (!hasSiblings()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tri(width() - 5.0, height() - 5.0);
    QPolygonF arrow;
    arrow << QPointF(tri.x() - 3.0, tri.y() + 1.5)
          << QPointF(tri.x() + 3.0, tri.y() + 1.5)
          << QPointF(tri.x(), tri.y() - 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 40));
    p.drawPolygon(arrow);
}

} // namespace tools
