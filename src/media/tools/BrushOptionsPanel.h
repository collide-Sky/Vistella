// SPDX-License-Identifier: MIT
//
// BrushOptionsPanel - P1.1 (2026-09-15) Brush full implementation
//   PS-style dynamic options panel for the Brush tool.
//   Two-way binds BrushSettings / BrushDynamics to UI sliders + checkboxes.
//   On any UI change, emits presetChanged(BrushPreset) so the active Brush
//   tool can call setPreset(preset).
//
//   Provides a sub-set of PS Brush panel (size/hardness/opacity/flow/spacing/
//   angle/roundness + Size Jitter + Scattering + Smoothing). Additional
//   dynamics groups (Transfer / Texture / Dual) can be added in P1.1 follow-up.

#pragma once

#include <QWidget>

#include "../brushes/BrushPreset.h"

class QLabel;
class QSlider;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QToolButton;

namespace Ui { class BrushOptionsPanel; }

namespace tools {

class BrushOptionsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit BrushOptionsPanel(QWidget* parent = nullptr);
    ~BrushOptionsPanel() override;

    // Initialize UI from a preset (called on first show + when preset selected).
    void setPreset(const brushes::BrushPreset &p);

    // Read back current UI as a preset (used to detect changes).
    brushes::BrushPreset currentPreset() const;

    // Bind to an existing BrushPreset's name (for header label).
    void setPresetName(const QString &name);

signals:
    // Emitted on any UI change. Caller should call brush.setPreset(p).
    void presetChanged(const brushes::BrushPreset &p);
    // Emitted when user clicks the "..." picker button (opens BrushPickerDialog).
    void pickerRequested();

private slots:
    // Forward signals from individual widgets.
    void onAnySettingChanged();
    void onPresetComboChanged(int idx);

private:
    Ui::BrushOptionsPanel* ui = nullptr;
    bool                   m_suspend = false;     // suppress recursive updates
    int                    m_presetIndex = 0;
};

} // namespace tools