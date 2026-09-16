#pragma once

#include <QWidget>

class QSlider;
class QSpinBox;
class QPushButton;

namespace tools {

class MaskBrushTool;

// PS-style options panel for the MaskBrushTool.
//
//   - Brush Size  (1..500 px)
//   - Hardness    (0..100 %)
//   - Opacity     (0..100 %)
//   - Flow        (0..100 %, currently unused / mirrors opacity)
//   - Mode toggle: "Reveal" (paint white) / "Protect" (paint black)
//
class MaskOptionsPanel : public QWidget {
    Q_OBJECT
public:
    explicit MaskOptionsPanel(MaskBrushTool* tool, QWidget* parent = nullptr);

private slots:
    void onSizeChanged(int v);
    void onHardnessChanged(int v);
    void onOpacityChanged(int v);
    void onFlowChanged(int v);
    void onModeToggled();

private:
    MaskBrushTool* m_tool = nullptr;
    QSlider* m_size = nullptr;
    QSpinBox* m_sizeSpin = nullptr;
    QSlider* m_hardness = nullptr;
    QSpinBox* m_hardnessSpin = nullptr;
    QSlider* m_opacity = nullptr;
    QSpinBox* m_opacitySpin = nullptr;
    QSlider* m_flow = nullptr;
    QSpinBox* m_flowSpin = nullptr;
    QPushButton* m_revealBtn = nullptr;
    QPushButton* m_protectBtn = nullptr;
};

}  // namespace tools