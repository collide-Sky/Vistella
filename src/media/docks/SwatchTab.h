// SPDX-License-Identifier: MIT
//
// SwatchTab - P7.7 (2026-10-14)
//
//   Sub-page 2 of ColorDock. PS-style "Swatch" tab:
//     - 12 PS standard palette swatches (4 cols x 3 rows)
//     - Click to set foreground color (PS default behavior)
//
//   P7.8 (2026-10-15) will extend to a tree with categories (tree/grass/...)
//
#pragma once

#include <QWidget>
#include <QColor>

class QFrame;
class QGridLayout;

namespace docks {

class SwatchTab : public QWidget
{
    Q_OBJECT
public:
    explicit SwatchTab(QWidget* parent = nullptr);
    ~SwatchTab();

    // Set the swatch to highlight (idx 0..11); -1 = none
    void setHighlightedIndex(int idx);
    int  highlightedIndex() const { return m_highlighted; }

signals:
    // PS default: clicking a swatch sets foreground color
    void swatchClicked(int idx, const QColor& color);

private:
    bool eventFilter(QObject* obj, QEvent* e);

    QFrame*      m_cells[12] = { nullptr };
    QGridLayout* m_grid      = nullptr;
    int          m_highlighted = -1;
};

} // namespace docks