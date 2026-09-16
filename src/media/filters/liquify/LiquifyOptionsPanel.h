#pragma once

#include <QWidget>

#include "LiquifyMesh.h"

class QComboBox;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QButtonGroup;

namespace filters::liquify {

// PS-style options panel for the Liquify dialog.
//
// Hosts:
//   - Tool selector (8 tools: Forward / Reconstruct / Smooth / Twirl /
//                    Pucker / Bloat / Freeze Mask / Thaw Mask)
//   - Brush Size (1..500 px) and Brush Pressure (0..100 %)
//   - Mesh Size (2..32)
//   - Show Mesh / Show Frozen toggles
//   - Reset button
//
// Emits one signal per user-driven change. The dialog connects these
// signals to its LiquifyEngine and LiquifyCanvas.
class LiquifyOptionsPanel : public QWidget {
    Q_OBJECT
public:
    explicit LiquifyOptionsPanel(QWidget* parent = nullptr);

    LiquifyToolMode toolMode() const;
    int brushSize() const;
    qreal brushPressure() const;
    int meshSize() const;
    bool showMesh() const;
    bool showFrozen() const;

signals:
    void toolModeChanged(LiquifyToolMode mode);
    void brushSizeChanged(int size);
    void brushPressureChanged(qreal pressure);
    void meshSizeChanged(int size);
    void showMeshChanged(bool show);
    void showFrozenChanged(bool show);
    void resetRequested();

private:
    void buildUi();
    void wireSignals();
    QPushButton* makeToolButton(LiquifyToolMode mode, const QString& label);
    static QString toolTipFor(LiquifyToolMode mode);

    QButtonGroup* m_toolGroup = nullptr;
    QSlider* m_brushSize = nullptr;
    QSpinBox* m_brushSizeSpin = nullptr;
    QSlider* m_brushPressure = nullptr;
    QSpinBox* m_brushPressureSpin = nullptr;
    QSlider* m_meshSize = nullptr;
    QSpinBox* m_meshSizeSpin = nullptr;
    QCheckBox* m_showMesh = nullptr;
    QCheckBox* m_showFrozen = nullptr;
    QPushButton* m_resetBtn = nullptr;
};

}  // namespace filters::liquify
