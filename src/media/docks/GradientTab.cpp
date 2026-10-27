// SPDX-License-Identifier: MIT
//
// GradientTab implementation
//
#include "GradientTab.h"
#include "logger.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QVBoxLayout>

namespace docks {

// F-M.1: 8 PS-style gradient presets.
//   index 0/1: dynamic (use current fg/bg)
//   index 2..7: fixed pairs
struct PresetPair {
    const char* name;
    QColor      c0;
    QColor      c1;
    bool        isDynamic0;  // index 0: c0 = fg
    bool        isDynamic1;  // index 1: c1 = transp (depends on fg)
};

static const PresetPair kPresets[8] = {
    { "fg->bg",         QColor(),       QColor(),       true,  false },  // 0
    { "fg->transp",     QColor(),       QColor(0,0,0,0),true,  true  },  // 1
    { "black->white",   QColor("#000000"), QColor("#FFFFFF"), false, false },  // 2
    { "white->black",   QColor("#FFFFFF"), QColor("#000000"), false, false },  // 3
    { "red->orange",    QColor("#FF0000"), QColor("#FFA500"), false, false },  // 4
    { "orange->yellow", QColor("#FFA500"), QColor("#FFFF00"), false, false },  // 5
    { "yellow->green",  QColor("#FFFF00"), QColor("#00FF00"), false, false },  // 6
    { "blue->purple",   QColor("#0000FF"), QColor("#800080"), false, false },  // 7
};

GradientTab::GradientTab(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(6);

    // Type combo row
    auto* typeRow = new QHBoxLayout;
    typeRow->setSpacing(4);
    auto* typeLabel = new QLabel(tr("类型:"), this);
    typeRow->addWidget(typeLabel);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("线性"));
    m_typeCombo->addItem(tr("径向"));
    m_typeCombo->addItem(tr("角度"));
    m_typeCombo->addItem(tr("菱形"));
    m_typeCombo->addItem(tr("对称"));
    typeRow->addWidget(m_typeCombo, 1);
    root->addLayout(typeRow);

    // Preview bar
    m_preview = new QFrame(this);
    m_preview->setMinimumHeight(28);
    m_preview->setFrameShape(QFrame::Box);
    m_preview->setAutoFillBackground(true);
    root->addWidget(m_preview);

    // Preset grid
    m_presetGrid = new QGridLayout;
    m_presetGrid->setSpacing(3);
    root->addLayout(m_presetGrid);
    for (int col = 0; col < 4; ++col) m_presetGrid->setColumnStretch(col, 1);

    buildPresets();
    updatePreview();

    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &GradientTab::onTypeComboChanged);
}

GradientTab::~GradientTab() = default;

void GradientTab::buildPresets()
{
    for (int i = 0; i < 8; ++i) {
        QFrame* cell = new QFrame(this);
        cell->setFixedSize(40, 16);
        cell->setFrameShape(QFrame::NoFrame);
        cell->setAutoFillBackground(true);
        cell->setProperty("gradientIndex", i);
        cell->setProperty("selected", i == m_gradientPreset);
        cell->setCursor(Qt::PointingHandCursor);
        cell->setStyleSheet(i == m_gradientPreset
                                ? "border: 2px solid #4A90E2;"
                                : "border: 1px solid #888;");
        cell->installEventFilter(this);
        m_presetGrid->addWidget(cell, i / 4, i % 4);
        m_presetCells[i] = cell;
    }
    updatePresetHighlight();
}

void GradientTab::updatePreview()
{
    if (!m_preview) return;
    QColor c0, c1;
    if (m_gradientPreset == 0) {
        c0 = m_foreground; c1 = m_background;
    } else if (m_gradientPreset == 1) {
        c0 = m_foreground;
        c1 = QColor(m_foreground.red(), m_foreground.green(),
                    m_foreground.blue(), 0);
    } else {
        c0 = kPresets[m_gradientPreset].c0;
        c1 = kPresets[m_gradientPreset].c1;
    }
    QPixmap pm(m_preview->width(), m_preview->height());
    QPainter painter(&pm);
    QLinearGradient lg(0, 0, pm.width(), 0);
    lg.setColorAt(0.0, c0);
    lg.setColorAt(1.0, c1);
    painter.fillRect(pm.rect(), lg);
    painter.end();
    QPalette pp = m_preview->palette();
    pp.setBrush(QPalette::Window, QBrush(pm));
    m_preview->setPalette(pp);
}

void GradientTab::updatePresetHighlight()
{
    for (int i = 0; i < 8; ++i) {
        if (!m_presetCells[i]) continue;
        bool sel = (i == m_gradientPreset);
        m_presetCells[i]->setProperty("selected", sel);
        m_presetCells[i]->setStyleSheet(sel ? "border: 2px solid #4A90E2;"
                                            : "border: 1px solid #888;");
    }
}

void GradientTab::onTypeComboChanged(int idx)
{
    GradientType t = static_cast<GradientType>(idx);
    if (m_gradientType == t) return;
    m_gradientType = t;
    LOG_INFO("[GradientTab] type: {}", static_cast<int>(t));
    emit gradientTypeChanged(t);
}

void GradientTab::setGradientType(GradientType t)
{
    if (m_gradientType == t) return;
    m_gradientType = t;
    if (m_typeCombo) {
        QSignalBlocker block(m_typeCombo);
        m_typeCombo->setCurrentIndex(static_cast<int>(t));
    }
    emit gradientTypeChanged(t);
}

void GradientTab::setGradientPreset(int idx)
{
    if (idx < 0 || idx >= 8) return;
    if (m_gradientPreset == idx) return;
    m_gradientPreset = idx;
    updatePresetHighlight();
    updatePreview();
    LOG_INFO("[GradientTab] preset: {}", idx);
    emit gradientPresetChanged(idx);
}

bool GradientTab::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonPress) {
        auto* frame = qobject_cast<QFrame*>(obj);
        if (!frame) return QWidget::eventFilter(obj, e);
        if (frame->property("gradientIndex").isValid()) {
            int idx = frame->property("gradientIndex").toInt();
            if (idx >= 0 && idx < 8) {
                setGradientPreset(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

} // namespace docks