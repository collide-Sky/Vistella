// SPDX-License-Identifier: MIT
//
// ToolGroupButton
//
// A toolbar button that belongs to a GROUP of tools.
//
//   Photoshop groups the tools: one button shows the tool that is
//   currently active, and the small triangle in its corner opens a
//   popup with the sibling tools. Clicking a sibling switches to it
//   AND makes it the button's new default.
//
//   The audit (docs/CODE_AUDIT_2026-10-26.md 5.3 item 10) found that
//   this interaction was missing entirely: the toolbar was a flat row
//   of 15 buttons with no groups at all.
//
//   Why a custom widget instead of a plain QToolButton with a menu:
//   the group button has to show TWO things at once -- the icon of the
//   active tool and the "there are more tools here" triangle -- and it
//   has to react to a long press (not just to a click on the triangle).
//   QToolButton can do the menu, but not the long press.
//
//   The long press: the button is held down for LONG_PRESS_MS without
//   the mouse moving, and the popup opens. A short click activates the
//   current tool (or opens the popup when the group has more than one
//   tool and the click was on the triangle).
#pragma once

#include <QPoint>
#include <QToolButton>

class QMenu;

namespace tools {

class ToolGroupButton : public QToolButton
{
    Q_OBJECT
public:
    // How long the button has to be held down to open the popup.
    static const int LONG_PRESS_MS = 500;
    // How far the mouse may move during the press and still count as
    // a long press (and not as a drag).
    static const int LONG_PRESS_SLOP_PX = 4;

    explicit ToolGroupButton(QWidget* parent = nullptr);

    // The tools in this group. The first one is the default.
    //
    //   The order is the order the popup shows them in.
    void setGroup(const QList<int>& toolIds, const QStringList& icons);

    // The tool the button currently shows.
    int currentToolId() const { return m_current; }
    void setCurrentToolId(int id);

    // The tool that a short click activates.
    //
    //   It is the tool that was picked last -- Photoshop keeps the
    //   picked tool as the button's new default.
    int activeToolId() const { return m_active; }
    void setActiveToolId(int id);

    // The tool that was picked with the last long press.
    int lastPickedToolId() const { return m_active; }

    // Does the group hold more than one tool?
    bool hasSiblings() const { return m_toolIds.size() > 1; }

    // The tools of the group, in the popup order.
    QList<int> toolIds() const { return m_toolIds; }

signals:
    // The user picked a tool from the group (by click or by popup).
    void toolActivated(int toolId);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    void openPopup();
    void applyCurrentIcon();

    QList<int>    m_toolIds;
    QStringList   m_icons;
    int           m_current = -1;   // the tool the button shows
    int           m_active  = -1;   // the tool a short click activates
    QMenu*        m_menu = nullptr;
    bool          m_pressActive = false;
    QPoint        m_pressPos;
    QTimer*       m_longPressTimer = nullptr;
};

} // namespace tools
