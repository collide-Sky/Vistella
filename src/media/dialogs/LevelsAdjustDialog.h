// SPDX-License-Identifier: MIT
//
// LevelsAdjustDialog - P0-3.2 v2 (2026-09-18)
//
// PS-style Levels dialog: 5 sliders (inLow/inHigh/gamma/outLow/outHigh)
// with a channel QComboBox (RGB/Red/Green/Blue).
//
// Args format on applied():
//   {
//     "channel": QString ("RGB" | "Red" | "Green" | "Blue"),
//     "inLow":   int (0..254),
//     "inHigh":  int (1..255),
//     "gamma":   double (0.10..3.00, displayed as v/100),
//     "outLow":  int (0..255),
//     "outHigh": int (0..255)
//   }
//
#pragma once

#include "AdjustDialogBase.h"

class QComboBox;
class QSlider;
class QLabel;
class QVBoxLayout;

namespace dialogs {

class LevelsAdjustDialog : public AdjustDialogBase
{
    Q_OBJECT
public:
    explicit LevelsAdjustDialog(const QVariantMap& args, QWidget* parent = nullptr);
    ~LevelsAdjustDialog() = default;

    static AdjustDialogBase* create(const QVariantMap& args, QWidget* parent);

protected:
    void setupUi(QVBoxLayout* body);
    void applyAdjust();

private slots:
    void onSliderChanged();
    void onChannelChanged(const QString& text);

private:
    void updateLabels();

    QComboBox* m_channelBox   = nullptr;

    QSlider* m_inLowSlider    = nullptr;
    QSlider* m_inHighSlider   = nullptr;
    QSlider* m_gammaSlider    = nullptr;
    QSlider* m_outLowSlider   = nullptr;
    QSlider* m_outHighSlider  = nullptr;

    QLabel*  m_inLowVal       = nullptr;
    QLabel*  m_inHighVal      = nullptr;
    QLabel*  m_gammaVal       = nullptr;
    QLabel*  m_outLowVal      = nullptr;
    QLabel*  m_outHighVal     = nullptr;

    QString  m_channel        = QString("RGB");
};

} // namespace dialogs
