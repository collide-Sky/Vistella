// SPDX-License-Identifier: MIT
//
// SwatchTab
//
//   Sub-page 2 of ColorDock. PS-style "Swatch" tab.
//   P7.7: 12-color flat grid (4 cols x 3 rows).
//   P7.8: classification tree with 3 categories (树 Tree / 草 Grass / 水滴 Water Drop).
//     Each category has 4-6 swatches. Click to set foreground color.
//
//   P7.8 stage: QTreeView with QStandardItemModel. Item data role carries the
//   QColor. Click on a swatch -> emit swatchClicked(idx, color) where idx is
//   a stable index in [0..total). P7.11+ (palette library) will load palettes
//   from .aco / .gpl files and replace the hardcoded 3 categories.
//
#pragma once

#include <QWidget>
#include <QColor>

class QTreeView;
class QStandardItemModel;
class QStandardItem;
class QModelIndex;

namespace docks {

// TODO(P8.7): the swatch, gradient and pattern lists are hard-coded sample data -- see docs/DEBT.md#A-SWATCH
class SwatchTab : public QWidget
{
    Q_OBJECT
public:
    explicit SwatchTab(QWidget* parent = nullptr);
    ~SwatchTab();

    // Set the swatch to highlight by flat index across all categories (0..N-1).
    //   P7.8 stage: stores index, applies highlight on next paint.
    //   P7.11+ (custom delegate): paints a 2px border on the matching swatch.
    void setHighlightedIndex(int idx);
    int  highlightedIndex() const { return m_highlighted; }

    // Total swatch count across all categories
    int swatchCount() const;

signals:
    // PS default: clicking a swatch sets foreground color
    void swatchClicked(int flatIndex, const QColor& color);

private slots:
    void onItemClicked(const QModelIndex& index);

private:
    void buildTree();
    // Map a flat index (0..swatchCount()-1) to its QModelIndex in the tree.
    QModelIndex flatToModelIndex(int flatIdx) const;
    // Inverse: model index -> flat index (or -1 if header).
    int modelIndexToFlat(const QModelIndex& idx) const;

    QTreeView*           m_tree     = nullptr;
    QStandardItemModel*  m_model    = nullptr;
    int                  m_highlighted = -1;
};

} // namespace docks