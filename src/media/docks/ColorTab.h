// SPDX-License-Identifier: MIT
//
// ColorTab
//
//   Sub-page 1 of ColorDock. PS-style "Color" tab:
//     - foreground/background swatch (fg 30x30 over bg 50x50)
//     - "Pick Color..." button (opens QColorDialog)
//     - Swap (X) and Default (D) buttons
//
//   This is a logical extraction from the original single-page ColorDock.
//   ColorDock now hosts 4 sub-tabs (Color/Swatch/Gradient/Pattern) and
//   forwards the signals from this widget to keep its public API stable.
//
#pragma once

#include <QWidget>
#include <QColor>

#include "../ColorState.h"

class QFrame;
class QPushButton;

namespace docks {

class ColorTab : public QWidget
{
    Q_OBJECT
public:
    explicit ColorTab(QWidget* parent = nullptr);
    ~ColorTab();

    // M1-8: the colour lives in the ColorState, not here.
    //   ColorTab is a VIEW: it renders what the state says and pushes
    //   user changes into it. There is no second copy any more.
    QColor foregroundColor() const;
    QColor backgroundColor() const;

    // Bind this view to the document's colour state.
    //
    //   The state is the single source of truth (docs/CONTRACTS.md
    //   7.6.2): the toolbar, this panel and the brush all read it, so
    //   they cannot disagree.
    //
    //   Binding twice re-points the view at the new state (the old
    //   connections are dropped).
    void setColorState(vistella::ColorState* state);
    vistella::ColorState* colorState() const { return m_state; }

public slots:
    // Push a colour into the state (the state decides whether that is a
    // change worth signalling).
    void setForegroundColor(const QColor& c);
    void setBackgroundColor(const QColor& c);
    // Update the view WITHOUT pushing back into the state.
    //   These are the slots the state itself is connected to, so they
    //   must not write back -- that would be an echo loop.
    void onForegroundColorChanged(const QColor& c);
    void onBackgroundColorChanged(const QColor& c);

signals:
    void foregroundColorChanged(const QColor& c);
    void backgroundColorChanged(const QColor& c);

private slots:
    void onPickClicked();
    void onSwapClicked();
    void onDefaultClicked();

private:
    QFrame*     m_bgSwatch   = nullptr;
    QFrame*     m_fgSwatch   = nullptr;
    QPushButton* m_pickBtn   = nullptr;
    QPushButton* m_swapBtn   = nullptr;
    QPushButton* m_defaultBtn = nullptr;

    // M1-8: the state this view renders.
    //   Not owned -- the ImageWindow owns it (one state per document).
    vistella::ColorState* m_state = nullptr;
};

} // namespace docks