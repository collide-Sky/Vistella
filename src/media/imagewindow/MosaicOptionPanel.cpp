// SPDX-License-Identifier: MIT
//
// MosaicOptionPanel implementation - Q4.2.2 (2026-09-24)
//
//   NOTE: include order matters - MosaicTool.h uses QPointer<ImageWindow>
//   which requires the full ImageWindow type (see qpointer.h:76
//   static_cast<QObject*>->ImageWindow*). Include imagewindow.h first.
//
#include "imagewindow.h"
#include "MosaicOptionPanel.h"
#include "MosaicTool.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

MosaicOptionPanel::MosaicOptionPanel(MosaicTool* tool, QWidget* parent)
    : QWidget(parent), m_tool(tool)
{
    setupUi();
    if (m_tool) {
        // MosaicTool signals - keep UI in sync when tool mutates its own
        // state (e.g. setEnabled from outside, undo/redo restoring mode).
        connect(m_tool, &MosaicTool::modeChanged,
                this, &MosaicOptionPanel::onToolModeChanged);
        connect(m_tool, &MosaicTool::sizeChanged,
                this, &MosaicOptionPanel::onToolSizeChanged);
        connect(m_tool, &MosaicTool::typeChanged,
                this, &MosaicOptionPanel::onToolTypeChanged);
    }
    refreshFromTool();
}

MosaicOptionPanel::~MosaicOptionPanel() = default;

void MosaicOptionPanel::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Brush size row: slider + spinbox + "px" label (PS style).
    auto* sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(tr("笔刷大小:"), this));
    m_sizeSlider = new QSlider(Qt::Horizontal, this);
    m_sizeSlider->setRange(5, 120);
    m_sizeSlider->setValue(30);
    sizeRow->addWidget(m_sizeSlider, 1);
    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setRange(5, 120);
    m_sizeSpin->setValue(30);
    m_sizeSpin->setSuffix(QStringLiteral(" px"));
    sizeRow->addWidget(m_sizeSpin);
    m_sizeLabel = new QLabel(QStringLiteral("30 px"), this);
    m_sizeLabel->setMinimumWidth(48);
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeRow->addWidget(m_sizeLabel);
    root->addLayout(sizeRow);

    // 4 mosaic types combo (PS style: Pixelate / Blur / Blackout / Tiles).
    auto* typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel(tr("涂抹类型:"), this));
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("马赛克 (Pixelate)"),   static_cast<int>(MosaicTool::Pixelate));
    m_typeCombo->addItem(tr("模糊 (Blur)"),          static_cast<int>(MosaicTool::Blur));
    m_typeCombo->addItem(tr("涂黑 (Blackout)"),      static_cast<int>(MosaicTool::Blackout));
    m_typeCombo->addItem(tr("网格 (Tiles)"),         static_cast<int>(MosaicTool::Tiles));
    typeRow->addWidget(m_typeCombo, 1);
    root->addLayout(typeRow);

    // Mosaic mode toggle button (PS style: enter / exit mosaic mode).
    m_modeBtn = new QToolButton(this);
    m_modeBtn->setText(tr("● 进入涂抹模式 (再点退出)"));
    m_modeBtn->setCheckable(true);
    m_modeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    root->addWidget(m_modeBtn);

    // Status hint (migrated from original "lblMosaicHint").
    m_hintLabel = new QLabel(tr("(点上方按钮进入模式, 然后在图上拖动)"), this);
    m_hintLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    m_hintLabel->setWordWrap(true);
    root->addWidget(m_hintLabel);

    root->addStretch(1);

    // Bidirectional binding.
    connect(m_sizeSlider, &QSlider::valueChanged,
            this, &MosaicOptionPanel::onSizeSliderChanged);
    // Q4.2.2: Qt 6 qOverload<int>::of fails to compile
    //   (error C3861: 'of' not found), use static_cast to disambiguate
    //   the overload instance - Qt 6 official recommendation.
    using IntSpinSignal = void (QSpinBox::*)(int);
    using IntComboSignal = void (QComboBox::*)(int);
    connect(m_sizeSpin, static_cast<IntSpinSignal>(&QSpinBox::valueChanged),
            this, [this](int v) {
                if (m_suspend) return;
                m_sizeSlider->setValue(v);
                if (m_tool) m_tool->setSize(v);
            });
    connect(m_modeBtn, &QToolButton::toggled,
            this, &MosaicOptionPanel::onModeButtonToggled);
    connect(m_typeCombo, static_cast<IntComboSignal>(&QComboBox::currentIndexChanged),
            this, &MosaicOptionPanel::onTypeComboChanged);
}

void MosaicOptionPanel::refreshFromTool()
{
    if (!m_tool) return;
    QSignalBlocker blockA(m_sizeSlider);
    QSignalBlocker blockB(m_sizeSpin);
    QSignalBlocker blockC(m_typeCombo);
    QSignalBlocker blockD(m_modeBtn);
    m_suspend = true;

    const int sz = m_tool->size();
    m_sizeSlider->setValue(sz);
    m_sizeSpin->setValue(sz);
    m_sizeLabel->setText(QStringLiteral("%1 px").arg(sz));
    m_typeCombo->setCurrentIndex(static_cast<int>(m_tool->type()));
    const bool on = m_tool->isEnabled();
    m_modeBtn->setChecked(on);
    m_modeBtn->setText(on ? tr("✓ 涂抹中 (再点退出)")
                          : tr("● 进入涂抹模式 (再点退出)"));

    m_suspend = false;
}

void MosaicOptionPanel::onSizeSliderChanged(int v)
{
    if (m_suspend) return;
    m_suspend = true;
    m_sizeSpin->setValue(v);
    m_sizeLabel->setText(QStringLiteral("%1 px").arg(v));
    m_suspend = false;
    if (m_tool) m_tool->setSize(v);
}

void MosaicOptionPanel::onModeButtonToggled(bool on)
{
    if (m_suspend) return;
    if (m_tool) m_tool->setEnabled(on);
    // Actual button text/state synced by onToolModeChanged to avoid
    // overwriting tool internal state.
}

void MosaicOptionPanel::onTypeComboChanged(int idx)
{
    if (m_suspend) return;
    if (m_tool) m_tool->setType(static_cast<MosaicTool::Type>(idx));
}

void MosaicOptionPanel::onToolModeChanged(bool on)
{
    QSignalBlocker block(m_modeBtn);
    m_modeBtn->setChecked(on);
    m_modeBtn->setText(on ? tr("✓ 涂抹中 (再点退出)")
                          : tr("● 进入涂抹模式 (再点退出)"));
}

void MosaicOptionPanel::onToolSizeChanged(int radius)
{
    QSignalBlocker blockA(m_sizeSlider);
    QSignalBlocker blockB(m_sizeSpin);
    m_sizeSlider->setValue(radius);
    m_sizeSpin->setValue(radius);
    m_sizeLabel->setText(QStringLiteral("%1 px").arg(radius));
}

void MosaicOptionPanel::onToolTypeChanged(int type)
{
    QSignalBlocker block(m_typeCombo);
    m_typeCombo->setCurrentIndex(type);
}
