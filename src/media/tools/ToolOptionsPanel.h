// SPDX-License-Identifier: MIT
//
// ToolOptionsPanel
//
// The option bar pages for the tools that M2-4 added.
//
//   Each of the new tools needs a page in the ImageOptionBar. Rather
//   than one panel per tool, they share a single panel whose widgets
//   are shown or hidden according to the tool.
//
//   The panel is driven from OUTSIDE (the tool pushes its values into
//   the panel), because the tool is not a QObject -- the same pattern
//   the brush options panel uses.
//
//   Why the controls matter: a tool whose options cannot be changed is
//   a tool with one fixed behaviour. The tolerance of the paint
//   bucket, the type of the gradient and the amount of the dodge /
//   burn are what make these tools usable at all.
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

namespace tools {

class ToolOptionsPanel : public QWidget
{
    Q_OBJECT
public:
    // Which tool the panel is showing the controls for.
    enum class Kind {
        Eraser,
        Gradient,
        PaintBucket,
        DodgeBurn,
        BlurSharpenSmudge
    };

    explicit ToolOptionsPanel(Kind kind, QWidget* parent = nullptr);

    // ===== push the tool's state into the panel =====
    void setTolerance(int v);
    void setGradientType(int index);
    void setMode(int index);
    void setAmount(int v);
    void setStrength(int v);
    void setBrushSize(int v);

    // ===== read the panel's state =====
    int tolerance() const;
    int gradientType() const;
    int mode() const;
    int amount() const;
    int strength() const;
    int brushSize() const;

signals:
    // The user changed something. The tool connects to this.
    void toleranceChanged(int v);
    void gradientTypeChanged(int index);
    void modeChanged(int index);
    void amountChanged(int v);
    void strengthChanged(int v);
    void brushSizeChanged(int v);

private:
    Kind m_kind;

    // The tolerance of the paint bucket.
    QLabel*    m_tolLabel = nullptr;
    QSlider*   m_tolSlider = nullptr;
    QSpinBox*  m_tolSpin = nullptr;

    // The type of the gradient (linear / radial / angular).
    QLabel*    m_gradLabel = nullptr;
    QComboBox* m_gradCombo = nullptr;

    // The mode of the dodge / burn (dodge / burn) and of the blur
    // group (blur / sharpen / smudge).
    QLabel*    m_modeLabel = nullptr;
    QComboBox* m_modeCombo = nullptr;

    // The amount / strength.
    QLabel*    m_amountLabel = nullptr;
    QSlider*   m_amountSlider = nullptr;
    QSpinBox*  m_amountSpin = nullptr;

    // The brush size.
    QLabel*    m_sizeLabel = nullptr;
    QSlider*   m_sizeSlider = nullptr;
    QSpinBox*  m_sizeSpin = nullptr;
};

} // namespace tools
