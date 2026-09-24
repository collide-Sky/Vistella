// SPDX-License-Identifier: MIT
//
// MosaicOptionPanel - Q4.2.2 (2026-09-24)
//
// PS-style options panel for MosaicTool. Migrated from imagewindow.ui
// hardcoded groupMosaic (slider size + mode button + 4-type combo +
// status hint). ImageWindow hosts this widget inside the left dock
// stack (below ImageOptionBar), replacing the placeholder group.
//
// Two-way binding:
//   size slider  <-> MosaicTool::setSize / size()
//   mode button  <-> MosaicTool::setEnabled / isEnabled()
//   type combo   <-> MosaicTool::setType  / type()
//
// status hint mirrors toolbar toggle state. No external mocks; binds
// to MosaicTool on init and emits no signals (MosaicTool owns its
// own modeChanged/sizeChanged/typeChanged).
//
#pragma once

#include <QWidget>

#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

class MosaicTool;

class MosaicOptionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MosaicOptionPanel(MosaicTool* tool, QWidget* parent = nullptr);
    ~MosaicOptionPanel();

    // Re-sync UI from tool state (called on first show + after external
    // changes such as undo/redo that may flip tool internal state).
    void refreshFromTool();

private slots:
    void onSizeSliderChanged(int v);
    void onModeButtonToggled(bool on);
    void onTypeComboChanged(int idx);
    // MosaicTool signals - keep UI in sync when tool mutates own state.
    void onToolModeChanged(bool on);
    void onToolSizeChanged(int radius);
    void onToolTypeChanged(int type);

private:
    void setupUi();

    MosaicTool* m_tool = nullptr;
    QSlider*    m_sizeSlider  = nullptr;
    QSpinBox*   m_sizeSpin    = nullptr;
    QLabel*     m_sizeLabel   = nullptr;
    QToolButton* m_modeBtn    = nullptr;
    QComboBox*  m_typeCombo   = nullptr;
    QLabel*     m_hintLabel   = nullptr;
    bool        m_suspend = false;
};
