// SPDX-License-Identifier: MIT
//
// ColorDock - F-H (2026-09-09) + F-M (2026-09-10) + P7.7 (2026-10-14)
//
//   P7.7: Right-side color panel split into 4 sub-tabs (PS-style):
//     - 颜色 (Color):   fg/bg swatch + pick + swap + default
//     - 色板 (Swatch):  12-color palette grid
//     - 渐变 (Gradient): 5 types + 8 preset cells
//     - 图章 (Pattern): 6 preset cells
//
//   Public API (signals/slots/accessors) preserved for callers
//   (imagewindow.cpp, LeftToolBar, etc). Sub-tab widgets are private
//   members; their signals are forwarded to ColorDock's own signals.
//
//   F-H stage: original single-page widget with fg/bg + 12 swatches
//   F-M stage: added gradient (5 types + 8 presets) + pattern (6 presets)
//   P7.7: split into 4 sub-tabs (this file)
//
#pragma once

#include <QWidget>
#include <QColor>

#include "ColorTab.h"
#include "SwatchTab.h"
#include "GradientTab.h"
#include "PatternTab.h"

class QTabWidget;
class QShowEvent;

namespace docks {

class ColorDock : public QWidget
{
    Q_OBJECT
public:
    explicit ColorDock(QWidget* parent = nullptr);
    ~ColorDock() override;

    // P7.7: Accessors for sub-tabs (used by P7.8+ swatch tree, future tests)
    ColorTab*    colorTab()    const { return m_colorTab; }
    SwatchTab*   swatchTab()   const { return m_swatchTab; }
    GradientTab* gradientTab() const { return m_gradientTab; }
    PatternTab*  patternTab()  const { return m_patternTab; }

    // F-H: foreground/background color accessors (sub-tab Color owns the state)
    QColor foregroundColor() const;
    QColor backgroundColor() const;

    // F-M.1: gradient type (5 PS standard) — re-export from GradientTab
    enum class GradientType {
        Linear     = 0,
        Radial     = 1,
        Angle      = 2,
        Diamond    = 3,
        Symmetric  = 4,
    };
    Q_ENUM(GradientType)

    GradientType gradientType() const;
    int          gradientPreset() const;
    int          patternPreset() const;

public slots:
    void setForegroundColor(const QColor& c);
    void setBackgroundColor(const QColor& c);
    void setGradientType(GradientType t);
    void setGradientPreset(int idx);
    void setPatternPreset(int idx);

signals:
    void foregroundColorChanged(const QColor& c);
    void backgroundColorChanged(const QColor& c);
    void gradientTypeChanged(GradientType t);
    void gradientPresetChanged(int idx);
    void patternPresetChanged(int idx);

protected:
    void showEvent(QShowEvent* e) override;

private slots:
    // Forwarded from sub-tabs. Each is a thin wrapper.
    void onColorTabForegroundChanged(const QColor& c);
    void onColorTabBackgroundChanged(const QColor& c);
    void onGradientTypeForward(GradientTab::GradientType t);
    void onGradientPresetForward(int idx);
    void onPatternPresetForward(int idx);
    void onSwatchClicked(int idx, const QColor& c);

private:
    QTabWidget* m_tabs = nullptr;

    ColorTab*    m_colorTab    = nullptr;
    SwatchTab*   m_swatchTab   = nullptr;
    GradientTab* m_gradientTab = nullptr;
    PatternTab*  m_patternTab  = nullptr;
};

} // namespace docks