// SPDX-License-Identifier: MIT
//
// ChannelMixerAdjustDialog implementation - P0-3.2 v2 (2026-09-18)
//
#include "ChannelMixerAdjustDialog.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace dialogs {

namespace {

// Default channel mixer matrix - PS identity: each output = its own source.
// Using 200 to keep parity with the AdjustmentPanel ChannelMixerParams
// (where 200 == 100% retention). Range is -200..200 here.
int kDefaultMatrix[3][3] = {
    { 200, 0,   0   },
    { 0,   200, 0   },
    { 0,   0,   200 },
};

const char* kOutChannelNames[3] = { "R out", "G out", "B out" };
const char* kInChannelNames[3]  = { "R in",  "G in",  "B in"  };

} // namespace

ChannelMixerAdjustDialog::ChannelMixerAdjustDialog(const QVariantMap& args, QWidget* parent)
    : AdjustDialogBase(QString("Channel Mixer (P0-3.2 v2)"), parent)
{
    setInitialArgs(args);
    init();

    // Restore initial matrix
    for (int outIdx = 0; outIdx < kChannelCount; ++outIdx) {
        for (int srcIdx = 0; srcIdx < kChannelCount; ++srcIdx) {
            const QString key = QString("m%1%2").arg(outIdx).arg(srcIdx);
            if (args.contains(key)) {
                m_sliders[idx(outIdx, srcIdx)]->setValue(args.value(key).toInt());
            }
        }
    }
    if (args.contains(QString("monochrome"))) {
        m_monoCheck->setChecked(args.value(QString("monochrome")).toBool());
    }
}

AdjustDialogBase* ChannelMixerAdjustDialog::create(const QVariantMap& args, QWidget* parent)
{
    return new ChannelMixerAdjustDialog(args, parent);
}

void ChannelMixerAdjustDialog::setupUi(QVBoxLayout* body)
{
    m_sliders.resize(kSliderCount);
    m_valueLabels.resize(kSliderCount);

    auto* matrixGroup = new QGroupBox(QString("3 x 3 Channel Mixer"), this);
    auto* grid = new QGridLayout(matrixGroup);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);

    // Header row: source channel columns (row 0)
    grid->addWidget(new QLabel(QString(""), this), 0, 0);
    for (int s = 0; s < kChannelCount; ++s) {
        auto* h = new QLabel(QString(kInChannelNames[s]), this);
        h->setAlignment(Qt::AlignCenter);
        grid->addWidget(h, 0, s + 1);
    }

    // 3 output rows (rows 1..3)
    for (int outIdx = 0; outIdx < kChannelCount; ++outIdx) {
        auto* rowHdr = new QLabel(QString(kOutChannelNames[outIdx]), this);
        rowHdr->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(rowHdr, outIdx + 1, 0);

        for (int srcIdx = 0; srcIdx < kChannelCount; ++srcIdx) {
            // Compact: slider above the value label, both in the same cell.
            auto* cell = new QWidget(this);
            auto* cellLayout = new QVBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(0);

            auto* s = new QSlider(Qt::Horizontal, cell);
            s->setRange(-200, 200);
            s->setValue(kDefaultMatrix[outIdx][srcIdx]);
            m_sliders[idx(outIdx, srcIdx)] = s;

            auto* v = new QLabel(QString::number(s->value()), cell);
            v->setAlignment(Qt::AlignCenter);
            v->setMinimumWidth(36);
            m_valueLabels[idx(outIdx, srcIdx)] = v;

            cellLayout->addWidget(s);
            cellLayout->addWidget(v);

            connect(s, &QSlider::valueChanged, this, [this, v](int value) {
                v->setText(QString::number(value));
                onSliderChanged();
            });

            grid->addWidget(cell, outIdx + 1, srcIdx + 1);
        }
    }

    body->addWidget(matrixGroup);

    // Monochrome + Reset row
    auto* ctrlRow = new QHBoxLayout;
    m_monoCheck = new QCheckBox(QString("Monochrome"), this);
    ctrlRow->addWidget(m_monoCheck);
    ctrlRow->addStretch(1);

    m_resetBtn = new QPushButton(QString("Reset"), this);
    ctrlRow->addWidget(m_resetBtn);
    body->addLayout(ctrlRow);

    body->addStretch(1);

    // Signals
    connect(m_monoCheck, &QCheckBox::toggled,
            this, &ChannelMixerAdjustDialog::onMonochromeToggled);
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        for (int outIdx = 0; outIdx < kChannelCount; ++outIdx) {
            for (int srcIdx = 0; srcIdx < kChannelCount; ++srcIdx) {
                m_sliders[idx(outIdx, srcIdx)]->setValue(kDefaultMatrix[outIdx][srcIdx]);
            }
        }
        // onSliderChanged triggered by individual valueChanged handlers.
    });
}

void ChannelMixerAdjustDialog::onSliderChanged()
{
    for (int outIdx = 0; outIdx < kChannelCount; ++outIdx) {
        for (int srcIdx = 0; srcIdx < kChannelCount; ++srcIdx) {
            updateParam(QString("m%1%2").arg(outIdx).arg(srcIdx),
                        m_sliders[idx(outIdx, srcIdx)]->value());
        }
    }
    triggerPreview();
}

void ChannelMixerAdjustDialog::onMonochromeToggled(bool checked)
{
    updateParam(QString("monochrome"), checked);
    triggerPreview();
}

void ChannelMixerAdjustDialog::applyAdjust()
{
    AdjustDialogBase::applyAdjust();
}

} // namespace dialogs
