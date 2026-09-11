// SPDX-License-Identifier: MIT
//
// HslAdjustDialog - F-K (2026-09-09)
//
// 5 dialog 中唯一实装的一个 (HSL 色彩调整).
//   3 slider: Hue (-180~180), Saturation (-100~100), Lightness (-100~100)
//   valueChanged -> updateParam + triggerPreview
//   OK / Apply -> applyAdjust emit applied({"hue", ..., "sat", ..., "light", ...})
//
#pragma once

#include "AdjustDialogBase.h"

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

    // DialogFactory 用
    static AdjustDialogBase* create(const QVariantMap& args, QWidget* parent);

protected:
    void setupUi(QVBoxLayout* body) override;
    void applyAdjust() override;

private slots:
    void onSliderChanged();

private:
    QSlider* m_hueSlider = nullptr;  // -180~180
    QSlider* m_satSlider  = nullptr;  // -100~100
    QSlider* m_lightSlider = nullptr; // -100~100
    QLabel*  m_hueLabel   = nullptr;
    QLabel*  m_satLabel   = nullptr;
    QLabel*  m_lightLabel = nullptr;
};

} // namespace dialogs
