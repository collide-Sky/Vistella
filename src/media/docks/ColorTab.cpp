// SPDX-License-Identifier: MIT
//
// ColorTab implementation - P7.7 (2026-10-14)
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
        p.setColor(QPalette::Window, m_background);
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
        p.setColor(QPalette::Window, m_foreground);
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

void ColorTab::setForegroundColor(const QColor& c)
{
    if (m_foreground == c) return;
    m_foreground = c;
    if (m_fgSwatch) {
        QPalette p = m_fgSwatch->palette();
        p.setColor(QPalette::Window, c);
        m_fgSwatch->setPalette(p);
    }
    emit foregroundColorChanged(c);
    LOG_DEBUG("[ColorTab] foreground: {} {} {}", c.red(), c.green(), c.blue());
}

void ColorTab::setBackgroundColor(const QColor& c)
{
    if (m_background == c) return;
    m_background = c;
    if (m_bgSwatch) {
        QPalette p = m_bgSwatch->palette();
        p.setColor(QPalette::Window, c);
        m_bgSwatch->setPalette(p);
    }
    emit backgroundColorChanged(c);
}

void ColorTab::onForegroundColorChanged(const QColor& c)
{
    if (m_foreground == c) return;
    m_foreground = c;
    if (m_fgSwatch) {
        QPalette p = m_fgSwatch->palette();
        p.setColor(QPalette::Window, c);
        m_fgSwatch->setPalette(p);
    }
    // No emit (echo guard)
}

void ColorTab::onBackgroundColorChanged(const QColor& c)
{
    if (m_background == c) return;
    m_background = c;
    if (m_bgSwatch) {
        QPalette p = m_bgSwatch->palette();
        p.setColor(QPalette::Window, c);
        m_bgSwatch->setPalette(p);
    }
}

void ColorTab::onPickClicked()
{
    QColor c = QColorDialog::getColor(m_foreground, this, tr("选择前景色"));
    if (c.isValid()) {
        setForegroundColor(c);
    }
}

void ColorTab::onSwapClicked()
{
    QColor tmp = m_foreground;
    setForegroundColor(m_background);
    setBackgroundColor(tmp);
}

void ColorTab::onDefaultClicked()
{
    setForegroundColor(Qt::black);
    setBackgroundColor(Qt::white);
}

} // namespace docks