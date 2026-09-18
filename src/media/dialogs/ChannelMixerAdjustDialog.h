// SPDX-License-Identifier: MIT
//
// ChannelMixerAdjustDialog - P0-3.2 v2 (2026-09-18)
//
// PS-style Channel Mixer dialog: 3x3 grid of sliders
// (output channel R/G/B vs source channel R/G/B), range -200..200,
// default diagonal = (200, 200, 200) with off-diagonal = 0.
// Includes a Monochrome checkbox and a Reset button.
//
// Args format on applied() (output-channel × source-channel indexed as mXY):
//   {
//     "m00": int (-200..200),  // out=R src=R
//     "m01": int,              // out=R src=G
//     "m02": int,              // out=R src=B
//     "m10": int,              // out=G src=R
//     "m11": int,              // out=G src=G
//     "m12": int,              // out=G src=B
//     "m20": int,              // out=B src=R
//     "m21": int,              // out=B src=G
//     "m22": int,              // out=B src=B
//     "monochrome": bool
//   }
//
#pragma once

#include "AdjustDialogBase.h"

#include <QVector>

class QSlider;
class QLabel;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

namespace dialogs {

class ChannelMixerAdjustDialog : public AdjustDialogBase
{
    Q_OBJECT
public:
    explicit ChannelMixerAdjustDialog(const QVariantMap& args, QWidget* parent = nullptr);
    ~ChannelMixerAdjustDialog() = default;

    static AdjustDialogBase* create(const QVariantMap& args, QWidget* parent);

protected:
    void setupUi(QVBoxLayout* body);
    void applyAdjust();

private slots:
    void onSliderChanged();
    void onMonochromeToggled(bool checked);

private:
    static constexpr int kChannelCount = 3;
    static constexpr int kSliderCount  = 9;   // 3 outputs x 3 sources
    static int idx(int outIdx, int srcIdx) { return outIdx * 3 + srcIdx; }

    QVector<QSlider*> m_sliders;             // size 9, indexed by outIdx*3 + srcIdx
    QVector<QLabel*>  m_valueLabels;
    QCheckBox*        m_monoCheck            = nullptr;
    QPushButton*      m_resetBtn             = nullptr;
};

} // namespace dialogs
