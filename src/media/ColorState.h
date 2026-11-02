#ifndef COLORSTATE_H
#define COLORSTATE_H

// =============================================================
//  ColorState -- the single source of truth for the foreground and
//                 background colour (M1-8)
//
//  The problem this solves:
//
//    The foreground / background colour used to live in THREE places:
//      - tools::LeftToolBar  (m_fgColor / m_bgColor)   -- per MainWindow
//      - docks::ColorTab     (m_foreground / m_background) -- per ImageWindow
//      - tools::Brush        (m_paintColor)             -- per tool instance
//    and the chain between them was BROKEN: MainWindow connected the
//    toolbar to RightPanelDock::colorDock(), which is a deprecated
//    accessor that returns nullptr -- so that connect never ran. The
//    toolbar colour did not reach the colour panel, and the brush
//    colour did not follow at all.
//
//  The model:
//
//    1. ColorState is owned by the ImageWindow (the document). The
//       colour is per DOCUMENT, like Photoshop: each open image has
//       its own foreground / background colour.
//
//    2. ColorTab (right panel) and LeftToolBar (left toolbar) are
//       VIEWS. They subscribe to the ColorState and render what it
//       says. They do not own the colour any more.
//
//    3. The Brush reads the colour from the ColorState when it
//       stamps, instead of keeping its own copy. That removes the
//       staleness hole where the brush kept a stale colour after a
//       tool switch (the Brush instance is destroyed and recreated on
//       every tool switch, which reset m_paintColor to black).
//
//    4. The Eyedropper pushes the sampled colour into the ColorState,
//       which closes DEBT entry A-EYEDROPPER (the eyedropper must
//       push the sampled colour into the shared state).
//
//  Signal flow:
//
//    user picks a colour in the toolbar
//        -> ColorState::setForegroundColor()
//        -> ColorState emits foregroundColorChanged()
//        -> ColorTab repaints, Brush picks it up on the next stamp
//
//    user picks a colour in the colour panel
//        -> ColorState::setForegroundColor()
//        -> ...same...
//
//    user samples with the eyedropper
//        -> ColorState::setForegroundColor()
//        -> ...same...
//
//  There is exactly ONE path, so the views can never disagree.
// =============================================================

#include <QObject>
#include <QColor>

namespace vistella {

class ColorState : public QObject
{
    Q_OBJECT

public:
    explicit ColorState(QObject* parent = nullptr);

    QColor foregroundColor() const { return m_foreground; }
    QColor backgroundColor() const { return m_background; }

    // Swap the foreground and the background (PS: the X key).
    void swapColors();

    // Reset to the PS defaults: black foreground, white background.
    void defaultColors();

    // The brush colour is the foreground colour -- one accessor so the
    // brush cannot accidentally read a different copy.
    QColor brushColor() const { return m_foreground; }

signals:
    void foregroundColorChanged(const QColor& c);
    void backgroundColorChanged(const QColor& c);

public slots:
    // Set the foreground colour. Does nothing when the colour is
    // unchanged, so the signal is only emitted on a real change (that
    // is what breaks the echo loops between the views).
    void setForegroundColor(const QColor& c);
    void setBackgroundColor(const QColor& c);

private:
    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;
};

}   // namespace vistella

#endif   // COLORSTATE_H
