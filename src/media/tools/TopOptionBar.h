#pragma once

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QToolButton;
class QLabel;

namespace tools {

// P7.5 (2026-10-12): 顶部共享选项栏 (PS 风格 — 模式 / 不透明度 / 流量 / 平滑 / 角度 / 大小).
//   P7.5 stage: 创建 visual widget + UI 把 7 控件画出来 (默认值).
//   P7.6 (2026-10-12): 在所有画笔类工具 (Brush/Clone/Heal/Patch/MaskBrush) 接 TopOptionBar,
//   共享 mode/opacity/flow/smoothing/angle/dyn-size across 工具.
//
//   控件 (横排, 从左到右):
//     [模式 combo: 正常/背后/清除]  [不透明度 spinbox 0..100%]  [流量 0..100%]
//     [平滑 0..100%]                 [角度 0..359° + lock]      [大小 1..2000 px + link]
class TopOptionBar : public QWidget
{
    Q_OBJECT
public:
    explicit TopOptionBar(QWidget* parent = nullptr);
    ~TopOptionBar() override;

    // PS 模式枚举 — 用于 comboBox 索引.
    enum class BlendMode { Normal = 0, Multiply = 1, Screen = 2 };

    // Getter — 给外部 (ImageWindow 撮合) 读配置.
    BlendMode blendMode() const;
    double opacity() const;        // 0..100
    double flow() const;           // 0..100
    double smoothing() const;      // 0..100
    int    angle() const;          // 0..359
    int    brushSize() const;      // 1..2000 px

signals:
    // 配置变化 — 接 ImageWindow (转发给当前 ToolState).
    void blendModeChanged(int idx);
    void opacityChanged(double pct);
    void flowChanged(double pct);
    void smoothingChanged(double pct);
    void angleChanged(int deg);
    void brushSizeChanged(int px);

private:
    QComboBox*     m_cmbBlendMode = nullptr;
    QDoubleSpinBox* m_spnOpacity  = nullptr;
    QDoubleSpinBox* m_spnFlow     = nullptr;
    QDoubleSpinBox* m_spnSmoothing = nullptr;
    QSpinBox*       m_spnAngle    = nullptr;
    QSpinBox*       m_spnSize     = nullptr;
    QToolButton*    m_btnAngleLock = nullptr;
    QToolButton*    m_btnSizeLink  = nullptr;
};

} // namespace tools
