// SPDX-License-Identifier: MIT
//
// BrushOptionsPanel impl - P1.1 (2026-09-15)

#include "BrushOptionsPanel.h"
#include "ui_BrushOptionsPanel.h"

#include "../brushes/BrushPreset.h"

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

    connect(ui->btnPicker, &QToolButton::clicked, this, &BrushOptionsPanel::pickerRequested);
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
}

void BrushOptionsPanel::onPresetComboChanged(int idx)
{
    if (m_suspend) return;
    m_presetIndex = idx;
    // Load the built-in preset matching this index
    brushes::BrushPreset p;
    switch (idx) {
        case 0: p = brushes::BrushPreset::makeBuiltInHardRound();  break;
        case 1: p = brushes::BrushPreset::makeBuiltInSoftRound();  break;
        case 2: p = brushes::BrushPreset::makeBuiltInAirbrush();   break;
        case 3: p = brushes::BrushPreset::makeBuiltInChalk();      break;
        case 4: p = brushes::BrushPreset::makeBuiltInCharcoal();   break;
        default: p = brushes::BrushPreset::makeBuiltInHardRound(); break;
    }
    // Update UI to reflect chosen preset (without re-emitting)
    QSignalBlocker block(this);
    Q_UNUSED(block);
    setPreset(p);
    emit presetChanged(p);
}

} // namespace tools