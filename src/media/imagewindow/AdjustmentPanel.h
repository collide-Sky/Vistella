// SPDX-License-Identifier: MIT
//
// AdjustmentPanel - P0-3.2 (2026-09-08)
//
// PS 风格的色彩调整 UI: 5 tab 容器
//   1. Curves       - 8 控制点自定义 QWidget 编辑器, 实时 buildCurvesLUT
//   2. Levels       - 3 个 input slider (inLow / inHigh / gamma) + 2 个 output (outLow / outHigh)
//   3. HSL          - 8 色相 hue slider + 8 sat slider + 1 lightness slider (17 总)
//   4. 黑白 (B&W)   - 6 个颜色 slider (Reds/Yellows/Greens/Cyans/Blues/Magentas)
//   5. 通道混合器   - 9 个 slider (RGB -> RGB 3x3 矩阵)
//
// 跟旧 ImageAdjustmentPanel 关系:
//   - 旧 5 toggle + 10 slider (ImageAdjustmentPanel) 完全保留, 不动
//   - AdjustmentPanel 是**新组件**, 跟旧的并存, 各管各的
//   - 5 tab 切换不丢参数 (AdjustmentPanel 内部缓存每 tab 当前值)
//
// 接 P0-2 异步:
//   - applyCurrentTab() 调 ImageProcessor::applyLutAsync (callback overload)
//   - 走 background pool, 完成后 QMetaObject::invokeMethod 切回主线程刷显示
//
// 接 Layer 系统 (P0-3.2 简化):
//   - P0-3.2: 直接调 m_host->setCurrentImage(result) + 推 ImageEditCommand(beforeImage, afterImage)
//   - P0-3.3 才升级为 LayerCommand::SetAdjustmentLut + SetAdjustmentType
//
// 设计原则 (一次性到位, P0-3.2 不留补丁):
//   1. 5 tab 内部参数 cache - 切 tab 不重置其他 tab 缓存
//   2. 每次 applyCurrentTab 走 background pool, 不阻塞 UI
//   3. 8 个 LUT 函数 (curves/levels/hueSat/bw/colorBalance/vibrance/photoFilter) 都已经 P0-3.1 实装
//   4. UI 控件数量充足 (5 tab, 25+ slider/control), 但不臃肿
//
#pragma once

#include "imagewindow.h"   // for ImageWindow, ImageEditCommand

#include <QWidget>
#include <QTabWidget>
#include <QPolygonF>
#include <QVector>
#include <QTimer>          // P0-3.3 (2026-09-08): debounce commit timer

#include <opencv2/core.hpp>

class QSlider;
class QLabel;
class QPushButton;
class QMouseEvent;
class QPaintEvent;

// =====================================================================
// CurveEditor - 自定义 QWidget, 显示 + 编辑 8 控制点曲线
//   8 control points 分布在 0-255 网格上 (左下 = (0,0), 右上 = (255,255))
//   鼠标拖动控制点, 松开 emit pointsChanged signal
//   paintEvent 画网格 + 控制点 + 曲线
// =====================================================================
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);

    QPolygonF controlPoints() const { return m_points; }
    void setControlPoints(const QPolygonF& p);

    // 8 个 control points 的默认分布 (PS 默认曲线 = 直线, 加 6 个均匀分布的控制点)
    static QPolygonF defaultPoints();

signals:
    void pointsChanged(const QPolygonF& p);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QPolygonF m_points;
    int  m_dragIndex = -1;
    static constexpr int kPointRadius = 6;
    static constexpr int kNumPoints   = 8;  // 8 控制点

    // 转换: control 空间 (0..255) <-> widget 空间 (像素)
    QPointF ctrlToWidget(const QPointF& p) const;
    QPointF widgetToCtrl(const QPointF& p) const;
};

// =====================================================================
// AdjustmentPanel - 5 tab 容器
// =====================================================================
class AdjustmentPanel : public QWidget {
    Q_OBJECT
public:
    explicit AdjustmentPanel(QWidget* parent = nullptr);
    ~AdjustmentPanel() override;

    void setHost(ImageWindow* w) { m_host = w; }

    // 5 tab 索引 (0=Curves, 1=Levels, 2=HSL, 3=B&W, 4=ChannelMixer)
    static constexpr int kTabCurves       = 0;
    static constexpr int kTabLevels       = 1;
    static constexpr int kTabHsl          = 2;
    static constexpr int kTabBlackWhite   = 3;
    static constexpr int kTabChannelMixer = 4;
    static constexpr int kTabCount        = 5;

    int currentTab() const { return m_currentTab; }
    void switchToTab(int tabIndex);
    QString tabName(int tabIndex) const;  // 撤销栈用, e.g. "Curves" / "Levels" / ...

    // 5 tab 参数 struct (P0-3.2 简化: 全部 P0-3.3 升级为 AdjustmentLayer)
    struct CurvesParams {
        QPolygonF controlPoints;  // CurveEditor 拿
    };
    struct LevelsParams {
        int    inLow  = 0;
        int    inHigh = 255;
        double gamma  = 1.0;
        int    outLow  = 0;
        int    outHigh = 255;
    };
    struct HslParams {
        // 8 个色相区段: Master / Reds / Yellows / Greens / Cyans / Blues / Magentas / (预留)
        QVector<int> hueShifts;     // [-180, 180]
        QVector<int> satShifts;     // [-100, 100]
        int          lightness = 0;  // [-100, 100]
    };
    struct BlackWhiteParams {
        // 6 个颜色: Reds / Yellows / Greens / Cyans / Blues / Magentas
        QVector<double> rgbMixer;  // [0, 200]
        // Tint (色调染色, PS 同款 B&W tint)
        int tintHue = 0;          // [0, 360]
        int tintSat = 0;          // [0, 100]
    };
    struct ChannelMixerParams {
        // 3x3 矩阵 RGB -> RGB, [0, 200] (200 = 100% 保留)
        int rR = 200, rG = 0,   rB = 0;
        int gR = 0,   gG = 200, gB = 0;
        int bR = 0,   bG = 0,   bB = 200;
        // P1.5.1 (2026-09-18): Monochrome 输出 (PS 同款): 输出灰度
        bool monochrome = false;
    };

    // 5 tab 参数 getter
    const CurvesParams&       curves() const { return m_curves; }
    const LevelsParams&       levels() const { return m_levels; }
    const HslParams&          hsl()    const { return m_hsl; }
    const BlackWhiteParams&   bw()     const { return m_bw; }
    const ChannelMixerParams& cm()     const { return m_cm; }

    // 应用当前 tab 的调值 (调值变化时调)
    // 走 background pool async, 完成后切主线程刷显示
    // P0-3.3 (2026-09-08): 不再立即推 ImageEditCommand, 改走 debounce commit
    //   200ms 内多次 valueChanged 只 push 一条 undo, 避免拖动时栈爆炸
    void applyCurrentTab();

    // P0-3.3 (2026-09-08): debounce commit - 调值"静默"200ms 后推一条 LayerCommand::SetAdjustmentLut
    //   由内部 QTimer 触发, 也可以手动调 (拖完立即 commit)
    void commitUndoDebounced();

    // 撤销栈 push (P0-3.3 升级: 改成 LayerCommand::SetAdjustmentLut)
    //   取 m_current 快照做 before, 应用后用结果做 after
    //   公开让外部(对话框, 拖动结束)能强制立即 commit, 不等 200ms debounce
    void pushUndoCommand();

signals:
    void tabChanged(int index);
    void paramChanged();         // 任何调值变化

public slots:
    // P1.5.1 (2026-09-18): Dispatcher for standalone DialogFactory dialogs.
    //   CurvesAdjustDialog::applied -> AdjustmentPanel::setStandaloneParams("Curves", args)
    //   Updates m_curves/m_levels/m_bw/m_cm, refreshes inline UI, calls applyCurrentTab().
    //   dialogId in {"Curves", "Levels", "HSL", "B&W", "ChannelMixer"}.
    void setStandaloneParams(const QString& dialogId, const QVariantMap& args);

private:
    // 内部 helper
    void buildCurvesPage();
    void buildLevelsPage();
    void buildHslPage();
    void buildBlackWhitePage();
    void buildChannelMixerPage();

    // P1.5.1 (2026-09-18): per-tab dispatcher helpers, called by setStandaloneParams
    void applyCurvesFromArgs(const QVariantMap& args);
    void applyLevelsFromArgs(const QVariantMap& args);
    void applyBnWFromArgs(const QVariantMap& args);
    void applyChannelMixerFromArgs(const QVariantMap& args);
    // P0 leftover review (2026-09-21): HSL standalone dialog args land
    //   here (QList<int> hueShifts (8) + QList<int> satShifts (8) + int lightness).
    void applyHslFromArgs(const QVariantMap& args);

    // 当前 tab 的 LUT 构建
    cv::Mat buildCurrentTabLut() const;

    ImageWindow* m_host = nullptr;
    int m_currentTab = 0;

    // 5 tab 参数缓存 (P0-3.2 简化: 切 tab 不重置)
    CurvesParams       m_curves;
    LevelsParams       m_levels;
    HslParams          m_hsl;
    BlackWhiteParams   m_bw;
    ChannelMixerParams m_cm;

    // UI 控件
    QTabWidget* m_tabWidget = nullptr;

    // P0-3.3 (2026-09-08) LOD debounce 状态:
    //   m_pendingBefore: 当前 debounce 窗口的"before" image (首次 valueChanged 时 snapshot)
    //   m_commitTimer: 200ms 单次定时器, 静默期到就 push 一条 LayerCommand
    //   设计: 拖动过程中频繁 applyCurrentTab, 但只 push 一条 undo
    //   200ms 是经验值, 太短可能误合并多条独立操作, 太长用户感知不到撤销生效
    cv::Mat m_pendingBefore;
    QTimer* m_commitTimer = nullptr;

    // Curves tab
    CurveEditor* m_curvesEditor = nullptr;
    QPushButton* m_curvesResetBtn = nullptr;

    // Levels tab - 5 slider
    QSlider* m_levelsInLow  = nullptr;
    QSlider* m_levelsInHigh = nullptr;
    QSlider* m_levelsGamma  = nullptr;
    QSlider* m_levelsOutLow = nullptr;
    QSlider* m_levelsOutHigh = nullptr;
    QLabel*  m_levelsInLowVal  = nullptr;
    QLabel*  m_levelsInHighVal = nullptr;
    QLabel*  m_levelsGammaVal  = nullptr;
    QLabel*  m_levelsOutLowVal = nullptr;
    QLabel*  m_levelsOutHighVal = nullptr;

    // HSL tab - 17 slider (8 hue + 8 sat + 1 lightness)
    QVector<QSlider*> m_hslHueSliders;
    QVector<QSlider*> m_hslSatSliders;
    QSlider*          m_hslLightness = nullptr;

    // B&W tab - 6 slider
    QVector<QSlider*> m_bwSliders;

    // Channel Mixer tab - 9 slider (3x3)
    QVector<QSlider*> m_cmSliders;
};
