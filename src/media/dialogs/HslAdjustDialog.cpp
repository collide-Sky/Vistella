// SPDX-License-Identifier: MIT
//
// HslAdjustDialog implementation - F-K (2026-09-09)
//
#include "HslAdjustDialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

namespace dialogs {

HslAdjustDialog::HslAdjustDialog(const QVariantMap& args, QWidget* parent)
    : AdjustDialogBase("HSL 调整", parent)
{
    setInitialArgs(args);
    // F-K (2026-09-09): base ctor 不调 setupUi (避免 pure virtual 链接错), derived 调 init() 触发
    init();
    // 默认值 (-1 表示 "未设置"): 从 m_args 读
    const int hue  = args.value("hue", 0).toInt();
    const int sat  = args.value("sat", 0).toInt();
    const int light = args.value("light", 0).toInt();
    m_hueSlider->setValue(hue);
    m_satSlider->setValue(sat);
    m_lightSlider->setValue(light);
}

AdjustDialogBase* HslAdjustDialog::create(const QVariantMap& args, QWidget* parent)
{
    return new HslAdjustDialog(args, parent);
}

void HslAdjustDialog::setupUi(QVBoxLayout* body)
{
    QFormLayout* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);

    auto makeRow = [&](const QString& label, int min, int max, int val, QSlider*& s, QLabel*& l) {
        s = new QSlider(Qt::Horizontal, this);
        s->setRange(min, max);
        s->setValue(val);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval((max - min) / 4);
        l = new QLabel(QString::number(val), this);
        l->setMinimumWidth(40);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(s, &QSlider::valueChanged, this, [this, s, l]() {
            l->setText(QString::number(s->value()));
            onSliderChanged();
        });
        QHBoxLayout* row = new QHBoxLayout;
        row->addWidget(s, 1);
        row->addWidget(l);
        form->addRow(label, row);
    };

    makeRow("色相 (Hue):",         -180, 180, 0, m_hueSlider,  m_hueLabel);
    makeRow("饱和度 (Saturation):", -100, 100, 0, m_satSlider,  m_satLabel);
    makeRow("明度 (Lightness):",    -100, 100, 0, m_lightSlider, m_lightLabel);

    body->addLayout(form);
    body->addStretch(1);
}

void HslAdjustDialog::onSliderChanged()
{
    updateParam("hue",   m_hueSlider->value());
    updateParam("sat",   m_satSlider->value());
    updateParam("light", m_lightSlider->value());
    triggerPreview();
}

void HslAdjustDialog::applyAdjust()
{
    // 基类 default: emit applied(args)
    AdjustDialogBase::applyAdjust();
}

} // namespace dialogs
