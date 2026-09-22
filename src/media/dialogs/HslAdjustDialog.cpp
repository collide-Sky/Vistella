// SPDX-License-Identifier: MIT
//
// HslAdjustDialog implementation - F-K (2026-09-09) / P0 leftover review (2026-09-21)
//
#include "HslAdjustDialog.h"
#include "logger.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

namespace dialogs {

HslAdjustDialog::HslAdjustDialog(const QVariantMap& args, QWidget* parent)
    : AdjustDialogBase("HSL 调整", parent)
{
    setInitialArgs(args);
    init();
    setBandDefaults();
}

AdjustDialogBase* HslAdjustDialog::create(const QVariantMap& args, QWidget* parent)
{
    return new HslAdjustDialog(args, parent);
}

void HslAdjustDialog::setBandDefaults()
{
    // Pull defaults from args if present (PS: each band defaults to 0).
    QVariantList hueList;
    QVariantList satList;
    if (m_args.contains("hueShifts")) hueList = m_args["hueShifts"].toList();
    if (m_args.contains("satShifts")) satList = m_args["satShifts"].toList();
    const int light = m_args.value("lightness", 0).toInt();

    for (int i = 0; i < kBandCount; ++i) {
        const int hue = (i < hueList.size()) ? hueList[i].toInt() : 0;
        const int sat = (i < satList.size()) ? satList[i].toInt() : 0;
        if (i < m_hueSliders.size() && m_hueSliders[i]) m_hueSliders[i]->setValue(hue);
        if (i < m_satSliders.size() && m_satSliders[i]) m_satSliders[i]->setValue(sat);
    }
    if (m_lightSlider) m_lightSlider->setValue(light);
    syncLabels();
}

void HslAdjustDialog::syncLabels()
{
    for (int i = 0; i < kBandCount; ++i) {
        if (i < m_hueSliders.size() && m_hueSliders[i] && i < m_hueLabels.size() && m_hueLabels[i]) {
            m_hueLabels[i]->setText(QString::number(m_hueSliders[i]->value()));
        }
        if (i < m_satSliders.size() && m_satSliders[i] && i < m_satLabels.size() && m_satLabels[i]) {
            m_satLabels[i]->setText(QString::number(m_satSliders[i]->value()));
        }
    }
    if (m_lightSlider && m_lightLabel) {
        m_lightLabel->setText(QString::number(m_lightSlider->value()));
    }
}

void HslAdjustDialog::onSliderChanged(int /*band*/)
{
    syncLabels();
    // PS behaviour: live preview emits preview() on every slider change.
    //   P0 leftover review (2026-09-21): use updateParam + triggerPreview
    //   so AdjustDialogBase marks m_modified = true (enables Apply button).
    updateParam("hueShifts", currentHueShifts());
    updateParam("satShifts", currentSatShifts());
    updateParam("lightness", m_lightSlider ? m_lightSlider->value() : 0);
    triggerPreview();
}

QVariantList HslAdjustDialog::currentHueShifts() const
{
    QVariantList list;
    for (int i = 0; i < kBandCount; ++i) {
        list << (i < m_hueSliders.size() && m_hueSliders[i]
                 ? m_hueSliders[i]->value() : 0);
    }
    return list;
}

QVariantList HslAdjustDialog::currentSatShifts() const
{
    QVariantList list;
    for (int i = 0; i < kBandCount; ++i) {
        list << (i < m_satSliders.size() && m_satSliders[i]
                 ? m_satSliders[i]->value() : 0);
    }
    return list;
}

void HslAdjustDialog::setupUi(QVBoxLayout* body)
{
    QFormLayout* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(4);

    // PS-style HSL band names: master + R, Y, G, C, B, M (7 + master = 8).
    static const char* kBandNames[kBandCount] = {
        "Master:", "Reds:", "Yellows:", "Greens:",
        "Cyans:", "Blues:", "Magentas:", "Whites:"
    };

    auto makeRow = [&](const QString &label, int min, int max, int val,
                       QSlider* &s, QLabel* &l) {
        s = new QSlider(Qt::Horizontal, this);
        s->setRange(min, max);
        s->setValue(val);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval((max - min) / 4);
        l = new QLabel(QString::number(val), this);
        l->setMinimumWidth(36);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QHBoxLayout* row = new QHBoxLayout;
        row->addWidget(s, 1);
        row->addWidget(l);
        form->addRow(label, row);
    };

    m_hueSliders.reserve(kBandCount);
    m_hueLabels.reserve(kBandCount);
    m_satSliders.reserve(kBandCount);
    m_satLabels.reserve(kBandCount);

    for (int i = 0; i < kBandCount; ++i) {
        QSlider* hue = nullptr;
        QLabel* hueLabel = nullptr;
        QSlider* sat = nullptr;
        QLabel* satLabel = nullptr;
        makeRow(QString::fromUtf8(kBandNames[i]) + QStringLiteral(" 色相"),
                -180, 180, 0, hue, hueLabel);
        makeRow(QString::fromUtf8(kBandNames[i]) + QStringLiteral(" 饱和"),
                -100, 100, 0, sat, satLabel);
        connect(hue, &QSlider::valueChanged, this, [this, i](int) { onSliderChanged(i); });
        connect(sat, &QSlider::valueChanged, this, [this, i](int) { onSliderChanged(i); });
        m_hueSliders.push_back(hue);
        m_hueLabels.push_back(hueLabel);
        m_satSliders.push_back(sat);
        m_satLabels.push_back(satLabel);
    }

    QSlider* lightSlider = nullptr;
    QLabel* lightLabel = nullptr;
    makeRow(QStringLiteral("明度 (Lightness):"), -100, 100, 0, lightSlider, lightLabel);
    connect(lightSlider, &QSlider::valueChanged, this, [this](int) { onSliderChanged(-1); });
    m_lightSlider = lightSlider;
    m_lightLabel = lightLabel;

    body->addLayout(form);
    body->addStretch(1);
}

void HslAdjustDialog::applyAdjust()
{
    // updateParam was already called for each slider change (sets m_modified);
    //   m_args is now the latest payload. Just emit applied().
    AdjustDialogBase::applied(m_args);
}

} // namespace dialogs
