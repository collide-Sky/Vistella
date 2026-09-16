#include "MaskOptionsPanel.h"

#include "MaskBrushTool.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tools {

MaskOptionsPanel::MaskOptionsPanel(MaskBrushTool* tool, QWidget* parent)
    : QWidget(parent), m_tool(tool) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto addRow = [&](const QString& label, QSlider** slider,
                      QSpinBox** spin, int min, int max, int init) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        *slider = new QSlider(Qt::Horizontal, this);
        (*slider)->setRange(min, max);
        (*slider)->setValue(init);
        row->addWidget(*slider, 1);
        *spin = new QSpinBox(this);
        (*spin)->setRange(min, max);
        (*spin)->setValue(init);
        row->addWidget(*spin);
        root->addLayout(row);
    };

    addRow(tr("Size:"),     &m_size,     &m_sizeSpin,     1, 500, 50);
    addRow(tr("Hardness:"), &m_hardness, &m_hardnessSpin, 0, 100, 70);
    addRow(tr("Opacity:"),  &m_opacity,  &m_opacitySpin,  0, 100, 100);
    addRow(tr("Flow:"),     &m_flow,     &m_flowSpin,     0, 100, 100);

    auto* modeRow = new QHBoxLayout;
    m_revealBtn = new QPushButton(tr("Reveal"), this);
    m_revealBtn->setCheckable(true);
    m_revealBtn->setChecked(true);
    m_protectBtn = new QPushButton(tr("Protect"), this);
    m_protectBtn->setCheckable(true);
    modeRow->addWidget(m_revealBtn);
    modeRow->addWidget(m_protectBtn);
    modeRow->addStretch(1);
    root->addLayout(modeRow);

    root->addStretch(1);

    // Wire signals
    connect(m_size, &QSlider::valueChanged, this, &MaskOptionsPanel::onSizeChanged);
    connect(m_sizeSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MaskOptionsPanel::onSizeChanged);
    connect(m_hardness, &QSlider::valueChanged,
            this, &MaskOptionsPanel::onHardnessChanged);
    connect(m_hardnessSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MaskOptionsPanel::onHardnessChanged);
    connect(m_opacity, &QSlider::valueChanged,
            this, &MaskOptionsPanel::onOpacityChanged);
    connect(m_opacitySpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MaskOptionsPanel::onOpacityChanged);
    connect(m_flow, &QSlider::valueChanged, this, &MaskOptionsPanel::onFlowChanged);
    connect(m_flowSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MaskOptionsPanel::onFlowChanged);

    // Mode buttons: exclusive.
    connect(m_revealBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            m_protectBtn->setChecked(false);
            onModeToggled();
        }
    });
    connect(m_protectBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            m_revealBtn->setChecked(false);
            onModeToggled();
        }
    });
}

void MaskOptionsPanel::onSizeChanged(int v) {
    if (!m_tool) return;
    m_size->blockSignals(true);
    m_size->setValue(v);
    m_size->blockSignals(false);
    m_sizeSpin->blockSignals(true);
    m_sizeSpin->setValue(v);
    m_sizeSpin->blockSignals(false);
    m_tool->setBrushSize(v);
}

void MaskOptionsPanel::onHardnessChanged(int v) {
    if (!m_tool) return;
    m_hardness->blockSignals(true);
    m_hardness->setValue(v);
    m_hardness->blockSignals(false);
    m_hardnessSpin->blockSignals(true);
    m_hardnessSpin->setValue(v);
    m_hardnessSpin->blockSignals(false);
    m_tool->setHardness(v / qreal(100));
}

void MaskOptionsPanel::onOpacityChanged(int v) {
    if (!m_tool) return;
    m_opacity->blockSignals(true);
    m_opacity->setValue(v);
    m_opacity->blockSignals(false);
    m_opacitySpin->blockSignals(true);
    m_opacitySpin->setValue(v);
    m_opacitySpin->blockSignals(false);
    m_tool->setOpacity(v / qreal(100));
}

void MaskOptionsPanel::onFlowChanged(int v) {
    if (!m_tool) return;
    m_flow->blockSignals(true);
    m_flow->setValue(v);
    m_flow->blockSignals(false);
    m_flowSpin->blockSignals(true);
    m_flowSpin->setValue(v);
    m_flowSpin->blockSignals(false);
    // Flow currently mirrors opacity (PS distinction not yet implemented).
    m_tool->setOpacity(v / qreal(100));
}

void MaskOptionsPanel::onModeToggled() {
    if (!m_tool) return;
    m_tool->setMode(m_revealBtn->isChecked()
                    ? MaskBrushTool::Reveal
                    : MaskBrushTool::Hide);
}

}  // namespace tools