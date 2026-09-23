// SPDX-License-Identifier: MIT
//
// CameraRawDialog - P3.5 (2026-09-23) PS-style Camera Raw dialog interface.
//
//   Non-modal dialog mirroring PS Camera Raw:
//     - 5 sliders (exposure/wbTemp/wbTint/sharpness/noiseRedux)
//     - Load / Reset / Apply / OK / Cancel
//     - HasLibRaw branch when libraw not configured: all sliders disabled
//       + status banner explains the missing dependency.
//
//   The dialog doesn't perform the actual decode — it returns a
//   CameraRawSettings struct via a signal; caller (ImageIOController)
//   routes that to CameraRawLoader::decodeRaw.
//
#pragma once

#include <QDialog>
#include "CameraRawLoader.h"

class QSlider;
class QLabel;
class QPushButton;

namespace camera_raw {

class CameraRawDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CameraRawDialog(QWidget *parent = nullptr);
    ~CameraRawDialog() override;

    // Set the initial settings (used to default to the file's embedded
    // WB/exposure once libraw is reading). When unset, defaults are used.
    void setInitialSettings(const CameraRawSettings& s);
    CameraRawSettings settings() const { return m_settings; }

signals:
    // User clicked Apply (preview against current image) — caller
    // calls CameraRawLoader::decodeRaw with current settings.
    void applyRequested();
    // User clicked OK — final settings confirmed, dialog closes.
    void okRequested();

private slots:
    void onExposureChanged(int v);
    void onWbTempChanged(int v);
    void onWbTintChanged(int v);
    void onSharpnessChanged(int v);
    void onNoiseReduxChanged(int v);
    void onResetClicked();
    void onApplyClicked();
    void onOkClicked();
    void onCancelClicked();

private:
    void setupUi();
    void refreshLabels();

    CameraRawSettings m_settings;
    CameraRawSettings m_defaults;

    QSlider    *m_exposure    = nullptr;
    QSlider    *m_wbTemp      = nullptr;
    QSlider    *m_wbTint      = nullptr;
    QSlider    *m_sharpness   = nullptr;
    QSlider    *m_noiseRedux  = nullptr;
    QLabel     *m_statusLabel = nullptr;
    QLabel     *m_exposureLab = nullptr;
    QLabel     *m_wbTempLab   = nullptr;
    QLabel     *m_wbTintLab   = nullptr;
    QLabel     *m_sharpLab    = nullptr;
    QLabel     *m_noiseLab    = nullptr;
    QPushButton *m_applyBtn   = nullptr;
    QPushButton *m_okBtn      = nullptr;
    QPushButton *m_cancelBtn  = nullptr;
    QPushButton *m_resetBtn   = nullptr;
};

}  // namespace camera_raw