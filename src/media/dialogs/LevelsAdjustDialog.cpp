// SPDX-License-Identifier: MIT
//
// LevelsAdjustDialog implementation - P0-3.2 v2 (2026-09-18)
//
#include "LevelsAdjustDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace dialogs {

LevelsAdjustDialog::LevelsAdjustDialog(const QVariantMap& args, QWidget* parent)
    : AdjustDialogBase(QString("Levels (P0-3.2 v2)"), parent)
{
    setInitialArgs(args);
    init();

    // Restore initial args
    const QString chIn = args.value(QString("channel")).toString();
    if (!chIn.isEmpty()) {
        m_channel = chIn;
        const int idx = m_channelBox->findText(m_channel);
        if (idx >= 0) m_channelBox->setCurrentIndex(idx);
    } else {
        m_channelBox->setCurrentText(QString("RGB"));
    }
    if (args.contains(QString("inLow"))) {
        m_inLowSlider->setValue(args.value(QString("inLow")).toInt());
    }
    if (args.contains(QString("inHigh"))) {
        m_inHighSlider->setValue(args.value(QString("inHigh")).toInt());
    }
    if (args.contains(QString("gamma"))) {
        m_gammaSlider->setValue(
            static_cast<int>(args.value(QString("gamma")).toDouble() * 100.0));
    }
    if (args.contains(QString("outLow"))) {
        m_outLowSlider->setValue(args.value(QString("outLow")).toInt());
    }
    if (args.contains(QString("outHigh"))) {
        m_outHighSlider->setValue(args.value(QString("outHigh")).toInt());
    }
    updateLabels();
}

AdjustDialogBase* LevelsAdjustDialog::create(const QVariantMap& args, QWidget* parent)
{
    return new LevelsAdjustDialog(args, parent);
}

void LevelsAdjustDialog::setupUi(QVBoxLayout* body)
{
    // Channel selector (top)
    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(new QLabel(QString("Channel:"), this));
    m_channelBox = new QComboBox(this);
    m_channelBox->addItem(QString("RGB"));
    m_channelBox->addItem(QString("Red"));
    m_channelBox->addItem(QString("Green"));
    m_channelBox->addItem(QString("Blue"));
    m_channelBox->setCurrentText(QString("RGB"));
    topRow->addWidget(m_channelBox);
    topRow->addStretch(1);
    body->addLayout(topRow);

    // Input group: inLow / inHigh / gamma
    auto* inGroup = new QGroupBox(QString("Input Levels"), this);
    auto* inForm  = new QFormLayout(inGroup);
    inForm->setLabelAlignment(Qt::AlignRight);
    inForm->setHorizontalSpacing(8);
    inForm->setVerticalSpacing(4);

    m_inLowSlider  = new QSlider(Qt::Horizontal, inGroup);
    m_inLowSlider->setRange(0, 254);   // strictly less than inHigh
    m_inLowSlider->setValue(0);
    m_inLowVal     = new QLabel(QString("0"), inGroup);
    m_inLowVal->setMinimumWidth(40);
    m_inLowVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(m_inLowSlider, 1);
        row->addWidget(m_inLowVal);
        inForm->addRow(QString("Black (inLow):"), row);
    }

    m_inHighSlider = new QSlider(Qt::Horizontal, inGroup);
    m_inHighSlider->setRange(1, 255);  // strictly greater than inLow
    m_inHighSlider->setValue(255);
    m_inHighVal    = new QLabel(QString("255"), inGroup);
    m_inHighVal->setMinimumWidth(40);
    m_inHighVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(m_inHighSlider, 1);
        row->addWidget(m_inHighVal);
        inForm->addRow(QString("White (inHigh):"), row);
    }

    m_gammaSlider  = new QSlider(Qt::Horizontal, inGroup);
    m_gammaSlider->setRange(10, 300);  // mapped to gamma = v / 100
    m_gammaSlider->setValue(100);      // gamma = 1.00
    m_gammaVal     = new QLabel(QString("1.00"), inGroup);
    m_gammaVal->setMinimumWidth(40);
    m_gammaVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(m_gammaSlider, 1);
        row->addWidget(m_gammaVal);
        inForm->addRow(QString("Gamma:"), row);
    }

    body->addWidget(inGroup);

    // Output group: outLow / outHigh
    auto* outGroup = new QGroupBox(QString("Output Levels"), this);
    auto* outForm  = new QFormLayout(outGroup);
    outForm->setLabelAlignment(Qt::AlignRight);
    outForm->setHorizontalSpacing(8);
    outForm->setVerticalSpacing(4);

    m_outLowSlider = new QSlider(Qt::Horizontal, outGroup);
    m_outLowSlider->setRange(0, 255);
    m_outLowSlider->setValue(0);
    m_outLowVal    = new QLabel(QString("0"), outGroup);
    m_outLowVal->setMinimumWidth(40);
    m_outLowVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(m_outLowSlider, 1);
        row->addWidget(m_outLowVal);
        outForm->addRow(QString("Black (outLow):"), row);
    }

    m_outHighSlider = new QSlider(Qt::Horizontal, outGroup);
    m_outHighSlider->setRange(0, 255);
    m_outHighSlider->setValue(255);
    m_outHighVal    = new QLabel(QString("255"), outGroup);
    m_outHighVal->setMinimumWidth(40);
    m_outHighVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(m_outHighSlider, 1);
        row->addWidget(m_outHighVal);
        outForm->addRow(QString("White (outHigh):"), row);
    }

    body->addWidget(outGroup);
    body->addStretch(1);

    // Wire signals
    connect(m_inLowSlider,  &QSlider::valueChanged, this, &LevelsAdjustDialog::onSliderChanged);
    connect(m_inHighSlider, &QSlider::valueChanged, this, &LevelsAdjustDialog::onSliderChanged);
    connect(m_gammaSlider,  &QSlider::valueChanged, this, &LevelsAdjustDialog::onSliderChanged);
    connect(m_outLowSlider, &QSlider::valueChanged, this, &LevelsAdjustDialog::onSliderChanged);
    connect(m_outHighSlider, &QSlider::valueChanged, this, &LevelsAdjustDialog::onSliderChanged);
    connect(m_channelBox,
            qOverload<const QString&>(&QComboBox::currentTextChanged),
            this, &LevelsAdjustDialog::onChannelChanged);
}

void LevelsAdjustDialog::onSliderChanged()
{
    // Enforce inLow < inHigh: typical PS behaviour.
    if (m_inLowSlider->value() >= m_inHighSlider->value()) {
        // If user dragged inLow past inHigh, snap back inLow to inHigh - 1.
        const int newLow = std::max(0, m_inHighSlider->value() - 1);
        m_inLowSlider->blockSignals(true);
        m_inLowSlider->setValue(newLow);
        m_inLowSlider->blockSignals(false);
    }
    if (m_inHighSlider->value() <= m_inLowSlider->value()) {
        const int newHigh = std::min(255, m_inLowSlider->value() + 1);
        m_inHighSlider->blockSignals(true);
        m_inHighSlider->setValue(newHigh);
        m_inHighSlider->blockSignals(false);
    }

    updateLabels();

    updateParam(QString("inLow"),   m_inLowSlider->value());
    updateParam(QString("inHigh"),  m_inHighSlider->value());
    updateParam(QString("gamma"),   m_gammaSlider->value() / 100.0);
    updateParam(QString("outLow"),  m_outLowSlider->value());
    updateParam(QString("outHigh"), m_outHighSlider->value());

    triggerPreview();
}

void LevelsAdjustDialog::onChannelChanged(const QString& text)
{
    m_channel = text;
    updateParam(QString("channel"), m_channel);
    triggerPreview();
}

void LevelsAdjustDialog::updateLabels()
{
    m_inLowVal->setText(QString::number(m_inLowSlider->value()));
    m_inHighVal->setText(QString::number(m_inHighSlider->value()));
    m_gammaVal->setText(QString::number(m_gammaSlider->value() / 100.0, 'f', 2));
    m_outLowVal->setText(QString::number(m_outLowSlider->value()));
    m_outHighVal->setText(QString::number(m_outHighSlider->value()));
}

void LevelsAdjustDialog::applyAdjust()
{
    AdjustDialogBase::applyAdjust();
}

} // namespace dialogs
