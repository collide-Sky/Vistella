// SPDX-License-Identifier: MIT
//
// GradientTab - P7.7 (2026-10-14)
//
//   Sub-page 3 of ColorDock. PS-style "Gradient" tab:
//     - 5 gradient types combo (linear/radial/angle/diamond/symmetric)
//     - Preview bar (200x24, fg->bg linear default)
//     - 8 PS-style preset cells (4 cols x 2 rows)
//
//   P7.7 stage: render a static preview based on the selected preset.
//   P7.11+ (PS gradient editor full): will add stop editor / position / opacity.
//
#pragma once

#include <QWidget>
#include <QColor>

class QComboBox;
class QFrame;
class QGridLayout;

namespace docks {

class GradientTab : public QWidget
{
    Q_OBJECT
public:
    explicit GradientTab(QWidget* parent = nullptr);
    ~GradientTab();

    // 5 PS standard gradient types
    enum class GradientType {
        Linear     = 0,
        Radial     = 1,
        Angle      = 2,
        Diamond    = 3,
        Symmetric  = 4,
    };
    Q_ENUM(GradientType)

    GradientType gradientType() const { return m_gradientType; }
    int          gradientPreset() const { return m_gradientPreset; }

    // Set fg/bg colors that drive the "fg->bg" / "fg->transp" presets
    void setForegroundColor(const QColor& c) { m_foreground = c; updatePreview(); }
    void setBackgroundColor(const QColor& c) { m_background = c; updatePreview(); }

public slots:
    void setGradientType(GradientType t);
    void setGradientPreset(int idx);

signals:
    void gradientTypeChanged(GradientType t);
    void gradientPresetChanged(int idx);

private slots:
    void onTypeComboChanged(int idx);

private:
    void buildPresets();
    void updatePreview();
    void updatePresetHighlight();
    bool eventFilter(QObject* obj, QEvent* e);

    QComboBox*  m_typeCombo  = nullptr;
    QFrame*     m_preview    = nullptr;
    QFrame*     m_presetCells[8] = { nullptr };
    QGridLayout* m_presetGrid = nullptr;

    GradientType m_gradientType   = GradientType::Linear;
    int          m_gradientPreset = 0;
    QColor       m_foreground     = Qt::black;
    QColor       m_background     = Qt::white;
};

} // namespace docks