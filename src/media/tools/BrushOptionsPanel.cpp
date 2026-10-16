// SPDX-License-Identifier: MIT
//
// BrushOptionsPanel impl - P1.1 (2026-09-15)
// P7.6.x (2026-10-14): 接入 BrushSharedState 6 字段 (blendMode/size/opacity/flow/
//   angle/smoothing). 取代 TopOptionBar. 撮合在 ImageOptionBar::attach() 里.

#include "BrushOptionsPanel.h"
#include "ui_BrushOptionsPanel.h"

#include "../brushes/BrushPreset.h"
#include "../brushes/BrushKind.h"

#include <QMenu>
#include <QSignalBlocker>

namespace tools {

BrushOptionsPanel::BrushOptionsPanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::BrushOptionsPanel)
{
    ui->setupUi(this);

    // Wire individual widget changes to onAnySettingChanged
    //   (skipping cmbPreset which has its own handler)
    connect(ui->sliderSize,        &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinSize,          qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderHardness,    &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinHardness,      qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderOpacity,     &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinOpacity,       qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderFlow,        &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinFlow,          qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderSpacing,     &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinSpacing,       qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderAngle,       &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinAngle,         qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderRoundness,   &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinRoundness,     qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);

    connect(ui->chkSizeJitter,       &QCheckBox::toggled, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderSizeJitter,    &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->cmbSizeJitterControl, qOverload<int>(&QComboBox::currentIndexChanged), this, &BrushOptionsPanel::onAnySettingChanged);

    connect(ui->chkScatter,    &QCheckBox::toggled, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->sliderScatter, &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinScatter,   qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);

    connect(ui->sliderSmoothAmount,  &QSlider::valueChanged, this, &BrushOptionsPanel::onAnySettingChanged);
    connect(ui->spinSmoothAmount,    qOverload<int>(&QSpinBox::valueChanged), this, &BrushOptionsPanel::onAnySettingChanged);

    connect(ui->cmbPreset, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BrushOptionsPanel::onPresetComboChanged);

    // P7.6.x (2026-10-14): BlendMode 不走 presetChanged (preset 里没这值) 走 sharedStateChanged.
    connect(ui->cmbBlendMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BrushOptionsPanel::onBlendModeChanged);

    connect(ui->btnPicker, &QToolButton::clicked, this, &BrushOptionsPanel::pickerRequested);

    // P7.9.2 (2026-10-16): kind picker button -> QMenu with 13 kinds.
    if (ui->btnKindPicker) {
        connect(ui->btnKindPicker, &QToolButton::clicked,
                this, &BrushOptionsPanel::onKindPickerClicked);
    }
    // Initialize lblKind from the default m_kind (HardRound).
    refreshKindLabel();
}

BrushOptionsPanel::~BrushOptionsPanel()
{
    delete ui;
}

void BrushOptionsPanel::setPresetName(const QString &name)
{
    m_suspend = true;
    int idx = ui->cmbPreset->findText(name);
    if (idx >= 0) {
        ui->cmbPreset->setCurrentIndex(idx);
        m_presetIndex = idx;
    }
    m_suspend = false;
}

void BrushOptionsPanel::setPreset(const brushes::BrushPreset &p)
{
    m_suspend = true;

    // Settings
    ui->sliderSize->setValue(p.settings.size);
    ui->spinSize->setValue(p.settings.size);
    ui->sliderHardness->setValue(p.settings.hardness);
    ui->spinHardness->setValue(p.settings.hardness);
    ui->sliderOpacity->setValue(p.settings.opacity);
    ui->spinOpacity->setValue(p.settings.opacity);
    ui->sliderFlow->setValue(p.settings.flow);
    ui->spinFlow->setValue(p.settings.flow);
    ui->sliderSpacing->setValue(p.settings.spacing);
    ui->spinSpacing->setValue(p.settings.spacing);
    ui->sliderAngle->setValue(p.settings.angle);
    ui->spinAngle->setValue(p.settings.angle);
    ui->sliderRoundness->setValue(p.settings.roundness);
    ui->spinRoundness->setValue(p.settings.roundness);

    // Shape Dynamics - Size Jitter
    ui->chkSizeJitter->setChecked(p.dynamics.shape.sizeJitterMax > 0);
    ui->sliderSizeJitter->setValue(p.dynamics.shape.sizeJitterMax);
    int ctrlIdx = 0;
    switch (p.dynamics.shape.sizeControl) {
        case brushes::JitterControl::Off:         ctrlIdx = 0; break;
        case brushes::JitterControl::Fade:        ctrlIdx = 1; break;
        case brushes::JitterControl::PenPressure: ctrlIdx = 2; break;
        case brushes::JitterControl::PenTilt:     ctrlIdx = 3; break;
        case brushes::JitterControl::Rotation:    ctrlIdx = 2; break;  // not in UI
        case brushes::JitterControl::StylusWheel:  ctrlIdx = 3; break;
    }
    ui->cmbSizeJitterControl->setCurrentIndex(ctrlIdx);

    // Scattering
    bool scatterOn = (p.dynamics.scattering.scatterX > 0 || p.dynamics.scattering.scatterY > 0);
    ui->chkScatter->setChecked(scatterOn);
    int scatter = p.dynamics.scattering.bothAxes
                  ? p.dynamics.scattering.scatterX
                  : p.dynamics.scattering.scatterY;
    ui->sliderScatter->setValue(scatter);
    ui->spinScatter->setValue(scatter);

    // Smoothing
    ui->sliderSmoothAmount->setValue(p.dynamics.smoothing.amount);
    ui->spinSmoothAmount->setValue(p.dynamics.smoothing.amount);

    // Match preset combo to name
    setPresetName(p.name);

    // P7.9.2 (2026-10-16): also reflect the preset's kind in lblKind +
    //   m_kind. setPreset() is the canonical "load preset" path; the kind
    //   is the preset's own kind field.
    m_kind = p.kind;
    refreshKindLabel();

    m_suspend = false;
}

brushes::BrushPreset BrushOptionsPanel::currentPreset() const
{
    brushes::BrushPreset p;

    // Settings
    p.settings.size      = ui->sliderSize->value();
    p.settings.hardness  = ui->sliderHardness->value();
    p.settings.opacity   = ui->sliderOpacity->value();
    p.settings.flow      = ui->sliderFlow->value();
    p.settings.spacing   = ui->sliderSpacing->value();
    p.settings.angle     = ui->sliderAngle->value();
    p.settings.roundness = ui->sliderRoundness->value();

    // Shape Dynamics - Size Jitter
    int jmax = ui->chkSizeJitter->isChecked() ? ui->sliderSizeJitter->value() : 0;
    p.dynamics.shape.sizeJitterMin = 0;
    p.dynamics.shape.sizeJitterMax = jmax;
    int ctrlIdx = ui->cmbSizeJitterControl->currentIndex();
    switch (ctrlIdx) {
        case 0: p.dynamics.shape.sizeControl = brushes::JitterControl::Off; break;
        case 1: p.dynamics.shape.sizeControl = brushes::JitterControl::Fade; break;
        case 2: p.dynamics.shape.sizeControl = brushes::JitterControl::PenPressure; break;
        case 3: p.dynamics.shape.sizeControl = brushes::JitterControl::PenTilt; break;
        default: p.dynamics.shape.sizeControl = brushes::JitterControl::Off; break;
    }

    // Scattering
    if (ui->chkScatter->isChecked()) {
        int s = ui->sliderScatter->value();
        p.dynamics.scattering.scatterX = s;
        p.dynamics.scattering.scatterY = s;
    }

    // Smoothing
    p.dynamics.smoothing.enabled = ui->sliderSmoothAmount->value() > 0;
    p.dynamics.smoothing.amount  = ui->sliderSmoothAmount->value();

    // Name: from combo current text
    p.name = ui->cmbPreset->currentText();
    if (p.name.isEmpty()) p.name = QStringLiteral("Custom Brush");

    return p;
}

void BrushOptionsPanel::onAnySettingChanged()
{
    if (m_suspend) return;
    emit presetChanged(currentPreset());
    // P7.6.x (2026-10-14): 6 共享字段 (size/opacity/flow/angle/smoothing) 中 5 个
    //   跟 BrushPreset 字段重叠 — 它们也会触发 sharedStateChanged 跟 ToolContext 同步.
    //   BlendMode 由 onBlendModeChanged 单独发.
    emit sharedStateChanged();
}

void BrushOptionsPanel::onBlendModeChanged(int /*idx*/)
{
    if (m_suspend) return;
    // cmbBlendMode 不影响 BrushPreset, 仅触发 sharedStateChanged.
    emit sharedStateChanged();
}

void BrushOptionsPanel::onPresetComboChanged(int idx)
{
    if (m_suspend) return;
    m_presetIndex = idx;
    // P7.9.2 (2026-10-16): 5 -> 13 built-in presets. Switch expanded; order
    //   matches BrushKind::allKinds() so the index of any preset is the
    //   same as the index in the static BrushPresetManager::builtIns() list.
    const brushes::BrushPreset p = makeBuiltInByIndex(idx);
    // Update UI to reflect chosen preset (without re-emitting)
    QSignalBlocker block(this);
    Q_UNUSED(block);
    setPreset(p);
    // P7.9.2: also update the kind label + m_kind.
    m_kind = p.kind;
    refreshKindLabel();
    emit presetChanged(p);
    emit sharedStateChanged();  // 9 重叠字段也跟 ToolContext 同步
}

// ---------------- P7.6.x (2026-10-14) shared state ----------------

BrushOptionsPanel::SharedState BrushOptionsPanel::sharedStatePulledIn() const
{
    SharedState s;
    s.blendMode  = ui->cmbBlendMode->currentIndex();
    s.size       = ui->sliderSize->value();
    s.opacityPct = ui->sliderOpacity->value();
    s.flowPct    = ui->sliderFlow->value();
    s.smoothing  = ui->sliderSmoothAmount->value();
    s.angle      = ui->sliderAngle->value();
    // P7.9.1 (2026-10-15): 3 advanced fields — .ui already has the widgets
    // (sliderHardness/sliderSpacing/sliderScatter + spin partners) from
    // earlier design; we just plumb them through the shared state.
    s.hardness   = ui->sliderHardness->value();
    s.spacing    = ui->sliderSpacing->value();
    s.scatter    = ui->sliderScatter->value();
    return s;
}

void BrushOptionsPanel::applySharedState(const SharedState& s)
{
    m_suspend = true;
    ui->cmbBlendMode->setCurrentIndex(s.blendMode);
    ui->sliderSize->setValue(s.size);
    ui->spinSize->setValue(s.size);
    ui->sliderOpacity->setValue(static_cast<int>(s.opacityPct));
    ui->spinOpacity->setValue(static_cast<int>(s.opacityPct));
    ui->sliderFlow->setValue(static_cast<int>(s.flowPct));
    ui->spinFlow->setValue(static_cast<int>(s.flowPct));
    ui->sliderSmoothAmount->setValue(static_cast<int>(s.smoothing));
    ui->spinSmoothAmount->setValue(static_cast<int>(s.smoothing));
    ui->sliderAngle->setValue(s.angle);
    ui->spinAngle->setValue(s.angle);
    // P7.9.1: 3 new fields
    ui->sliderHardness->setValue(static_cast<int>(s.hardness));
    ui->spinHardness->setValue(static_cast<int>(s.hardness));
    ui->sliderSpacing->setValue(static_cast<int>(s.spacing));
    ui->spinSpacing->setValue(static_cast<int>(s.spacing));
    ui->sliderScatter->setValue(static_cast<int>(s.scatter));
    ui->spinScatter->setValue(static_cast<int>(s.scatter));
    m_suspend = false;
}

// ---------------- P7.9.2 (2026-10-16) 13 brush kinds ----------------
//
//   Design (user拍板 2026-10-16):
//     - lblKind readonly shows "Basic - Hard Round" / "Wet - Watercolor" /
//       "Special - Eraser" etc.
//     - btnKindPicker (QToolButton "v") pops a QMenu with 13 QActions under
//       3 section headers (Basic / Wet / Special).
//     - On kind selection, combo switches to first preset of that kind.
//     - On preset combo change (built-in index), kind is inferred from the
//       preset's `kind` field.
//
//   m_kind drives: lblKind text, per-kind needsGpu() override (P8.2),
//   and the status bar GPU indicator (P7.11 follow-up).

brushes::BrushPreset BrushOptionsPanel::makeBuiltInByIndex(int idx) const
{
    // Order matches BrushKind::allKinds() / BrushPresetManager::builtIns().
    switch (idx) {
        case 0:  return brushes::BrushPreset::makeBuiltInHardRound();
        case 1:  return brushes::BrushPreset::makeBuiltInSoftRound();
        case 2:  return brushes::BrushPreset::makeBuiltInPencil();
        case 3:  return brushes::BrushPreset::makeBuiltInMarker();
        case 4:  return brushes::BrushPreset::makeBuiltInAirbrush();
        case 5:  return brushes::BrushPreset::makeBuiltInChalk();
        case 6:  return brushes::BrushPreset::makeBuiltInCharcoal();
        case 7:  return brushes::BrushPreset::makeBuiltInCalligraphic();
        case 8:  return brushes::BrushPreset::makeBuiltInWatercolor();
        case 9:  return brushes::BrushPreset::makeBuiltInOil();
        case 10: return brushes::BrushPreset::makeBuiltInAcrylic();
        case 11: return brushes::BrushPreset::makeBuiltInSmudge();
        case 12: return brushes::BrushPreset::makeBuiltInEraser();
        default: return brushes::BrushPreset::makeBuiltInHardRound();
    }
}

void BrushOptionsPanel::refreshKindLabel()
{
    if (!ui || !ui->lblKind) return;
    ui->lblKind->setText(QString::fromLatin1(categoryName(categoryOf(m_kind)))
                       + QStringLiteral(" - ")
                       + kindName(m_kind));
}

void BrushOptionsPanel::setKind(brushes::BrushKind k)
{
    if (m_kind == k) return;
    m_kind = k;
    refreshKindLabel();
    selectFirstPresetOfKind(k);
    emit kindChanged(k);
}

void BrushOptionsPanel::selectFirstPresetOfKind(brushes::BrushKind k)
{
    if (!ui || !ui->cmbPreset) return;
    // The 13 built-in combo items are in BrushKind::allKinds() order, so
    // the index of the first preset of kind `k` is the kind's enum value.
    const int idx = static_cast<int>(k);
    if (idx < 0 || idx >= ui->cmbPreset->count()) return;
    QSignalBlocker block(ui->cmbPreset);
    Q_UNUSED(block);
    ui->cmbPreset->setCurrentIndex(idx);
    m_presetIndex = idx;
}

void BrushOptionsPanel::onKindPickerClicked()
{
    buildKindMenu();
    if (!m_kindMenu) return;
    const QPoint pos = ui->btnKindPicker->mapToGlobal(
        QPoint(0, ui->btnKindPicker->height()));
    m_kindMenu->popup(pos);
}

void BrushOptionsPanel::buildKindMenu()
{
    if (m_kindMenu) return;     // built once, reused
    m_kindMenu = new QMenu(this);
    m_kindMenu->setTitle(QStringLiteral("Pick brush kind"));

    // 3 section headers (Basic / Wet / Special) + their QActions. Each
    // QAction's data is the BrushKind enum value; triggered -> setKind.
    for (int cat = 0; cat < 3; ++cat) {
        const brushes::BrushCategory bc = static_cast<brushes::BrushCategory>(cat);
        // Skip empty categories (defensive — all 3 are non-empty in P7.9.2).
        bool any = false;
        for (const auto k : brushes::allKinds()) {
            if (brushes::categoryOf(k) == bc) { any = true; break; }
        }
        if (!any) continue;
        m_kindMenu->addSection(QString::fromLatin1(brushes::categoryName(bc)));
        for (const auto k : brushes::allKinds()) {
            if (brushes::categoryOf(k) != bc) continue;
            QAction* a = m_kindMenu->addAction(brushes::kindName(k));
            a->setData(static_cast<int>(k));
            a->setCheckable(true);
            a->setChecked(k == m_kind);
            connect(a, &QAction::triggered, this, [this, k]() {
                setKind(k);
            });
        }
    }
}

} // namespace tools