// SPDX-License-Identifier: MIT
//
// HslAdjustDialog - F-K (2026-09-09) / P0 leftover review (2026-09-21)
//
// PS-style HSL adjust dialog. 8 hue bands + 8 saturation bands + 1
// lightness, matching AdjustmentPanel::m_hsl structure (master + R, Y, G, C, B, M).
// Args (in + out):
//   "hueShifts": QList<int> (8 ints, -180..180)
//   "satShifts": QList<int> (8 ints, -100..100)
//   "lightness": int (-100..100)
//
// P0 leftover review (2026-09-21): previous single-channel (1 hue + 1 sat
// + 1 light) was a stub incompatible with AdjustmentPanel::m_hsl
// array structure, leaving AdjustmentPanel::setStandaloneParams("HSL", ...)
// as a no-op. Now uses the PS-compatible 8-band layout so the dialog
// round-trips through the panel's inline HSL tab.

#pragma once

#include "AdjustDialogBase.h"

#include <QVector>

class QSlider;
class QLabel;
class QVBoxLayout;

namespace dialogs {

class HslAdjustDialog : public AdjustDialogBase
{
    Q_OBJECT
public:
    explicit HslAdjustDialog(const QVariantMap& args, QWidget* parent = nullptr);
    ~HslAdjustDialog() override = default;

    // DialogFactory calls this.
    static AdjustDialogBase* create(const QVariantMap& args, QWidget* parent);

protected:
    void setupUi(QVBoxLayout* body) override;
    void applyAdjust() override;

private slots:
    void onSliderChanged(int band);

private:
    QVariantList currentHueShifts() const;
    QVariantList currentSatShifts() const;

private:
    static constexpr int kBandCount = 8;

    void setBandDefaults();
    void syncLabels();
    void syncSliders();

    QVector<QSlider*> m_hueSliders;     // -180..180
    QVector<QSlider*> m_satSliders;     // -100..100
    QSlider*          m_lightSlider = nullptr; // -100..100
    QVector<QLabel*>  m_hueLabels;
    QVector<QLabel*>  m_satLabels;
    QLabel*           m_lightLabel = nullptr;
};

} // namespace dialogs
