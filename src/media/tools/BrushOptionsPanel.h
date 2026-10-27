// SPDX-License-Identifier: MIT
//
// BrushOptionsPanel - Brush full implementation
//   PS-style dynamic options panel for the Brush tool.
//   Two-way binds BrushSettings / BrushDynamics to UI sliders + checkboxes.
//   On any UI change, emits presetChanged(BrushPreset) so the active Brush
//   tool can call setPreset(preset).
//
//   Provides a sub-set of PS Brush panel (size/hardness/opacity/flow/spacing/
//   angle/roundness + Size Jitter + Scattering + Smoothing). Additional
//   dynamics groups (Transfer / Texture / Dual) can be added in P1.1 follow-up.
//
// 增加 BlendMode combo + 6 字段跟 ToolContext.brushSharedState
//   的双向同步. 取代旧 TopOptionBar (P7.5) — TopOptionBar 整个 class 删除, 它的 6 控件
//   拆分如下:
//     - blendMode   -> 新增 cmbBlendMode (在 BrushPreset 里没有的图层混合模式)
//     - size        -> 已经在 sliderSize/spinSize (BrushSettings.size)
//     - opacity     -> 已经在 sliderOpacity/spinOpacity
//     - flow        -> 已经在 sliderFlow/spinFlow
//     - angle       -> 已经在 sliderAngle/spinAngle
//     - smoothing   -> 已经在 sliderSmoothAmount/spinSmoothAmount
//   6 字段的同步通过 sharedStateChanged / sharedStatePulledIn 两个 public method
//   在 ImageOptionBar (Brush page 容器) 里撮合.

#pragma once

#include <QWidget>

#include "../brushes/BrushPreset.h"
#include "../brushes/BrushKind.h"

class QLabel;
class QSlider;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QToolButton;
class QMenu;

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

    // 13 brush kinds. The kind picker is a button next
    //   to a readonly label ("Kind: Basic - Hard Round"). When the user
    //   picks a kind, the preset combo switches to the first preset of
    //   that kind. setKind() programmatically switches both the label and
    //   the combo.
    brushes::BrushKind currentKind() const { return m_kind; }
    void setKind(brushes::BrushKind k);

    // 共享 brush state 撮合接口 — 取代 TopOptionBar
    //   sharedStateChanged: 外部 (用户改 UI 后) 把字段同步到 ToolContext.brushSharedState.
    //   sharedStatePulledIn: ToolContext 状态变 (切 tool / load file / preset 选) 把字段
    //     拉回到 UI. ImageOptionBar 在 Brush page 显示时调一次.
    //   6 -> 9 字段 (+ hardness/spacing/scatter). 字段跟
    //   ToolContext::BrushSharedState 镜像, ImageOptionBar::onToolContextBrushStateChanged
    //   做双边 round-trip.
    struct SharedState {
        int    blendMode  = 0;     // 0..5 (跟 cmbBlendMode 索引一致)
        int    size       = 32;    // px
        double opacityPct = 100.0;// %
        double flowPct    = 100.0;// %
        double smoothing  = 0.0;  // % (P7.9.1: 默认 0, was 50.0)
        int    angle      = 0;    // deg
        // P7.9.1: 3 new advanced fields
        double hardness   = 50.0; // %
        double spacing    = 25.0; // %
        double scatter    = 0.0;  // %
    };
    SharedState sharedStatePulledIn() const; // UI -> struct (用户改 UI 后读)
    void applySharedState(const SharedState& s); // struct -> UI (外部推送)

signals:
    // Emitted on any UI change. Caller should call brush.setPreset(p).
    void presetChanged(const brushes::BrushPreset &p);
    // any of 6 shared fields changed (cmbBlendMode 单独触发
    //   不走 presetChanged). ImageOptionBar 撮合时 1 个 signal 就够.
    void sharedStateChanged();
    // Emitted when user clicks the "..." picker button (opens BrushPickerDialog).
    void pickerRequested();
    // P7.9.2: emitted when kind picker selection changes. Carries the new kind
    //   so subscribers (status bar GPU indicator, BrushOptionsPanel internal)
    //   can react. The preset combo updates automatically.
    void kindChanged(brushes::BrushKind k);

private slots:
    // Forward signals from individual widgets.
    void onAnySettingChanged();
    void onBlendModeChanged(int idx);
    void onPresetComboChanged(int idx);
    // P7.9.2: kind picker button clicked -> show popup QMenu with 13 kinds
    //   grouped by category (Basic / Wet / Special).
    void onKindPickerClicked();

private:
    Ui::BrushOptionsPanel* ui = nullptr;
    bool                   m_suspend = false;     // suppress recursive updates
    int                    m_presetIndex = 0;
    // P7.9.2: current kind (drives the lblKind display + per-kind needsGpu()).
    brushes::BrushKind     m_kind = brushes::BrushKind::HardRound;

    // P7.9.2: build the kind popup QMenu lazily (once). 13 QActions grouped
    //   under 3 section headers (Basic / Wet / Special).
    QMenu*                 m_kindMenu = nullptr;
    void buildKindMenu();

    // P7.9.2: switch combo to first preset of the given kind. Updates
    //   m_kind + lblKind + cmbPreset in one go (m_suspend-guarded).
    void selectFirstPresetOfKind(brushes::BrushKind k);
    // P7.9.2: refresh lblKind text from m_kind.
    void refreshKindLabel();
    // P7.9.2: build a BrushPreset by combo index (replaces 5-case switch
    //   that was hardcoded before P7.9.2).
    brushes::BrushPreset makeBuiltInByIndex(int idx) const;
};

} // namespace tools