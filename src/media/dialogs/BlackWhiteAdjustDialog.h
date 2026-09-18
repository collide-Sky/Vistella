// SPDX-License-Identifier: MIT
//
// BlackWhiteAdjustDialog - P0-3.2 v2 (2026-09-18)
//
// PS-style Black & White dialog: 6 colour sliders
// (Reds / Yellows / Greens / Cyans / Blues / Magentas, range -200..300,
// default 100 = neutral) with colour swatch labels, plus a Tint section
// (Hue 0..360, Saturation 0..100).
//
// Args format on applied():
//   {
//     "color0": int (-200..300, Reds),
//     "color1": int (Yellows),
//     "color2": int (Greens),
//     "color3": int (Cyans),
//     "color4": int (Blues),
//     "color5": int (Magentas),
//     "tintHue": int (0..360),
//     "tintSat": int (0..100)
//   }
//
#pragma once

#include "AdjustDialogBase.h"

#include <QVector>

class QSlider;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace dialogs {

class BlackWhiteAdjustDialog : public AdjustDialogBase
{
    Q_OBJECT
public:
    explicit BlackWhiteAdjustDialog(const QVariantMap& args, QWidget* parent = nullptr);
    ~BlackWhiteAdjustDialog() = default;

    static AdjustDialogBase* create(const QVariantMap& args, QWidget* parent);

protected:
    void setupUi(QVBoxLayout* body);
    void applyAdjust();

private slots:
    void onSliderChanged();

private:
    static constexpr int kColorCount = 6;

    QVector<QSlider*> m_colorSliders;     // -200..300, default 100
    QVector<QLabel*>  m_colorLabels;      // value labels
    QSlider*          m_tintHue           = nullptr;  // 0..360
    QSlider*          m_tintSat           = nullptr;  // 0..100
    QLabel*           m_tintHueVal        = nullptr;
    QLabel*           m_tintSatVal        = nullptr;
    QPushButton*      m_resetBtn          = nullptr;
};

} // namespace dialogs
