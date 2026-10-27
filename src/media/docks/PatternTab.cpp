// SPDX-License-Identifier: MIT
//
// PatternTab implementation
//
//   6 PS-style patterns. P7.11+ extends to full editor (scale/density/color).
//
#include "PatternTab.h"
#include "logger.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QVBoxLayout>

namespace docks {

// F-M.2: 6 PS-style pattern kinds (rendered as QPixmap on demand)
enum class PatternKind {
    SolidBlack = 0,
    SolidWhite = 1,
    Grid       = 2,
    Diagonal   = 3,
    Dots       = 4,
    Checker    = 5,
};
static const int kPatternCount = 6;

PatternTab::PatternTab(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    m_grid = new QGridLayout;
    m_grid->setSpacing(3);
    root->addLayout(m_grid);
    for (int col = 0; col < 3; ++col) m_grid->setColumnStretch(col, 1);

    buildPresets();
    root->addStretch(1);
}

PatternTab::~PatternTab() = default;

void PatternTab::buildPresets()
{
    for (int i = 0; i < kPatternCount; ++i) {
        QFrame* cell = new QFrame(this);
        cell->setMinimumSize(40, 40);
        cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cell->setFrameShape(QFrame::NoFrame);
        cell->setAutoFillBackground(true);
        cell->setProperty("patternIndex", i);
        cell->setProperty("selected", i == m_patternPreset);
        cell->setCursor(Qt::PointingHandCursor);
        cell->setStyleSheet(i == m_patternPreset && i >= 0
                                ? "border: 2px solid #4A90E2;"
                                : "border: 1px solid #888;");

        // Render pattern pixmap
        QPixmap pm(38, 38);
        pm.fill(Qt::white);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const int inset = 1;
        const int inner = 36;
        const QRect r(inset, inset, inner, inner);
        switch (static_cast<PatternKind>(i)) {
        case PatternKind::SolidBlack:
            painter.fillRect(r, Qt::black);
            break;
        case PatternKind::SolidWhite:
            painter.fillRect(r, Qt::white);
            painter.setPen(Qt::gray);
            painter.drawRect(r);
            break;
        case PatternKind::Grid: {
            painter.fillRect(r, Qt::white);
            painter.setPen(QColor("#CCCCCC"));
            for (int x = inset; x <= inset + inner; x += 6) {
                painter.drawLine(x, inset, x, inset + inner);
            }
            for (int y = inset; y <= inset + inner; y += 6) {
                painter.drawLine(inset, y, inset + inner, y);
            }
            break;
        }
        case PatternKind::Diagonal: {
            painter.fillRect(r, Qt::white);
            painter.setPen(QColor("#888888"));
            for (int d = -inner; d <= inner; d += 5) {
                painter.drawLine(inset + d, inset, inset + d + inner, inset + inner);
            }
            break;
        }
        case PatternKind::Dots: {
            painter.fillRect(r, Qt::white);
            painter.setBrush(Qt::black);
            painter.setPen(Qt::NoPen);
            for (int x = inset + 3; x < inset + inner; x += 6) {
                for (int y = inset + 3; y < inset + inner; y += 6) {
                    painter.drawEllipse(QPoint(x, y), 1, 1);
                }
            }
            break;
        }
        case PatternKind::Checker: {
            const int tile = 6;
            for (int x = 0; x < inner; x += tile) {
                for (int y = 0; y < inner; y += tile) {
                    bool dark = ((x / tile) + (y / tile)) % 2 == 0;
                    painter.fillRect(inset + x, inset + y, tile, tile,
                                     dark ? Qt::black : Qt::white);
                }
            }
            break;
        }
        }
        painter.end();
        QPalette pp = cell->palette();
        pp.setBrush(QPalette::Window, QBrush(pm));
        cell->setPalette(pp);
        cell->installEventFilter(this);
        m_grid->addWidget(cell, i / 3, i % 3);
        m_cells[i] = cell;
    }
    updateHighlight();
}

void PatternTab::updateHighlight()
{
    for (int i = 0; i < kPatternCount; ++i) {
        if (!m_cells[i]) continue;
        bool sel = (i == m_patternPreset);
        m_cells[i]->setProperty("selected", sel);
        m_cells[i]->setStyleSheet(sel ? "border: 2px solid #4A90E2;"
                                     : "border: 1px solid #888;");
    }
}

void PatternTab::setPatternPreset(int idx)
{
    if (idx < -1 || idx >= kPatternCount) return;
    if (m_patternPreset == idx) return;
    m_patternPreset = idx;
    updateHighlight();
    LOG_INFO("[PatternTab] preset: {}", idx);
    emit patternPresetChanged(idx);
}

bool PatternTab::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonPress) {
        auto* frame = qobject_cast<QFrame*>(obj);
        if (!frame) return QWidget::eventFilter(obj, e);
        if (frame->property("patternIndex").isValid()) {
            int idx = frame->property("patternIndex").toInt();
            if (idx >= 0 && idx < kPatternCount) {
                setPatternPreset(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

} // namespace docks