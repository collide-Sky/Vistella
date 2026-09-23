// SPDX-License-Identifier: MIT
//
// CameraRawDialog implementation - P3.5 (2026-09-23)
//
#include "CameraRawDialog.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

namespace camera_raw {

CameraRawDialog::CameraRawDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Camera Raw"));
    setMinimumWidth(380);
    m_defaults = m_settings;     // default = PS 50/50 default
    setupUi();
    refreshLabels();
}

CameraRawDialog::~CameraRawDialog() = default;

void CameraRawDialog::setInitialSettings(const CameraRawSettings& s)
{
    m_settings = s;
    m_defaults = s;
    if (m_exposure)  m_exposure->setValue(s.exposure);
    if (m_wbTemp)    m_wbTemp->setValue(s.wbTemp);
    if (m_wbTint)    m_wbTint->setValue(s.wbTint);
    if (m_sharpness) m_sharpness->setValue(s.sharpness);
    if (m_noiseRedux)m_noiseRedux->setValue(s.noiseRedux);
    refreshLabels();
}

void CameraRawDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Status banner (libraw not configured? show)
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        hasLibRawSupport()
            ? QStringLiteral("color: gray;")
            : QStringLiteral("color: #b00; font-weight: bold;"));
    m_statusLabel->setText(hasLibRawSupport()
        ? QStringLiteral("状态: libraw OK, 可解码 RAW")
        : QStringLiteral("状态: libraw 未配置 — 解码需 vcpkg install libraw:x64-windows"));
    root->addWidget(m_statusLabel);

    // 5 sliders in a grid (label + slider + value)
    auto *grid = new QGridLayout;
    auto makeSlider = [&](int row, const QString& label, int &out) {
        QLabel *lab = new QLabel(label, this);
        QSlider *s = new QSlider(Qt::Horizontal, this);
        s->setRange(0, 100);
        s->setValue(out);
        QLabel *val = new QLabel(QStringLiteral("0"), this);
        val->setMinimumWidth(36);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(lab, row, 0);
        grid->addWidget(s,   row, 1);
        grid->addWidget(val, row, 2);
        s->setEnabled(hasLibRawSupport());
        return std::make_pair(s, val);
    };
    auto e = makeSlider(0, QStringLiteral("曝光:"),     m_settings.exposure);
    auto t = makeSlider(1, QStringLiteral("色温:"),     m_settings.wbTemp);
    auto i = makeSlider(2, QStringLiteral("色调:"),     m_settings.wbTint);
    auto s = makeSlider(3, QStringLiteral("锐度:"),     m_settings.sharpness);
    auto n = makeSlider(4, QStringLiteral("降噪:"),     m_settings.noiseRedux);
    m_exposure    = e.first;  m_exposureLab = e.second;
    m_wbTemp      = t.first;  m_wbTempLab   = t.second;
    m_wbTint      = i.first;  m_wbTintLab   = i.second;
    m_sharpness   = s.first;  m_sharpLab    = s.second;
    m_noiseRedux  = n.first;  m_noiseLab    = n.second;
    root->addLayout(grid);

    connect(m_exposure,   &QSlider::valueChanged, this, &CameraRawDialog::onExposureChanged);
    connect(m_wbTemp,     &QSlider::valueChanged, this, &CameraRawDialog::onWbTempChanged);
    connect(m_wbTint,     &QSlider::valueChanged, this, &CameraRawDialog::onWbTintChanged);
    connect(m_sharpness,  &QSlider::valueChanged, this, &CameraRawDialog::onSharpnessChanged);
    connect(m_noiseRedux, &QSlider::valueChanged, this, &CameraRawDialog::onNoiseReduxChanged);

    root->addStretch(1);

    auto *btnRow = new QHBoxLayout;
    m_resetBtn = new QPushButton(QStringLiteral("重置"), this);
    connect(m_resetBtn, &QPushButton::clicked, this, &CameraRawDialog::onResetClicked);
    btnRow->addWidget(m_resetBtn);

    btnRow->addStretch(1);

    m_applyBtn = new QPushButton(QStringLiteral("预览"), this);
    connect(m_applyBtn, &QPushButton::clicked, this, &CameraRawDialog::onApplyClicked);
    btnRow->addWidget(m_applyBtn);

    m_okBtn = new QPushButton(QStringLiteral("确定"), this);
    m_okBtn->setDefault(true);
    connect(m_okBtn, &QPushButton::clicked, this, &CameraRawDialog::onOkClicked);
    btnRow->addWidget(m_okBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &CameraRawDialog::onCancelClicked);
    btnRow->addWidget(m_cancelBtn);

    root->addLayout(btnRow);
}

void CameraRawDialog::refreshLabels()
{
    m_exposureLab->setText(QStringLiteral("%1").arg(m_settings.exposure));
    m_wbTempLab->setText(QStringLiteral("%1").arg(m_settings.wbTemp));
    m_wbTintLab->setText(QStringLiteral("%1").arg(m_settings.wbTint));
    m_sharpLab->setText(QStringLiteral("%1").arg(m_settings.sharpness));
    m_noiseLab->setText(QStringLiteral("%1").arg(m_settings.noiseRedux));
}

void CameraRawDialog::onExposureChanged(int v)  { m_settings.exposure   = v; m_exposureLab->setText(QString::number(v)); }
void CameraRawDialog::onWbTempChanged(int v)    { m_settings.wbTemp     = v; m_wbTempLab->setText(QString::number(v)); }
void CameraRawDialog::onWbTintChanged(int v)    { m_settings.wbTint     = v; m_wbTintLab->setText(QString::number(v)); }
void CameraRawDialog::onSharpnessChanged(int v) { m_settings.sharpness  = v; m_sharpLab->setText(QString::number(v)); }
void CameraRawDialog::onNoiseReduxChanged(int v){ m_settings.noiseRedux = v; m_noiseLab->setText(QString::number(v)); }

void CameraRawDialog::onResetClicked()
{
    setInitialSettings(m_defaults);
}

void CameraRawDialog::onApplyClicked()
{
    emit applyRequested();
}

void CameraRawDialog::onOkClicked()
{
    emit okRequested();
    accept();
}

void CameraRawDialog::onCancelClicked()
{
    reject();
}

}  // namespace camera_raw