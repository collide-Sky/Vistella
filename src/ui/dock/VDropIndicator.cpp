// VDropIndicator.cpp
//   P7.6.x v3 commit 2 (2026-10-14).
#include "VDropIndicator.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

namespace vistella::ui {

VDropIndicator::VDropIndicator(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
}

void VDropIndicator::setShape(Shape s)
{
    if (m_shape == s) return;
    m_shape = s;
    update();
}

void VDropIndicator::attachTo(QWidget* host)
{
    m_host = host;
    if (host) {
        setParent(host);
        raise();
        show();
    } else {
        hide();
    }
}

void VDropIndicator::detach()
{
    m_host.clear();
    hide();
    if (parentWidget()) setParent(nullptr);
}

void VDropIndicator::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    if (m_shape == Shape::None) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    switch (m_shape) {
    case Shape::InterTab: {
        // Thin vertical line, full height of the indicator, centred horizontally.
        const int x = r.width() / 2;
        p.setPen(QPen(m_color, 2));
        p.drawLine(x, 2, x, r.height() - 2);
        break;
    }
    case Shape::OnTab: {
        // Tinted rectangle filling the whole indicator rect.
        p.setBrush(QColor(m_color.red(), m_color.green(), m_color.blue(), 64));
        p.setPen(QPen(m_color, 2));
        p.drawRect(r.adjusted(1, 1, -1, -1));
        break;
    }
    case Shape::DockArea: {
        // Hollow rectangle, slightly inside, with rounded corners.
        QPainterPath path;
        path.addRoundedRect(r.adjusted(2, 2, -2, -2), 4, 4);
        p.setBrush(QColor(m_color.red(), m_color.green(), m_color.blue(), 40));
        p.setPen(QPen(m_color, 2, Qt::DashLine));
        p.drawPath(path);
        break;
    }
    case Shape::None:
        break;
    }
}

void VDropIndicator::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    update();
}

} // namespace vistella::ui
