// SPDX-License-Identifier: MIT
//
// ToolOptionsPanel implementation
//
// See ToolOptionsPanel.h for the why.
#include "ToolOptionsPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tools {

namespace {

// A labelled slider with a spin box next to it.
void addSliderRow(QFormLayout* form, const QString& label, QSlider*& slider,
                  QSpinBox*& spin, int min, int max, int value)
{
    slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(value);

    spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(value);

    QWidget* row = new QWidget;
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(slider, 1);
    rowLayout->addWidget(spin);

    form->addRow(label, row);
}

} // namespace

ToolOptionsPanel::ToolOptionsPanel(Kind kind, QWidget* parent)
    : QWidget(parent), m_kind(kind)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    auto* form = new QFormLayout;
    outer->addLayout(form);

    switch (m_kind) {
    case Kind::PaintBucket:
        addSliderRow(form, QStringLiteral("Tolerance"), m_tolSlider, m_tolSpin,
                     0, 255, 32);
        connect(m_tolSlider, &QSlider::valueChanged, this, &ToolOptionsPanel::toleranceChanged);
        connect(m_tolSpin, &QSpinBox::valueChanged, this, &ToolOptionsPanel::toleranceChanged);
        break;

    case Kind::Gradient:
        m_gradLabel = new QLabel(QStringLiteral("Type"));
        m_gradCombo = new QComboBox;
        m_gradCombo->addItems({QStringLiteral("Linear"),
                               QStringLiteral("Radial"),
                               QStringLiteral("Angular")});
        form->addRow(m_gradLabel, m_gradCombo);
        connect(m_gradCombo, &QComboBox::currentIndexChanged,
                this, &ToolOptionsPanel::gradientTypeChanged);
        break;

    case Kind::DodgeBurn:
        m_modeLabel = new QLabel(QStringLiteral("Mode"));
        m_modeCombo = new QComboBox;
        m_modeCombo->addItems({QStringLiteral("Dodge"), QStringLiteral("Burn")});
        form->addRow(m_modeLabel, m_modeCombo);
        connect(m_modeCombo, &QComboBox::currentIndexChanged,
                this, &ToolOptionsPanel::modeChanged);

        addSliderRow(form, QStringLiteral("Amount"), m_amountSlider, m_amountSpin,
                     1, 100, 50);
        connect(m_amountSlider, &QSlider::valueChanged, this, &ToolOptionsPanel::amountChanged);
        connect(m_amountSpin, &QSpinBox::valueChanged, this, &ToolOptionsPanel::amountChanged);
        break;

    case Kind::BlurSharpenSmudge:
        m_modeLabel = new QLabel(QStringLiteral("Mode"));
        m_modeCombo = new QComboBox;
        m_modeCombo->addItems({QStringLiteral("Blur"),
                               QStringLiteral("Sharpen"),
                               QStringLiteral("Smudge")});
        form->addRow(m_modeLabel, m_modeCombo);
        connect(m_modeCombo, &QComboBox::currentIndexChanged,
                this, &ToolOptionsPanel::modeChanged);

        addSliderRow(form, QStringLiteral("Strength"), m_amountSlider, m_amountSpin,
                     1, 100, 50);
        connect(m_amountSlider, &QSlider::valueChanged, this, &ToolOptionsPanel::strengthChanged);
        connect(m_amountSpin, &QSpinBox::valueChanged, this, &ToolOptionsPanel::strengthChanged);

        addSliderRow(form, QStringLiteral("Size"), m_sizeSlider, m_sizeSpin,
                     1, 500, 40);
        connect(m_sizeSlider, &QSlider::valueChanged, this, &ToolOptionsPanel::brushSizeChanged);
        connect(m_sizeSpin, &QSpinBox::valueChanged, this, &ToolOptionsPanel::brushSizeChanged);
        break;

    case Kind::Eraser:
        addSliderRow(form, QStringLiteral("Size"), m_sizeSlider, m_sizeSpin,
                     1, 500, 20);
        connect(m_sizeSlider, &QSlider::valueChanged, this, &ToolOptionsPanel::brushSizeChanged);
        connect(m_sizeSpin, &QSpinBox::valueChanged, this, &ToolOptionsPanel::brushSizeChanged);
        break;
    }
}

// ===== push the tool's state into the panel =====

void ToolOptionsPanel::setTolerance(int v)
{
    if (m_tolSlider) m_tolSlider->setValue(v);
    if (m_tolSpin) m_tolSpin->setValue(v);
}

void ToolOptionsPanel::setGradientType(int index)
{
    if (m_gradCombo) m_gradCombo->setCurrentIndex(index);
}

void ToolOptionsPanel::setMode(int index)
{
    if (m_modeCombo) m_modeCombo->setCurrentIndex(index);
}

void ToolOptionsPanel::setAmount(int v)
{
    if (m_amountSlider) m_amountSlider->setValue(v);
    if (m_amountSpin) m_amountSpin->setValue(v);
}

void ToolOptionsPanel::setStrength(int v)
{
    setAmount(v);
}

void ToolOptionsPanel::setBrushSize(int v)
{
    if (m_sizeSlider) m_sizeSlider->setValue(v);
    if (m_sizeSpin) m_sizeSpin->setValue(v);
}

// ===== read the panel's state =====

int ToolOptionsPanel::tolerance() const
{
    return m_tolSpin ? m_tolSpin->value() : 32;
}

int ToolOptionsPanel::gradientType() const
{
    return m_gradCombo ? m_gradCombo->currentIndex() : 0;
}

int ToolOptionsPanel::mode() const
{
    return m_modeCombo ? m_modeCombo->currentIndex() : 0;
}

int ToolOptionsPanel::amount() const
{
    return m_amountSpin ? m_amountSpin->value() : 50;
}

int ToolOptionsPanel::strength() const
{
    return amount();
}

int ToolOptionsPanel::brushSize() const
{
    return m_sizeSpin ? m_sizeSpin->value() : 20;
}

} // namespace tools
