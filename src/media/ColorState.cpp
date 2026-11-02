#include "ColorState.h"

namespace vistella {

ColorState::ColorState(QObject* parent)
    : QObject(parent)
{
}

void ColorState::setForegroundColor(const QColor& c)
{
    if (!c.isValid()) return;
    if (m_foreground == c) return;   // no change -> no signal
    m_foreground = c;
    emit foregroundColorChanged(m_foreground);
}

void ColorState::setBackgroundColor(const QColor& c)
{
    if (!c.isValid()) return;
    if (m_background == c) return;   // no change -> no signal
    m_background = c;
    emit backgroundColorChanged(m_background);
}

void ColorState::swapColors()
{
    const QColor fg = m_foreground;
    m_foreground = m_background;
    m_background = fg;
    emit foregroundColorChanged(m_foreground);
    emit backgroundColorChanged(m_background);
}

void ColorState::defaultColors()
{
    setForegroundColor(Qt::black);
    setBackgroundColor(Qt::white);
}

}   // namespace vistella
