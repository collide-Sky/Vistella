// SPDX-License-Identifier: MIT
//
// ColorTab implementation
//
#include "ColorTab.h"
#include "logger.h"

#include <QColorDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace docks {

ColorTab::ColorTab(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(6);

    // Swatch row: bg 50x50 + fg 30x30 + spacer + buttons
    auto* swatchRow = new QHBoxLayout;
    swatchRow->setSpacing(4);

    m_bgSwatch = new QFrame(this);
    m_bgSwatch->setMinimumSize(50, 50);
    m_bgSwatch->setMaximumSize(50, 50);
    m_bgSwatch->setFrameShape(QFrame::Box);
    m_bgSwatch->setAutoFillBackground(true);
    {
        QPalette p = m_bgSwatch->palette();
        p.setColor(QPalette::Window, QColor(Qt::white));
        m_bgSwatch->setPalette(p);
    }
    swatchRow->addWidget(m_bgSwatch);

    m_fgSwatch = new QFrame(this);
    m_fgSwatch->setMinimumSize(30, 30);
    m_fgSwatch->setMaximumSize(30, 30);
    m_fgSwatch->setFrameShape(QFrame::Box);
    m_fgSwatch->setAutoFillBackground(true);
    {
        QPalette p = m_fgSwatch->palette();
        p.setColor(QPalette::Window, QColor(Qt::black));
        m_fgSwatch->setPalette(p);
    }
    swatchRow->addWidget(m_fgSwatch);

    swatchRow->addStretch(1);

    // Right-side compact buttons: X swap + D default
    m_swapBtn = new QPushButton(QStringLiteral("X"), this);
    m_swapBtn->setObjectName(QStringLiteral("swapBtn"));
    m_swapBtn->setToolTip(tr("交换前景/背景"));
    m_swapBtn->setFixedSize(28, 28);
    swatchRow->addWidget(m_swapBtn);

    m_defaultBtn = new QPushButton(QStringLiteral("D"), this);
    m_defaultBtn->setObjectName(QStringLiteral("defaultBtn"));
    m_defaultBtn->setToolTip(tr("恢复默认前景/背景"));
    m_defaultBtn->setFixedSize(28, 28);
    swatchRow->addWidget(m_defaultBtn);

    root->addLayout(swatchRow);

    // Pick color button (full width)
    m_pickBtn = new QPushButton(tr("拾取颜色..."), this);
    m_pickBtn->setObjectName(QStringLiteral("pickBtn"));
    m_pickBtn->setMinimumHeight(26);
    root->addWidget(m_pickBtn);

    // Spacer pushes everything to the top; future additions below
    root->addStretch(1);

    connect(m_pickBtn,    &QPushButton::clicked, this, &ColorTab::onPickClicked);
    connect(m_swapBtn,    &QPushButton::clicked, this, &ColorTab::onSwapClicked);
    connect(m_defaultBtn, &QPushButton::clicked, this, &ColorTab::onDefaultClicked);
}

ColorTab::~ColorTab() = default;

// M1-8: the swatch painting, in one place.
static void paintSwatch(QFrame* swatch, const QColor& c)
{
    if (!swatch) return;
    QPalette p = swatch->palette();
    p.setColor(QPalette::Window, c);
    swatch->setPalette(p);
}

QColor ColorTab::foregroundColor() const
{
    return m_state ? m_state->foregroundColor() : QColor(Qt::black);
}

QColor ColorTab::backgroundColor() const
{
    return m_state ? m_state->backgroundColor() : QColor(Qt::white);
}

void ColorTab::setColorState(vistella::ColorState* state)
{
    if (m_state == state) return;

    // Drop the old connections, otherwise the old state keeps writing
    // into this view.
    if (m_state) {
        m_state->disconnect(this);
    }

    m_state = state;

    if (m_state) {
        connect(m_state, &vistella::ColorState::foregroundColorChanged,
                this, &ColorTab::onForegroundColorChanged);
        connect(m_state, &vistella::ColorState::backgroundColorChanged,
                this, &ColorTab::onBackgroundColorChanged);
        // Show the current value right away.
        onForegroundColorChanged(m_state->foregroundColor());
        onBackgroundColorChanged(m_state->backgroundColor());
    }
}

void ColorTab::setForegroundColor(const QColor& c)
{
    // Push into the state; the state decides whether it is a change.
    if (m_state) {
        m_state->setForegroundColor(c);
        return;
    }
    // No state bound (e.g. in a unit test): keep the old behaviour and
    // still signal, so the panel is usable standalone.
    paintSwatch(m_fgSwatch, c);
    emit foregroundColorChanged(c);
    LOG_DEBUG("[ColorTab] foreground: {} {} {}", c.red(), c.green(), c.blue());
}

void ColorTab::setBackgroundColor(const QColor& c)
{
    if (m_state) {
        m_state->setBackgroundColor(c);
        return;
    }
    paintSwatch(m_bgSwatch, c);
    emit backgroundColorChanged(c);
}

void ColorTab::onForegroundColorChanged(const QColor& c)
{
    paintSwatch(m_fgSwatch, c);
    // No emit: this slot is connected TO the state, so emitting here
    // would be an echo loop.
}

void ColorTab::onBackgroundColorChanged(const QColor& c)
{
    paintSwatch(m_bgSwatch, c);
    // No emit (echo guard)
}

void ColorTab::onPickClicked()
{
    QColor c = QColorDialog::getColor(foregroundColor(), this, tr("选择前景色"));
    if (c.isValid()) {
        setForegroundColor(c);
    }
}

void ColorTab::onSwapClicked()
{
    if (m_state) {
        m_state->swapColors();
        return;
    }
    const QColor tmp = foregroundColor();
    setForegroundColor(backgroundColor());
    setBackgroundColor(tmp);
}

void ColorTab::onDefaultClicked()
{
    if (m_state) {
        m_state->defaultColors();
        return;
    }
    setForegroundColor(Qt::black);
    setBackgroundColor(Qt::white);
}

} // namespace docks