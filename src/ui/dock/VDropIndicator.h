// VDropIndicator.h
//
// Lightweight drop hint overlay used by VDraggableTabWidget while a tab is
// being dragged. Three hint shapes:
//   - InterTab : a vertical line drawn between two tabs, meaning "insert here".
//   - OnTab    : a tinted rectangle behind a single tab, meaning "merge here".
//   - DockArea : a tinted rectangle at a dock edge, meaning "create new dock".
//
// The indicator is a child of the target widget (set via setHost()); the
// caller positions the indicator with move()/resize()/raise() before showing.
//   P7.6.x v3 commit 2 (2026-10-14).
#pragma once

#include <QWidget>
#include <QColor>
#include <QPointer>

class QPaintEvent;
class QResizeEvent;

namespace vistella::ui {

class VDropIndicator : public QWidget {
    Q_OBJECT
public:
    enum class Shape {
        None      = 0,
        InterTab  = 1,    // vertical line between tabs
        OnTab     = 2,    // tinted rect behind a single tab
        DockArea  = 3,    // tinted rect for a new sub-dock
    };
    Q_ENUM(Shape)

    explicit VDropIndicator(QWidget* parent = nullptr);

    Shape  shape()      const { return m_shape; }
    void   setShape(Shape s);

    QColor color() const         { return m_color; }
    void   setColor(QColor c)    { m_color = c; update(); }

    // Convenience: set parent + raise + show. nullptr hides.
    void   attachTo(QWidget* host);
    void   detach();

protected:
    void paintEvent(QPaintEvent* e);
    void resizeEvent(QResizeEvent* e);

private:
    QPointer<QWidget> m_host;
    Shape  m_shape    = Shape::None;
    QColor m_color    = QColor(0x1f, 0x6f, 0xeb, 0xff); // PS-style accent
};

} // namespace vistella::ui
