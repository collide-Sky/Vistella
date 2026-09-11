// SPDX-License-Identifier: MIT
//
// ImageAdjustmentPanel — QWidget, 接管 5 toggle + 10 slider + ParamSet 同步
// (P0-1: 拆 imagewindow.cpp 上帝类的 6 大组件之一)
//   P0-2 会把 applyCurrentParams() 改成走 EngineContext background 池.
//
// P0-1.3 (2026-09-07): full migration
//   - Owns m_params / m_lastAppliedParams / m_hasLastApplied
//   - applyCurrentParams() / setupSliderRanges() / syncUiFromParams() / updateLabel()
//   - onAnyParamChanged() / onSatChanged() / onHueChanged() / onExposureChanged()
//   - paramsUnchanged() / invalidateParamCache()
//   - All these moved verbatim from ImageWindow, only the wiring changed:
//     m_original / m_current / m_undoStack / m_infoPanel / m_inUndoRedo / ui->
//     are reached via m_host (friend class on ImageWindow)
//
#pragma once

#include <QWidget>

#include "../imagewindow.h"  // for ImageEditCommand::ParamSet

class QSlider;
class QCheckBox;
class QLabel;

class ImageAdjustmentPanel : public QWidget {
    Q_OBJECT
public:
    explicit ImageAdjustmentPanel(QWidget* parent = nullptr);
    ~ImageAdjustmentPanel() override;

    // Wire host (used to push commands onto the undo stack, sync layer cache).
    // Raw pointer; ImageWindow owns this component and always outlives it.
    void setHost(ImageWindow* w) { m_host = w; }

    // Wire host's .ui so the panel can read/write sliders, toggles, value
    // labels. Must be called once after setHost (the host owns the ui).
    void setUi(Ui::ImageWindow* ui);

    // Push current ParamSet onto the host (used when user drags a slider).
    // The host decides whether to enqueue a command, sync UI, etc.
    void notifyParamChanged();

    // Mirror ParamSet into UI controls WITHOUT triggering valueChanged signals
    // (used during undo/redo and on file open).
    void syncUiFromParams(const ImageEditCommand::ParamSet& p);

    // Initialise all slider ranges. Required because .ui files leave QSlider
    // at 0-99, which clamps setValue(100) to 99 and silently drops user input.
    void setupSliderRanges();

    // Returns the live ParamSet the panel is currently showing.
    const ImageEditCommand::ParamSet& params() const { return m_params; }
    void setParams(const ImageEditCommand::ParamSet& p) { m_params = p; }

    // P0-1.3 (2026-09-07): full migration
    //   Re-render the canvas with current params. Skips work if params
    //   haven't changed since last apply (m_hasLastApplied + paramsUnchanged).
    //   Pushes new ImageEditCommand when user-driven.
    //   P0-2 (2026-09-08): 改名为 syncApplyCurrentParams, 保留原行为 (测试用)
    //     新增 asyncApplyCurrentParams 走 m_host->engineContext()->background() 池
    void applyCurrentParams(bool repaint);                       // 兼容: == syncApplyCurrentParams
    void syncApplyCurrentParams(bool repaint);                   // P0-2 同步版 (测试用)
    void asyncApplyCurrentParams(bool repaint);                  // P0-2 异步版 (UI 走这个)
    void updateLabel();

    bool paramsUnchanged() const;
    void invalidateParamCache() { m_hasLastApplied = false; }

public slots:
    // Any slider drag / toggle change → enqueue ImageEditCommand
    void onAnyParamChanged();
    // 3 个新 slider 槽: 先更新 label 提示, 再走 onAnyParamChanged
    void onSatChanged(int v);
    void onHueChanged(int v);
    void onExposureChanged(int v);

signals:
    void paramChanged();          // user dragged a slider/toggled a checkbox
    void saturationLabelChanged(int v);
    void hueLabelChanged(int v);
    void exposureLabelChanged(int v);

private:
    ImageWindow*                     m_host = nullptr;
    ImageEditCommand::ParamSet       m_params;
    // 阶段 1 Step A Bug 3 (2026-09-04): ParamSet hash 缓存
    //   之前 applyCurrentParams 每次都重跑完整 pipeline (gray/binary/brightness/sat/...)
    //   引入 "last applied" 状态, 参数没变时直接 return
    //   解决: 切 tab / undo 不动 params 时白白重算
    ImageEditCommand::ParamSet       m_lastAppliedParams;
    bool                             m_hasLastApplied = false;

    // Concrete widget pointers (declared in .cpp) — kept opaque here so the
    // header doesn't depend on the auto-generated ui_ImageWindow.h.
    struct Widgets;
    Widgets* m_ui = nullptr;
};
