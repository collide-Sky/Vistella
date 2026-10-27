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

class QFrame;
class QPushButton;

namespace docks {

class ColorTab : public QWidget
{
    Q_OBJECT
public:
    explicit ColorTab(QWidget* parent = nullptr);
    ~ColorTab();

    QColor foregroundColor() const { return m_foreground; }
    QColor backgroundColor() const { return m_background; }

public slots:
    void setForegroundColor(const QColor& c);
    void setBackgroundColor(const QColor& c);
    // Forwarded from ColorDock when the active brush tool changes its
    //   default brush color (so swatch updates without emit echo loop).
    // P7.7: used by ColorDock to keep fg/bg swatch in sync across windows.
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

    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;
};

} // namespace docks