// SPDX-License-Identifier: MIT
//
// ColorDock - F-H (2026-09-09) + F-M (2026-09-10)
//
// Right-side panel dock 1: color / swatch / palette / gradient / pattern (PS-style).
//   F-H stage:
//     - foreground/background swatch (2 QFrame, fg 30x30 over bg 50x50)
//     - "拾取颜色..." button (opens QColorDialog)
//     - 12-color swatch grid (PS standard)
//   F-M stage:
//     - gradient editor (5 types: linear/radial/angle/diamond/symmetric)
//       + preview bar (200x24, fg->bg linear default)
//       + 8 PS-style preset cells (4 cols x 2 rows)
//     - pattern picker (6 PS-style preset cells, 3 cols x 2 rows)
//
// PS 2026 layout (per SPEC F-M):
//   +----------------+
//   | fg/bg swatch   |
//   | 拾取颜色...     |
//   | 色板 [12 色]   |
//   | 渐变 [5+1+8]   |
//   | 图案 [6]       |
//   +----------------+
//
// F-P.3 (2026-09-09): ColorDock now uses ColorDock.ui for framework
//   (only dynamic cell widgets are created in ctor).
//
#pragma once

#include <QWidget>
#include <QColor>

class QFrame;
class QPushButton;
class QComboBox;

namespace Ui { class ColorDock; }

namespace docks {

class ColorDock : public QWidget
{
    Q_OBJECT
public:
    explicit ColorDock(QWidget* parent = nullptr);
    ~ColorDock() override;

    bool eventFilter(QObject* obj, QEvent* e) override;

    // F-H: foreground/background color accessors
    QColor foregroundColor() const { return m_foreground; }
    QColor backgroundColor() const { return m_background; }
    void   setForegroundColor(const QColor& c);
    void   setBackgroundColor(const QColor& c);

    // F-M.1: gradient type (5 PS standard)
    enum class GradientType {
        Linear     = 0,   // 线性
        Radial     = 1,   // 径向
        Angle      = 2,   // 角度
        Diamond    = 3,   // 菱形
        Symmetric  = 4,   // 对称
    };
    Q_ENUM(GradientType)

    // F-M.1: gradient accessors
    GradientType gradientType() const { return m_gradientType; }
    int          gradientPreset() const { return m_gradientPreset; }   // 0..7, -1 = custom
    void         setGradientType(GradientType t);
    void         setGradientPreset(int idx);

    // F-M.2: pattern accessors
    int  patternPreset() const { return m_patternPreset; }   // 0..5, -1 = none
    void setPatternPreset(int idx);

signals:
    void foregroundColorChanged(const QColor& c);
    void backgroundColorChanged(const QColor& c);
    void gradientTypeChanged(docks::ColorDock::GradientType t);
    void gradientPresetChanged(int idx);
    void patternPresetChanged(int idx);

private slots:
    void onPickColorClicked();
    void onGradientTypeChanged(int idx);

private:
    void setupPalette();
    void setupGradientPresets();
    void setupPatternPresets();
    void updateGradientPreview();

    Ui::ColorDock* ui = nullptr;   // F-P.3 (2026-09-09)

    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;

    GradientType m_gradientType    = GradientType::Linear;
    int          m_gradientPreset  = 0;     // default: fg->bg linear
    int          m_patternPreset   = -1;    // default: none
};

} // namespace docks
