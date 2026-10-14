// SPDX-License-Identifier: MIT
//
// SwatchTab implementation - P7.7 (2026-10-14)
//
//   12-color PS standard palette grid. P7.8 replaces with tree.
//
#include "SwatchTab.h"
#include "logger.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPalette>
#include <QVBoxLayout>

namespace docks {

// F-H: 12 PS standard palette colors
static const QColor kDefaultPalette[12] = {
    QColor("#000000"), QColor("#7F7F7F"), QColor("#880015"), QColor("#ED1C24"),
    QColor("#FF7F27"), QColor("#FFF200"), QColor("#22B14C"), QColor("#00A2E8"),
    QColor("#3F48CC"), QColor("#A349A4"), QColor("#B97A57"), QColor("#FFAEC9"),
};

SwatchTab::SwatchTab(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    m_grid = new QGridLayout;
    m_grid->setSpacing(2);
    root->addLayout(m_grid);

    // Q4.3 (2026-10-08): grid stretches — 4 cols share dock width equally
    for (int col = 0; col < 4; ++col) m_grid->setColumnStretch(col, 1);

    for (int i = 0; i < 12; ++i) {
        QFrame* cell = new QFrame(this);
        cell->setMinimumSize(20, 20);
        cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cell->setFrameShape(QFrame::Box);
        cell->setAutoFillBackground(true);
        {
            QPalette p = cell->palette();
            p.setColor(QPalette::Window, kDefaultPalette[i]);
            cell->setPalette(p);
        }
        cell->setProperty("paletteIndex", i);
        cell->setCursor(Qt::PointingHandCursor);
        cell->installEventFilter(this);
        m_grid->addWidget(cell, i / 4, i % 4);
        m_cells[i] = cell;
    }

    root->addStretch(1);
}

SwatchTab::~SwatchTab() = default;

void SwatchTab::setHighlightedIndex(int idx)
{
    if (m_highlighted == idx) return;
    m_highlighted = idx;
    for (int i = 0; i < 12; ++i) {
        if (!m_cells[i]) continue;
        bool sel = (i == idx);
        m_cells[i]->setProperty("selected", sel);
        m_cells[i]->setStyleSheet(sel ? "border: 2px solid #4A90E2;"
                                     : "border: 1px solid #888;");
    }
}

bool SwatchTab::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonPress) {
        auto* frame = qobject_cast<QFrame*>(obj);
        if (!frame) return QWidget::eventFilter(obj, e);
        if (frame->property("paletteIndex").isValid()) {
            int idx = frame->property("paletteIndex").toInt();
            if (idx >= 0 && idx < 12) {
                setHighlightedIndex(idx);
                emit swatchClicked(idx, kDefaultPalette[idx]);
                LOG_DEBUG("[SwatchTab] clicked idx={} color=({},{},{})",
                          idx,
                          kDefaultPalette[idx].red(),
                          kDefaultPalette[idx].green(),
                          kDefaultPalette[idx].blue());
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

} // namespace docks