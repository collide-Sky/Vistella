// SPDX-License-Identifier: MIT
//
// SwatchTab implementation
//
//   QTreeView with 3 categories. Each swatch is a QStandardItem whose
//   background brush is the swatch color; the QStandardItem also stores
//   the QColor in UserRole for emit.
//
//   P7.8 stage: 3 hardcoded categories with theme colors.
//   P7.11+ (palette library): load .aco / .gpl, add multiple user palettes.
//
#include "SwatchTab.h"
#include "logger.h"

#include <QBrush>
#include <QModelIndex>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace docks {

// 3 categories with 4-6 themed swatches each
//   树 (Tree):     browns / leaf greens (forest, bark, leaf)
//   草 (Grass):    greens (light, dark, olive, lime, mint)
//   水滴 (Water):  blues / cyans (sky, ocean, ice, deep, lagoon)
//
// Total = 5 + 5 + 5 = 15 swatches.
struct SwatchSpec {
    const char* name;   // display name (English; lupdate for tr)
    QColor      color;
};

static const SwatchSpec kTreeSwatches[] = {
    { "Forest",   QColor("#2D5016") },
    { "Bark",     QColor("#5C3A1E") },
    { "Leaf",     QColor("#7CB342") },
    { "Moss",     QColor("#4A6B2A") },
    { "Pine",     QColor("#1B3A1B") },
};
static const SwatchSpec kGrassSwatches[] = {
    { "Light",    QColor("#9CCC65") },
    { "Dark",     QColor("#33691E") },
    { "Olive",    QColor("#808000") },
    { "Lime",     QColor("#CDDC39") },
    { "Mint",     QColor("#A5D6A7") },
};
static const SwatchSpec kWaterSwatches[] = {
    { "Sky",      QColor("#4FC3F7") },
    { "Ocean",    QColor("#0277BD") },
    { "Ice",      QColor("#B3E5FC") },
    { "Deep",     QColor("#01579B") },
    { "Lagoon",   QColor("#00ACC1") },
};

static const int kTreeCount  = sizeof(kTreeSwatches) / sizeof(SwatchSpec);
static const int kGrassCount = sizeof(kGrassSwatches) / sizeof(SwatchSpec);
static const int kWaterCount = sizeof(kWaterSwatches) / sizeof(SwatchSpec);

SwatchTab::SwatchTab(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(0);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels(QStringList{});  // no column header

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setExpandsOnDoubleClick(false);   // single click on item already selects
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(14);
    m_tree->setAnimated(false);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(m_tree, 1);

    buildTree();

    // Expand all categories by default (P7.8 PS style — see all swatches)
    m_tree->expandAll();

    connect(m_tree, &QTreeView::clicked, this, &SwatchTab::onItemClicked);
}

SwatchTab::~SwatchTab() = default;

void SwatchTab::buildTree()
{
    m_model->removeRows(0, m_model->rowCount());

    // Helper: append a category with N swatches
    auto addCategory = [&](const QString& name,
                           const SwatchSpec* specs, int count) {
        auto* category = new QStandardItem(name);
        category->setFlags(Qt::ItemIsEnabled);  // not selectable as a swatch
        category->setData(QStringLiteral("category"), Qt::UserRole);
        QFont f = category->font();
        f.setBold(true);
        category->setFont(f);
        m_model->appendRow(category);

        for (int i = 0; i < count; ++i) {
            const SwatchSpec& s = specs[i];
            auto* item = new QStandardItem(QString::fromUtf8(s.name));
            item->setEditable(false);
            item->setToolTip(QString::fromUtf8(s.name));
            // Color in background (full row) and store QColor in UserRole
            item->setBackground(QBrush(s.color));
            // Use contrasting text color
            qreal luminance = 0.299 * s.color.red()
                            + 0.587 * s.color.green()
                            + 0.114 * s.color.blue();
            item->setForeground(QColor(luminance > 128 ? Qt::black : Qt::white));
            item->setData(QVariant::fromValue(s.color), Qt::UserRole);
            item->setData(QStringLiteral("swatch"), Qt::UserRole + 1);
            category->appendRow(item);
        }
    };

    addCategory(tr("树 (Tree)"),    kTreeSwatches,  kTreeCount);
    addCategory(tr("草 (Grass)"),   kGrassSwatches, kGrassCount);
    addCategory(tr("水滴 (Water)"), kWaterSwatches, kWaterCount);
}

int SwatchTab::swatchCount() const
{
    return kTreeCount + kGrassCount + kWaterCount;
}

QModelIndex SwatchTab::flatToModelIndex(int flatIdx) const
{
    if (flatIdx < 0) return QModelIndex();
    int remaining = flatIdx;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        QStandardItem* cat = m_model->item(r);
        if (!cat) continue;
        int childCount = cat->rowCount();
        if (remaining < childCount) {
            return cat->child(remaining)->index();
        }
        remaining -= childCount;
    }
    return QModelIndex();
}

int SwatchTab::modelIndexToFlat(const QModelIndex& idx) const
{
    if (!idx.isValid()) return -1;
    QStandardItem* item = m_model->itemFromIndex(idx);
    if (!item) return -1;
    if (item->data(Qt::UserRole + 1).toString() != QStringLiteral("swatch")) {
        return -1;  // category header
    }
    // Compute flat index: sum of all previous categories' child counts + row in current
    QStandardItem* parent = item->parent();
    if (!parent) return -1;
    int flat = 0;
    int parentRow = parent->index().row();
    for (int r = 0; r < parentRow; ++r) {
        QStandardItem* prevCat = m_model->item(r);
        if (prevCat) flat += prevCat->rowCount();
    }
    flat += item->row();
    return flat;
}

void SwatchTab::setHighlightedIndex(int idx)
{
    if (m_highlighted == idx) return;
    m_highlighted = idx;
    // P7.8 stage: just track index; full highlight requires custom delegate.
    // P7.11+ adds CustomDelegate that paints 2px blue border on the row.
    LOG_DEBUG("[SwatchTab] setHighlightedIndex={}", idx);
}

void SwatchTab::onItemClicked(const QModelIndex& index)
{
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;
    if (item->data(Qt::UserRole + 1).toString() != QStringLiteral("swatch")) {
        // Category header clicked: toggle expand
        if (m_tree->isExpanded(index)) {
            m_tree->collapse(index);
        } else {
            m_tree->expand(index);
        }
        return;
    }
    QColor c = item->data(Qt::UserRole).value<QColor>();
    int flatIdx = modelIndexToFlat(index);
    setHighlightedIndex(flatIdx);
    emit swatchClicked(flatIdx, c);
    LOG_DEBUG("[SwatchTab] clicked flat={} color=({},{},{})",
              flatIdx, c.red(), c.green(), c.blue());
}

} // namespace docks