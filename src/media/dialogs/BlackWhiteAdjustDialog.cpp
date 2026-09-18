// SPDX-License-Identifier: MIT
//
// BlackWhiteAdjustDialog implementation - P0-3.2 v2 (2026-09-18)
//
#include "BlackWhiteAdjustDialog.h"

#include <QColor>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace dialogs {

namespace {

// PS B&W preset-neutral is 100% on each colour. Range -200..300 matches the
// AdjustmentPanel::BlackWhiteParams::rgbMixer (currently stored as 0..200),
// but to keep parity with the standalone dialog requested -200..300 we
// serialise plain integers here. Consumer is responsible for clamping.
const char* kBwColorNames[6] = {
    "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"
};

// Visual swatch colours approximating the PS B&W preset chips.
const char* kBwSwatchStyles[6] = {
    "background-color: #d62828;",   // Reds
    "background-color: #f4c430;",   // Yellows
    "background-color: #2e8b57;",   // Greens
    "background-color: #00b7c7;",   // Cyans
    "background-color: #2860d6;",   // Blues
    "background-color: #c427a3;"    // Magentas
};

} // namespace

BlackWhiteAdjustDialog::BlackWhiteAdjustDialog(const QVariantMap& args, QWidget* parent)
    : AdjustDialogBase(QString("Black & White (P0-3.2 v2)"), parent)
{
    setInitialArgs(args);
    init();

    // Restore per-color values
    for (int i = 0; i < kColorCount; ++i) {
        const QString key = QString("color%1").arg(i);
        if (args.contains(key)) {
            m_colorSliders[i]->setValue(args.value(key).toInt());
        }
    }
    if (args.contains(QString("tintHue"))) {
        m_tintHue->setValue(args.value(QString("tintHue")).toInt());
    }
    if (args.contains(QString("tintSat"))) {
        m_tintSat->setValue(args.value(QString("tintSat")).toInt());
    }
}

AdjustDialogBase* BlackWhiteAdjustDialog::create(const QVariantMap& args, QWidget* parent)
{
    return new BlackWhiteAdjustDialog(args, parent);
}

void BlackWhiteAdjustDialog::setupUi(QVBoxLayout* body)
{
    // Colour mixer group
    auto* mixGroup = new QGroupBox(QString("Color Mixer"), this);
    auto* mixLayout = new QVBoxLayout(mixGroup);
    mixLayout->setContentsMargins(8, 8, 8, 8);
    mixLayout->setSpacing(4);

    m_colorSliders.resize(kColorCount);
    m_colorLabels.resize(kColorCount);

    for (int i = 0; i < kColorCount; ++i) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);

        // Colour swatch (16x16, fixed)
        auto* swatch = new QLabel(this);
        swatch->setFixedSize(16, 16);
        swatch->setStyleSheet(
            QString("border: 1px solid #888;") + QString(kBwSwatchStyles[i]));
        row->addWidget(swatch);

        // Name label (fixed width)
        auto* nameLbl = new QLabel(QString(kBwColorNames[i]), this);
        nameLbl->setMinimumWidth(70);
        row->addWidget(nameLbl);

        // Slider (-200..300, default 100)
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-200, 300);
        slider->setValue(100);
        row->addWidget(slider, 1);
        m_colorSliders[i] = slider;

        // Value label
        auto* valLbl = new QLabel(QString("100"), this);
        valLbl->setMinimumWidth(36);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(valLbl);
        m_colorLabels[i] = valLbl;

        // Per-row live label update (without going through onSliderChanged
        // immediately, so dragging is responsive).
        connect(slider, &QSlider::valueChanged, this, [valLbl](int v) {
            valLbl->setText(QString::number(v));
        });

        mixLayout->addLayout(row);
    }

    body->addWidget(mixGroup);

    // Tint group
    auto* tintGroup = new QGroupBox(QString("Tint"), this);
    auto* tintLayout = new QFormLayout(tintGroup);
    tintLayout->setLabelAlignment(Qt::AlignRight);
    tintLayout->setHorizontalSpacing(8);

    {
        m_tintHue = new QSlider(Qt::Horizontal, this);
        m_tintHue->setRange(0, 360);
        m_tintHue->setValue(0);
        m_tintHueVal = new QLabel(QString("0"), this);
        m_tintHueVal->setMinimumWidth(40);
        m_tintHueVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* row = new QHBoxLayout;
        row->addWidget(m_tintHue, 1);
        row->addWidget(m_tintHueVal);
        tintLayout->addRow(QString("Hue:"), row);
    }
    {
        m_tintSat = new QSlider(Qt::Horizontal, this);
        m_tintSat->setRange(0, 100);
        m_tintSat->setValue(0);
        m_tintSatVal = new QLabel(QString("0"), this);
        m_tintSatVal->setMinimumWidth(40);
        m_tintSatVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* row = new QHBoxLayout;
        row->addWidget(m_tintSat, 1);
        row->addWidget(m_tintSatVal);
        tintLayout->addRow(QString("Saturation:"), row);
    }

    body->addWidget(tintGroup);

    // Reset button (sends all 6 colors back to 100, tints to 0)
    m_resetBtn = new QPushButton(QString("Reset"), this);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(m_resetBtn);
    body->addLayout(btnRow);

    body->addStretch(1);

    // Global wire: any slider change -> onSliderChanged
    for (QSlider* s : m_colorSliders) {
        connect(s, &QSlider::valueChanged, this, &BlackWhiteAdjustDialog::onSliderChanged);
    }
    connect(m_tintHue, &QSlider::valueChanged, this, [this](int v) {
        m_tintHueVal->setText(QString::number(v));
        onSliderChanged();
    });
    connect(m_tintSat, &QSlider::valueChanged, this, [this](int v) {
        m_tintSatVal->setText(QString::number(v));
        onSliderChanged();
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        for (QSlider* s : m_colorSliders) s->setValue(100);
        m_tintHue->setValue(0);
        m_tintSat->setValue(0);
        // onSliderChanged will be emitted by individual valueChanged signals.
    });
}

void BlackWhiteAdjustDialog::onSliderChanged()
{
    for (int i = 0; i < kColorCount; ++i) {
        updateParam(QString("color%1").arg(i), m_colorSliders[i]->value());
    }
    updateParam(QString("tintHue"), m_tintHue->value());
    updateParam(QString("tintSat"), m_tintSat->value());
    triggerPreview();
}

void BlackWhiteAdjustDialog::applyAdjust()
{
    AdjustDialogBase::applyAdjust();
}

} // namespace dialogs
