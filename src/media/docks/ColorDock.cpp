// SPDX-License-Identifier: MIT
//
// ColorDock implementation - F-H (2026-09-09) + F-M (2026-09-10) + P7.7 (2026-10-14)
//
//   P7.7 refactor: ColorDock is a QTabWidget host with 4 sub-tabs.
//   Sub-tab widgets (ColorTab/SwatchTab/GradientTab/PatternTab) own their state.
//   ColorDock forwards their signals through to its own public signals so
//   external callers (imagewindow.cpp, LeftToolBar) keep working.
//
#include "ColorDock.h"
#include "ColorTab.h"
#include "SwatchTab.h"
#include "GradientTab.h"
#include "PatternTab.h"
#include "logger.h"

#include <QShowEvent>
#include <QTabWidget>
#include <QVBoxLayout>

namespace docks {

ColorDock::ColorDock(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->setDocumentMode(true);
    m_tabs->setMinimumSize(240, 0);
    root->addWidget(m_tabs);

    // Sub-page 1: 颜色 (Color)
    m_colorTab = new ColorTab(this);
    m_tabs->addTab(m_colorTab, tr("颜色"));

    // Sub-page 2: 色板 (Swatch)
    m_swatchTab = new SwatchTab(this);
    m_tabs->addTab(m_swatchTab, tr("色板"));

    // Sub-page 3: 渐变 (Gradient)
    m_gradientTab = new GradientTab(this);
    m_tabs->addTab(m_gradientTab, tr("渐变"));

    // Sub-page 4: 图章 (Pattern)
    m_patternTab = new PatternTab(this);
    m_tabs->addTab(m_patternTab, tr("图章"));

    // Forward signals from sub-tabs to ColorDock's public signals
    connect(m_colorTab, &ColorTab::foregroundColorChanged,
            this, &ColorDock::onColorTabForegroundChanged);
    connect(m_colorTab, &ColorTab::backgroundColorChanged,
            this, &ColorDock::onColorTabBackgroundChanged);

    connect(m_swatchTab, &SwatchTab::swatchClicked,
            this, &ColorDock::onSwatchClicked);

    connect(m_gradientTab, &GradientTab::gradientTypeChanged,
            this, &ColorDock::onGradientTypeForward);
    connect(m_gradientTab, &GradientTab::gradientPresetChanged,
            this, &ColorDock::onGradientPresetForward);

    connect(m_patternTab, &PatternTab::patternPresetChanged,
            this, &ColorDock::onPatternPresetForward);

    LOG_INFO("[ColorDock] created with 4 sub-tabs (color/swatch/gradient/pattern)");
}

ColorDock::~ColorDock() = default;

void ColorDock::showEvent(QShowEvent* e)
{
    // PS default: show on Swatch tab when first shown (most-used)
    if (m_tabs && m_swatchTab) {
        m_tabs->setCurrentWidget(m_swatchTab);
    }
    QWidget::showEvent(e);
}

// ===== accessors / setters (forwarded) =====

QColor ColorDock::foregroundColor() const
{
    return m_colorTab ? m_colorTab->foregroundColor() : QColor(Qt::black);
}

QColor ColorDock::backgroundColor() const
{
    return m_colorTab ? m_colorTab->backgroundColor() : QColor(Qt::white);
}

ColorDock::GradientType ColorDock::gradientType() const
{
    if (!m_gradientTab) return GradientType::Linear;
    return static_cast<GradientType>(m_gradientTab->gradientType());
}

int ColorDock::gradientPreset() const
{
    return m_gradientTab ? m_gradientTab->gradientPreset() : 0;
}

int ColorDock::patternPreset() const
{
    return m_patternTab ? m_patternTab->patternPreset() : -1;
}

void ColorDock::setForegroundColor(const QColor& c)
{
    if (m_colorTab) m_colorTab->setForegroundColor(c);
}

void ColorDock::setBackgroundColor(const QColor& c)
{
    if (m_colorTab) m_colorTab->setBackgroundColor(c);
}

void ColorDock::setGradientType(GradientType t)
{
    if (m_gradientTab) {
        m_gradientTab->setGradientType(
            static_cast<GradientTab::GradientType>(static_cast<int>(t)));
    }
}

void ColorDock::setGradientPreset(int idx)
{
    if (m_gradientTab) m_gradientTab->setGradientPreset(idx);
}

void ColorDock::setPatternPreset(int idx)
{
    if (m_patternTab) m_patternTab->setPatternPreset(idx);
}

// ===== signal forwarders =====
//   All forwarders are thin: receive sub-tab signal, re-emit as ColorDock signal.
//   No state mutation here — sub-tabs own the state. The sub-tab signal
//   already passed its own "no echo" guard; re-emit just propagates.

void ColorDock::onColorTabForegroundChanged(const QColor& c)
{
    // Propagate to GradientTab so its "fg->bg" / "fg->transp" presets redraw
    if (m_gradientTab) m_gradientTab->setForegroundColor(c);
    emit foregroundColorChanged(c);
}

void ColorDock::onColorTabBackgroundChanged(const QColor& c)
{
    if (m_gradientTab) m_gradientTab->setBackgroundColor(c);
    emit backgroundColorChanged(c);
}

void ColorDock::onGradientTypeForward(GradientTab::GradientType t)
{
    emit gradientTypeChanged(static_cast<GradientType>(static_cast<int>(t)));
}

void ColorDock::onGradientPresetForward(int idx)
{
    emit gradientPresetChanged(idx);
}

void ColorDock::onPatternPresetForward(int idx)
{
    emit patternPresetChanged(idx);
}

void ColorDock::onSwatchClicked(int idx, const QColor& c)
{
    // PS default: clicking a swatch sets foreground color
    Q_UNUSED(idx);
    if (m_colorTab) m_colorTab->setForegroundColor(c);
}

} // namespace docks