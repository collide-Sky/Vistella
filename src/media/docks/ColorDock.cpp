// SPDX-License-Identifier: MIT
//
// ColorDock implementation - F-H (2026-09-09) + F-M (2026-09-10)
//
#include "ColorDock.h"
#include "ui_ColorDock.h"
#include "logger.h"

#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>

namespace docks {

// F-H: PS standard 12-color swatch palette
static const QColor kDefaultPalette[] = {
    QColor("#000000"), QColor("#7F7F7F"), QColor("#880015"), QColor("#ED1C24"),
    QColor("#FF7F27"), QColor("#FFF200"), QColor("#22B14C"), QColor("#00A2E8"),
    QColor("#3F48CC"), QColor("#A349A4"), QColor("#B97A57"), QColor("#FFAEC9"),
};

// F-M.1: 8 PS-style gradient preset stops (color at position 0.0 and 1.0)
struct GradientPreset {
    const char* name;
    QColor      c0;     // position 0.0 (start)
    QColor      c1;     // position 1.0 (end)
};
// 8 preset defined at runtime since c0/c1 depend on current fg/bg color.
// We use 2 static presets (fg->bg / fg->transparent) and 6 fixed pairs.
static const GradientPreset kGradientPresets[] = {
    { "fg->bg",       QColor(),        QColor()        },   // index 0: dynamic (uses m_foreground/m_background)
    { "fg->transp",   QColor(),        QColor(0,0,0,0) },   // index 1: dynamic fg -> transparent
    { "black->white", QColor("#000000"), QColor("#FFFFFF") },
    { "white->black", QColor("#FFFFFF"), QColor("#000000") },
    { "red->orange",  QColor("#FF0000"), QColor("#FFA500") },
    { "orange->yellow", QColor("#FFA500"), QColor("#FFFF00") },
    { "yellow->green", QColor("#FFFF00"), QColor("#00FF00") },
    { "blue->purple", QColor("#0000FF"), QColor("#800080") },
};

// F-M.2: 6 PS-style pattern presets (rendered as QPixmap on demand)
enum class PatternKind {
    SolidBlack = 0,    // index 0
    SolidWhite = 1,    // index 1
    Grid       = 2,    // index 2
    Diagonal   = 3,    // index 3
    Dots       = 4,    // index 4
    Checker    = 5,    // index 5
};
static const int kPatternCount = 6;

ColorDock::ColorDock(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::ColorDock;
    ui->setupUi(this);

    // F-H: set initial swatch colors
    {
        QPalette bp = ui->bgSwatch->palette();
        bp.setColor(QPalette::Window, m_background);
        ui->bgSwatch->setPalette(bp);
        QPalette fp = ui->fgSwatch->palette();
        fp.setColor(QPalette::Window, m_foreground);
        ui->fgSwatch->setPalette(fp);
    }

    // F-H: pick button
    connect(ui->pickBtn, &QPushButton::clicked, this, &ColorDock::onPickColorClicked);

    // F-M.1: gradient type combo (5 items, index 0..4)
    connect(ui->gradientTypeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ColorDock::onGradientTypeChanged);

    // Build dynamic cell widgets
    setupPalette();
    setupGradientPresets();
    setupPatternPresets();
    updateGradientPreview();
}

ColorDock::~ColorDock()
{
    delete ui;
}

void ColorDock::setForegroundColor(const QColor& c)
{
    if (m_foreground == c) return;
    m_foreground = c;
    if (ui && ui->fgSwatch) {
        QPalette p = ui->fgSwatch->palette();
        p.setColor(QPalette::Window, c);
        ui->fgSwatch->setPalette(p);
    }
    // F-M.1: preset 0/1 depend on fg/bg -> redraw preview
    if (m_gradientPreset <= 1) {
        updateGradientPreview();
    }
    emit foregroundColorChanged(c);
    LOG_DEBUG("[ColorDock] foreground: {} {} {}", c.red(), c.green(), c.blue());
}

void ColorDock::setBackgroundColor(const QColor& c)
{
    if (m_background == c) return;
    m_background = c;
    if (ui && ui->bgSwatch) {
        QPalette p = ui->bgSwatch->palette();
        p.setColor(QPalette::Window, c);
        ui->bgSwatch->setPalette(p);
    }
    if (m_gradientPreset == 0) {
        updateGradientPreview();
    }
    emit backgroundColorChanged(c);
}

void ColorDock::onPickColorClicked()
{
    QColor c = QColorDialog::getColor(m_foreground, this, tr("选择前景色"));
    if (c.isValid()) {
        setForegroundColor(c);
    }
}

void ColorDock::onGradientTypeChanged(int idx)
{
    GradientType t = static_cast<GradientType>(idx);
    if (m_gradientType == t) return;
    m_gradientType = t;
    LOG_INFO("[ColorDock] gradient type: {}", static_cast<int>(t));
    emit gradientTypeChanged(t);
}

void ColorDock::setGradientType(GradientType t)
{
    if (m_gradientType == t) return;
    m_gradientType = t;
    if (ui && ui->gradientTypeCombo) {
        ui->gradientTypeCombo->blockSignals(true);
        ui->gradientTypeCombo->setCurrentIndex(static_cast<int>(t));
        ui->gradientTypeCombo->blockSignals(false);
    }
    emit gradientTypeChanged(t);
}

void ColorDock::setGradientPreset(int idx)
{
    if (idx < 0 || idx >= 8) return;
    if (m_gradientPreset == idx) return;
    m_gradientPreset = idx;
    // Update cell highlight (border)
    if (ui) {
        auto* grid = ui->gradientPresetsGrid;
        if (grid) {
            for (int i = 0; i < grid->count(); ++i) {
                QWidget* w = grid->itemAt(i)->widget();
                if (!w) continue;
                bool sel = (w->property("gradientIndex").toInt() == idx);
                w->setProperty("selected", sel);
                w->setStyleSheet(sel ? "border: 2px solid #4A90E2;"
                                     : "border: 1px solid #888;");
            }
        }
    }
    updateGradientPreview();
    LOG_INFO("[ColorDock] gradient preset: {}", idx);
    emit gradientPresetChanged(idx);
}

void ColorDock::setPatternPreset(int idx)
{
    if (idx < -1 || idx >= kPatternCount) return;
    if (m_patternPreset == idx) return;
    m_patternPreset = idx;
    if (ui) {
        auto* grid = ui->patternPresetsGrid;
        if (grid) {
            for (int i = 0; i < grid->count(); ++i) {
                QWidget* w = grid->itemAt(i)->widget();
                if (!w) continue;
                bool sel = (w->property("patternIndex").toInt() == idx);
                w->setProperty("selected", sel);
                w->setStyleSheet(sel ? "border: 2px solid #4A90E2;"
                                     : "border: 1px solid #888;");
            }
        }
    }
    LOG_INFO("[ColorDock] pattern preset: {}", idx);
    emit patternPresetChanged(idx);
}

// ===== F-H: 12 palette cells =====
void ColorDock::setupPalette()
{
    if (!ui || !ui->paletteGrid) return;
    for (int i = 0; i < 12; ++i) {
        QFrame* cell = new QFrame(ui->paletteContainer);
        cell->setFixedSize(20, 20);
        cell->setFrameShape(QFrame::Box);
        cell->setAutoFillBackground(true);
        QPalette p = cell->palette();
        p.setColor(QPalette::Window, kDefaultPalette[i]);
        cell->setPalette(p);
        cell->setProperty("paletteIndex", i);
        cell->setCursor(Qt::PointingHandCursor);
        cell->installEventFilter(this);
        ui->paletteGrid->addWidget(cell, i / 4, i % 4);
    }
}

// ===== F-M.1: 8 gradient preset cells =====
void ColorDock::setupGradientPresets()
{
    if (!ui || !ui->gradientPresetsGrid) return;
    for (int i = 0; i < 8; ++i) {
        QFrame* cell = new QFrame(ui->gradientPresetsContainer);
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
        ui->gradientPresetsGrid->addWidget(cell, i / 4, i % 4);
    }
}

// ===== F-M.2: 6 pattern preset cells =====
void ColorDock::setupPatternPresets()
{
    if (!ui || !ui->patternPresetsGrid) return;
    for (int i = 0; i < kPatternCount; ++i) {
        QFrame* cell = new QFrame(ui->patternPresetsContainer);
        cell->setFixedSize(40, 40);
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
        ui->patternPresetsGrid->addWidget(cell, i / 3, i % 3);
    }
}

// ===== F-M.1: gradient preview bar =====
void ColorDock::updateGradientPreview()
{
    if (!ui || !ui->gradientPreview) return;
    QColor c0, c1;
    if (m_gradientPreset == 0) {
        c0 = m_foreground; c1 = m_background;
    } else if (m_gradientPreset == 1) {
        c0 = m_foreground; c1 = QColor(m_foreground.red(), m_foreground.green(),
                                        m_foreground.blue(), 0);
    } else {
        c0 = kGradientPresets[m_gradientPreset].c0;
        c1 = kGradientPresets[m_gradientPreset].c1;
    }
    QPixmap pm(ui->gradientPreview->width(), ui->gradientPreview->height());
    QPainter painter(&pm);
    QLinearGradient lg(0, 0, pm.width(), 0);
    lg.setColorAt(0.0, c0);
    lg.setColorAt(1.0, c1);
    painter.fillRect(pm.rect(), lg);
    painter.end();
    QPalette pp = ui->gradientPreview->palette();
    pp.setBrush(QPalette::Window, QBrush(pm));
    ui->gradientPreview->setPalette(pp);
}

// ===== Event filter: palette / gradient / pattern cells =====
bool ColorDock::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonPress) {
        auto* frame = qobject_cast<QFrame*>(obj);
        if (!frame) return QWidget::eventFilter(obj, e);

        // F-H: palette cell
        if (frame->property("paletteIndex").isValid()) {
            int idx = frame->property("paletteIndex").toInt();
            if (idx >= 0 && idx < 12) {
                setForegroundColor(kDefaultPalette[idx]);
                return true;
            }
        }
        // F-M.1: gradient preset cell
        if (frame->property("gradientIndex").isValid()) {
            int idx = frame->property("gradientIndex").toInt();
            if (idx >= 0 && idx < 8) {
                setGradientPreset(idx);
                return true;
            }
        }
        // F-M.2: pattern preset cell
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
