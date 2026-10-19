#ifndef SETTINGSGPUPANEL_H
#define SETTINGSGPUPANEL_H

#include <QWidget>

#include "../media/gpu/BackendRegistry.h"   // gpu::RenderMode

class QRadioButton;
class QPushButton;
class QLabel;
class QButtonGroup;

// P8.3.x (2026-10-19): GPU settings panel.
//
//   Self-contained QWidget that exposes the 3-state RenderMode (Auto /
//   Force GPU / Force CPU) plus a "Probe OpenGL" button that runs a
//   fresh capability probe. Designed to be embedded in any QStackedWidget
//   (currently SettingsDialog) and to be testable without going through
//   the full dialog lifecycle.
//
//   All state is read from / written to gpu::BackendRegistry::instance().
//   No persistent QSettings — the registry is the single source of truth
//   at runtime; if you want mode to survive restart, save it yourself
//   (the main app already wires this up via the status bar menu).
class SettingsGpuPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsGpuPanel(QWidget *parent = nullptr);
    ~SettingsGpuPanel() override;

    // The current selection (without applying to the registry). Lets
    // tests verify the radio state and lets the parent dialog show a
    // "modified" indicator.
    gpu::RenderMode selectedMode() const;

    // Apply the selected mode to the global registry.
    void applySelected();

    // Read-only state for tests / status display.
    QString currentModeLabel() const;
    QString currentBackendName() const;
    bool    gpuAvailable() const;

signals:
    // Emitted after applySelected() actually changes the registry. The
    // status bar / main window listen and refresh their own widgets.
    void modeApplied(gpu::RenderMode newMode);

private slots:
    void onApplyClicked();
    void onProbeClicked();

private:
    void refreshFromRegistry();
    void refreshStatusLabel();

    QButtonGroup*   m_group       = nullptr;
    QRadioButton*   m_autoRadio   = nullptr;
    QRadioButton*   m_gpuRadio    = nullptr;
    QRadioButton*   m_cpuRadio    = nullptr;
    QPushButton*    m_applyBtn    = nullptr;
    QPushButton*    m_probeBtn    = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QLabel*         m_probeLabel  = nullptr;
};

#endif // SETTINGSGPUPANEL_H