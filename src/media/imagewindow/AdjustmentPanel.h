// SPDX-License-Identifier: MIT
//
// AdjustmentPanel
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
// 接 Layer 系统:
//   调整是**非破坏**的 —— 每个 tab 拥有自己的 Adjustment 图层, LUT 是图层的属性,
//   由图层栈在合成时套用. 底图像素永远不被改写, 所以:
//     - 连续拖滑块不会在已调整的结果上再调整一次
//     - 之前的画笔/马赛克涂抹不会被调整覆盖
//     - 撤销恢复的是 LUT, 不是像素快照 (撤销栈里存的是 256 字节, 不是整图)
//
// 设计原则 (一次性到位, P0-3.2 不留补丁):
//   1. 5 tab 内部参数 cache - 切 tab 不重置其他 tab 缓存
//   2. 每次 applyCurrentTab 走 background pool, 不阻塞 UI
//   3. 8 个 LUT 函数 (curves/levels/hueSat/bw/colorBalance/vibrance/photoFilter) 都已经 P0-3.1 实装
//   4. UI 控件数量充足 (5 tab, 25+ slider/control), 但不臃肿
//
#pragma once

#include "imagewindow.h"   // for ImageWindow, ImageEditCommand
#include "imageworker/layers/Layer.h"   // Layer snapshot stored in the preview baseline

#include <QWidget>
#include <QTabWidget>
#include <QPolygonF>
#include <QVector>
#include <QTimer>          // debounce commit timer

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
        // M2-6b: one curve per channel. The Curves dialog has a channel
        //   selector (RGB / Red / Green / Blue); the curve of the
        //   channel that was edited is stored here. An empty polygon
        //   means "this channel was never edited" and the master curve
        //   applies to it.
        QPolygonF redPoints;
        QPolygonF greenPoints;
        QPolygonF bluePoints;
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
        // Monochrome 输出 (PS 同款): 输出灰度
        bool monochrome = false;
    };

    // 5 tab 参数 getter
    const CurvesParams&       curves() const { return m_curves; }
    const LevelsParams&       levels() const { return m_levels; }
    const HslParams&          hsl()    const { return m_hsl; }
    const BlackWhiteParams&   bw()     const { return m_bw; }
    const ChannelMixerParams& cm()     const { return m_cm; }

    // 应用当前 tab 的调值 (调值变化时调)
    // 写进该 tab 的 Adjustment 图层 (LUT), 合成时由图层栈套用
    // 不立即推撤销命令, 改走 debounce commit:
    //   200ms 内多次 valueChanged 只 push 一条 undo, 避免拖动时栈爆炸
    void applyCurrentTab();

    // ---- the live preview session (M2-6a) ----
    //
    // The standalone adjustment dialogs (Curves/Levels/HSL/B&W/ChannelMixer) have to
    // change the canvas live while the user drags a slider, but the preview must NOT use
    // the debounce commit path of applyCurrentTab -- that path pushes commands into the
    // undo stack, one drag would flood it, and Cancel would leave junk entries behind.
    //
    // So there is a whole separate session lifecycle here:
    //
    //   beginPreviewSession(dialogId)
    //       takes a baseline snapshot (does the adjustment layer of that tab exist, its LUT, its
    //       opacity, its index). That snapshot is what Cancel restores.
    //   previewStandaloneParams(dialogId, args)
    //       routes exactly like setStandaloneParams, but pushes NO undo
    //       command while writing. The canvas changes at once, the undo stack does not.
    //   commitPreviewSession(dialogId)
    //       freezes the previewed state into EXACTLY ONE undo command. One undo returns to the
    //       state before the dialog opened.
    //   cancelPreviewSession(dialogId)
    //       restores the baseline snapshot and removes the adjustment layer the preview
    //       created -- after Cancel the document has to be exactly as it was before the dialog opened.
    //
    // dialogId in {"Curves", "Levels", "HSL", "B&W", "ChannelMixer"}.
    // beginPreviewSession returns false when this dialogId cannot be previewed (unknown id).
    bool beginPreviewSession(const QString& dialogId);
    void previewStandaloneParams(const QString& dialogId, const QVariantMap& args);
    void commitPreviewSession(const QString& dialogId);
    void cancelPreviewSession(const QString& dialogId);
    // Whether a preview session is running (read only, for the host / the tests)
    bool hasPreviewSession() const { return m_previewSession.active; }

    // debounce commit - 调值"静默"200ms 后推一条 LayerCommand::SetAdjustmentLut
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
    // Dispatcher for standalone DialogFactory dialogs
    //   CurvesAdjustDialog::applied -> AdjustmentPanel::setStandaloneParams("Curves", args)
    //   Updates m_curves/m_levels/m_bw/m_cm, refreshes inline UI, calls applyCurrentTab().
    //   dialogId in {"Curves", "Levels", "HSL", "B&W", "ChannelMixer"}.
    void setStandaloneParams(const QString& dialogId, const QVariantMap& args);

private:
    // ---- the state of the preview session (M2-6a) ----
    //
    // The baseline snapshot holds the state of THE ADJUSTMENT LAYER OF THIS TAB when the session started:
    //   layerExisted  -- did this tab have an adjustment layer when the session started
    //   layerIndex    -- its index if it had one (-1 when it was created by the session)
    //   baselineLut   -- its LUT if it had one (the undo / Cancel restore target)
    //   baselineOpacity -- its opacity if it had one
    //   layerSnapshot -- its full Layer snapshot if it had one (Cancel puts the layer back
    //                    in its place, keeping its stableId and the rest of its identity)
    //
    // The preview does NOT go through the undo stack, so the panel has to hold this snapshot itself.
    struct PreviewSession {
        bool     active        = false;
        QString  dialogId;
        int      tab           = -1;
        bool     layerExisted  = false;
        int      layerIndex    = -1;
        float    baselineOpacity = 1.0f;
        cv::Mat  baselineLut;
        layers::Layer baselineLayerSnapshot;
    };

    // dialogId -> tab index (Curves/Levels/HSL/B&W/ChannelMixer)
    // Returns -1 when the id cannot be resolved.
    static int tabIndexForDialogId(const QString& dialogId);
    // The index of the adjustment layer a tab currently uses, -1 when there is none.
    int adjustmentLayerIndexForTab(int tab) const;
    // The preview writes the adjustment: it creates the layer or changes it, and pushes no undo command.
    void applyTabLutInPreviewMode(int tab, const cv::Mat& lut);

    // 内部 helper
    void buildCurvesPage();
    void buildLevelsPage();
    void buildHslPage();
    void buildBlackWhitePage();
    void buildChannelMixerPage();

    // per-tab dispatcher helpers, called by setStandaloneParams (previewMode=false)
    //   and by previewStandaloneParams (previewMode=true).
    // With previewMode=true the final write goes through applyTabLutInPreviewMode (no undo command).
    void applyCurvesFromArgs(const QVariantMap& args, bool previewMode);
    void applyLevelsFromArgs(const QVariantMap& args, bool previewMode);
    void applyBnWFromArgs(const QVariantMap& args, bool previewMode);
    void applyChannelMixerFromArgs(const QVariantMap& args, bool previewMode);
    // HSL standalone dialog args land
    //   here (QList<int> hueShifts (8) + QList<int> satShifts (8) + int lightness).
    void applyHslFromArgs(const QVariantMap& args, bool previewMode);

    // 当前 tab 的 LUT 构建
    cv::Mat buildCurrentTabLut() const;

    // M2-6b: the per-pixel payload of a tab.
    //   HSL / B&W / ChannelMixer are NOT per-channel functions, so they
    //   cannot travel as a 1D LUT -- they carry an AdjustmentParams.
    //   Curves and Levels keep the LUT path (kind == Lut).
    layers::AdjustmentParams buildTabParams(int tab) const;
    // M2-6b: the per-channel LUTs of a tab.
    //   Empty when the tab has no per-channel curves; otherwise three
    //   LUTs in the OpenCV BGR order (blue, green, red).
    QVector<cv::Mat> buildTabChannelLuts(int tab) const;

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

    //  每个 tab 的调整图层 (LayerKind::Adjustment) 在图层栈里的下标.
    //   -1 = 这个 tab 还没建过调整图层.
    //   调整图层放在栈顶, 它套用的对象是它下面的所有图层.
    int m_adjLayerIndex[kTabCount];

    // LOD debounce 状态
    //   m_pendingLut[tab]: 当前 debounce 窗口的"before" LUT (改值前的 LUT)
    //   m_commitTimer: 200ms 单次定时器, 静默期到就 push 一条 LayerCommand
    //   设计: 拖动过程中频繁 applyCurrentTab, 但只 push 一条 undo
    //   200ms 是经验值, 太短可能误合并多条独立操作, 太长用户感知不到撤销生效
    cv::Mat m_pendingLut[kTabCount];
    // M2-6b: an adjustment layer carries more than a LUT, so the
    //   debounce window has to remember the whole "before" state.
    //   m_pendingValid is the sentinel: an empty LUT is a legal state,
    //   so the LUT itself cannot be the sentinel any more.
    bool                     m_pendingValid[kTabCount] = {false, false, false, false, false};
    layers::AdjustmentParams m_pendingParams[kTabCount];
    QVector<cv::Mat>         m_pendingChannelLuts[kTabCount];
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

    // The preview session that is running (M2-6a)
    PreviewSession m_previewSession;
};
