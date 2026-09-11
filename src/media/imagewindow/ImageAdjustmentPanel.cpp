// SPDX-License-Identifier: MIT
//
// ImageAdjustmentPanel — P0-1.3 (2026-09-07) full migration.
// P0-2 (2026-09-08): applyCurrentParams 异步化 (走 EngineContext background 池)
//
// Owns: m_params / m_lastAppliedParams / m_hasLastApplied + the
// apply/setup/sync/on* methods that operate on them. Reaches the host's
// m_original / m_current / m_undoStack / m_infoPanel / m_inUndoRedo and
// .ui controls via m_host (ImageWindow is a friend class).
//
#include "ImageAdjustmentPanel.h"

#include "../ui_imagewindow.h"
#include "../imageprocessor.h"
#include "../imageinfopanel.h"

#include <QCheckBox>
#include <QSlider>
#include <QToolButton>
#include <QLabel>
#include <QSpinBox>
#include <QFontComboBox>
#include <QMetaObject>

#include <opencv2/imgproc.hpp>

// Internal struct holding typed pointers into host's .ui file. Lets the .cpp
// avoid touching ui_imagewindow.h from the header.
struct ImageAdjustmentPanel::Widgets {
    // 5 toggles
    QToolButton *btnGray    = nullptr;
    QToolButton *btnInvert  = nullptr;
    QToolButton *btnBinary  = nullptr;
    QToolButton *btnSharpen = nullptr;
    QToolButton *btnEdge    = nullptr;
    // 10 sliders
    QSlider *sliderAlpha     = nullptr;
    QSlider *sliderBeta      = nullptr;
    QSlider *sliderBinary    = nullptr;
    QSlider *sliderBlur      = nullptr;
    QSlider *sliderSharpen   = nullptr;
    QSlider *sliderCannyLow  = nullptr;
    QSlider *sliderCannyHigh = nullptr;
    QSlider *sliderSat       = nullptr;
    QSlider *sliderHue       = nullptr;
    QSlider *sliderExposure  = nullptr;
    // 10 value labels
    QLabel *lblAlphaVal     = nullptr;
    QLabel *lblBetaVal      = nullptr;
    QLabel *lblBinaryVal    = nullptr;
    QLabel *lblBlurVal      = nullptr;
    QLabel *lblSharpenVal   = nullptr;
    QLabel *lblCannyLowVal  = nullptr;
    QLabel *lblCannyHighVal = nullptr;
    QLabel *lblSatVal       = nullptr;
    QLabel *lblHueVal       = nullptr;
    QLabel *lblExposureVal  = nullptr;
};

ImageAdjustmentPanel::ImageAdjustmentPanel(QWidget* parent) : QWidget(parent) {}
ImageAdjustmentPanel::~ImageAdjustmentPanel() { delete m_ui; }

void ImageAdjustmentPanel::setUi(Ui::ImageWindow* ui) {
    if (m_ui) { delete m_ui; m_ui = nullptr; }
    auto* w = new Widgets;
    // 5 toggles
    w->btnGray         = ui->btnGray;
    w->btnInvert       = ui->btnInvert;
    w->btnBinary       = ui->btnBinary;
    w->btnSharpen      = ui->btnSharpen;
    w->btnEdge         = ui->btnEdge;
    // 10 sliders
    w->sliderAlpha     = ui->sliderAlpha;
    w->sliderBeta      = ui->sliderBeta;
    w->sliderBinary    = ui->sliderBinary;
    w->sliderBlur      = ui->sliderBlur;
    w->sliderSharpen   = ui->sliderSharpen;
    w->sliderCannyLow  = ui->sliderCannyLow;
    w->sliderCannyHigh = ui->sliderCannyHigh;
    w->sliderSat       = ui->sliderSat;
    w->sliderHue       = ui->sliderHue;
    w->sliderExposure  = ui->sliderExposure;
    // 10 value labels
    w->lblAlphaVal     = ui->lblAlphaVal;
    w->lblBetaVal      = ui->lblBetaVal;
    w->lblBinaryVal    = ui->lblBinaryVal;
    w->lblBlurVal      = ui->lblBlurVal;
    w->lblSharpenVal   = ui->lblSharpenVal;
    w->lblCannyLowVal  = ui->lblCannyLowVal;
    w->lblCannyHighVal = ui->lblCannyHighVal;
    w->lblSatVal       = ui->lblSatVal;
    w->lblHueVal       = ui->lblHueVal;
    w->lblExposureVal  = ui->lblExposureVal;
    m_ui = w;
}

void ImageAdjustmentPanel::notifyParamChanged() { emit paramChanged(); }

// ------------------------------------------------------------------
// applyCurrentParams — P0-2 (2026-09-08) 改为 dispatch
//   原 API 行为保留 (原同步实现仍在 syncApplyCurrentParams 里)
//   默认走 async (m_host->asyncEnabled() == true), slider 拖动不卡 UI
//   loadFile 之类需要"等图加载完才能继续"的场景, 调 syncApplyCurrentParams 直跑
//
// 切换策略:
//   - m_host->asyncEnabled() == true (默认) → asyncApplyCurrentParams
//   - m_host->asyncEnabled() == false → syncApplyCurrentParams (测试 / 调试用)
//   切换不影响行为正确性, 只影响 UI 响应速度
// ------------------------------------------------------------------
void ImageAdjustmentPanel::applyCurrentParams(bool repaint) {
    if (m_host && m_host->asyncEnabled()) {
        asyncApplyCurrentParams(repaint);
    } else {
        syncApplyCurrentParams(repaint);
    }
}

// ------------------------------------------------------------------
// syncApplyCurrentParams — full image filter pipeline + cache short-circuit
//   P0-1.3 (2026-09-07): moved from ImageWindow verbatim. Only the
//   field accesses (m_original / m_current / m_inUndoRedo / m_infoPanel /
//   m_undoStack) go through m_host now.
//   P0-2 (2026-09-08): 重命名, 跟 asyncApplyCurrentParams 配对, 测试用
// ------------------------------------------------------------------
void ImageAdjustmentPanel::syncApplyCurrentParams(bool repaint) {
    if (!m_host || !m_ui) return;
    // 把 m_params 推到 UI (不触发 valueChanged / toggled)
    syncUiFromParams(m_params);
    updateLabel();
    if (repaint) {
        // 计算 + 渲染
        if (m_host->originalImage().empty()) return;
        // 阶段 1 Step A Bug 3 (2026-09-04): 参数未变 → 跳过整个 pipeline
        //   m_current 已经是上次的结果, 直接刷新显示即可
        //   解决切 tab / undo 触发 applyCurrentParams 时白白跑 OpenCV
        //   注: loadFile 走 invalidateParamCache() 强制下次重算
        if (m_hasLastApplied && paramsUnchanged() && !m_host->currentImage().empty()) {
            m_host->update();
            return;
        }
        cv::Mat work = m_host->originalImage().clone();

        if (m_params.toggleGray) {
            cv::Mat g; ImageProcessor::toGray(work, g); work = g;
        }
        if (m_params.toggleBinary) {
            cv::Mat b; ImageProcessor::toBinary(work, b, m_params.binaryThresh); work = b;
        }
        // 阶段 1 Step A Bug 2 (2026-09-04): brightness 默认中性时跳过
        //   之前无条件跑, 浪费 CPU, 更糟的是 alpha=1,beta=0 时仍然分配 Mat + 像素遍历
        //   跟曝光/饱和度/色相一样按"是否中性"条件触发
        if (m_params.alphaPct != 100 || m_params.beta != 0) {
            cv::Mat bc; ImageProcessor::brightnessContrast(work, bc,
                m_params.alphaPct / 100.0, m_params.beta); work = bc;
        }
        // 曝光
        if (m_params.exposurePct != 0) {
            cv::Mat e; ImageProcessor::exposure(work, e, m_params.exposurePct / 100.0); work = e;
        }
        // 饱和度
        if (m_params.satPct != 100) {
            cv::Mat s; ImageProcessor::saturation(work, s, m_params.satPct / 100.0); work = s;
        }
        // 色相
        if (m_params.hueShift != 0) {
            cv::Mat h; ImageProcessor::hueShift(work, h, m_params.hueShift); work = h;
        }
        // 阶段 1 Step A Bug 2 (2026-09-04): blur 加 toggle 开关
        //   之前默认 ksize=5 无条件跑, 用户看到初始图就是模糊的
        //   修法: 必须 toggleBlur=true 才跑, 跟 gray/binary/sharpen 同模式
        if (m_params.toggleBlur) {
            cv::Mat b; ImageProcessor::gaussianBlur(work, b, m_params.ksizeBlur, 0); work = b;
        }
        if (m_params.toggleSharpen) {
            cv::Mat lap;
            cv::Laplacian(work, lap, CV_16S, 3);
            cv::convertScaleAbs(lap, lap);
            cv::Mat out = work - lap;
            for (int i = 1; i < m_params.sharpenAmt; ++i) {
                cv::Mat lap2;
                cv::Laplacian(out, lap2, CV_16S, 3);
                cv::convertScaleAbs(lap2, lap2);
                out = out - lap2;
            }
            work = out;
        }
        if (m_params.toggleEdge) {
            cv::Mat e; ImageProcessor::edgeDetect(work, e,
                m_params.cannyLow, m_params.cannyHigh); work = e;
        }
        if (m_params.toggleInvert) {
            cv::Mat inv; ImageProcessor::invert(work, inv); work = inv;
        }
        m_host->setCurrentImage(work);
        // 阶段 1 Step A Bug 3 (2026-09-04): 缓存 last applied, 下次直接跳过
        m_lastAppliedParams = m_params;
        m_hasLastApplied = true;
    }
}

// ------------------------------------------------------------------
// asyncApplyCurrentParams — P0-2 (2026-09-08) 新增
//   走 m_host->engineContext()->background().submit(...) 后台跑 OpenCV
//   pipeline, 主线程立即回 (slider 拖动不卡 UI)
//   完成后用 QMetaObject::invokeMethod 跨线程 emit, 切回主线程刷显示
//
// 设计要点 (一次性到位, P0-2 不预留补丁):
//   1. UI 部分 (syncUiFromParams + updateLabel) 仍然主线程立即跑
//      → slider 拖动有即时反馈 (label / toggle 状态)
//   2. OpenCV pipeline 后台跑, 完成后切回主线程
//   3. m_currentTaskId 原子递增做"取消" - 老 task 完成后主线程发现 id 变了
//      直接 return, 不刷显示. 这是 P0-2 范围内的轻量级取消机制
//      (未来 P0-3+ 用 CancellationToken 更彻底)
//   4. m_hasLastApplied 缓存: 参数未变时 UI 立即刷, 后台不跑
//   5. paramsUnchanged + non-empty current: 完全跳过, 跟 sync 版一致
//   6. async 路径下, m_lastAppliedParams 在回调里设 (后台跑完才更新缓存)
// ------------------------------------------------------------------
void ImageAdjustmentPanel::asyncApplyCurrentParams(bool repaint) {
    if (!m_host || !m_ui) return;
    // UI 部分主线程立即跑 - 跟 sync 版前 7 行一致
    syncUiFromParams(m_params);
    updateLabel();
    if (!repaint) return;
    if (m_host->originalImage().empty()) return;
    if (m_hasLastApplied && paramsUnchanged() && !m_host->currentImage().empty()) {
        m_host->update();
        return;
    }

    // P0-2 关键: 分配一个新 task id, 旧的 in-flight task 完成后主线程发现
    //   currentTaskId 已经不是自己的 id, 直接 return, 不刷显示
    const uint64_t myTaskId = m_host->currentTaskId().fetch_add(1) + 1;
    // 拷一份 params 给后台闭包, 避免后台跑一半 m_params 变了
    const ImageEditCommand::ParamSet before = m_params;

    auto* engine = m_host->engineContext();
    if (!engine) {
        // 兜底: engine 没初始化, 走同步
        syncApplyCurrentParams(repaint);
        return;
    }

    // 主线程 imageWindow 指针 (Qt queued connection 需要 context 存活)
    // 用 QPointer 防止 ImageWindow 销毁后 invokeMethod 失效
    QPointer<ImageWindow> hostGuard = m_host;

    // 拷贝 original 进去, 避免后台跑一半 m_original 变了 (loadFile 时)
    // 注意: clone 是必须的, m_host->originalImage() 后续 loadFile 会改引用
    cv::Mat originalSnapshot;
    {
        const cv::Mat& origRef = m_host->originalImage();
        if (origRef.empty()) return;
        originalSnapshot = origRef.clone();
    }

    // submit 到 background pool - IExecutor::submit_fn 包装
    // Task::Fn 签名: void(TaskContext&)
    const bool submitted = engine->background().submit_fn(
        [hostGuard, myTaskId, before, originalSnapshot = std::move(originalSnapshot)](
            vistella::tp::TaskContext& /*ctx*/) mutable -> void
        {
            // 后台跑完整 pipeline (跟 sync 版同样逻辑, 但用 before 而不是 m_params)
            cv::Mat work = originalSnapshot.clone();

            if (before.toggleGray) {
                cv::Mat g; ImageProcessor::toGray(work, g); work = g;
            }
            if (before.toggleBinary) {
                cv::Mat b; ImageProcessor::toBinary(work, b, before.binaryThresh); work = b;
            }
            if (before.alphaPct != 100 || before.beta != 0) {
                cv::Mat bc; ImageProcessor::brightnessContrast(work, bc,
                    before.alphaPct / 100.0, before.beta); work = bc;
            }
            if (before.exposurePct != 0) {
                cv::Mat e; ImageProcessor::exposure(work, e, before.exposurePct / 100.0); work = e;
            }
            if (before.satPct != 100) {
                cv::Mat s; ImageProcessor::saturation(work, s, before.satPct / 100.0); work = s;
            }
            if (before.hueShift != 0) {
                cv::Mat h; ImageProcessor::hueShift(work, h, before.hueShift); work = h;
            }
            if (before.toggleBlur) {
                cv::Mat b; ImageProcessor::gaussianBlur(work, b, before.ksizeBlur, 0); work = b;
            }
            if (before.toggleSharpen) {
                cv::Mat lap;
                cv::Laplacian(work, lap, CV_16S, 3);
                cv::convertScaleAbs(lap, lap);
                cv::Mat out = work - lap;
                for (int i = 1; i < before.sharpenAmt; ++i) {
                    cv::Mat lap2;
                    cv::Laplacian(out, lap2, CV_16S, 3);
                    cv::convertScaleAbs(lap2, lap2);
                    out = out - lap2;
                }
                work = out;
            }
            if (before.toggleEdge) {
                cv::Mat e; ImageProcessor::edgeDetect(work, e,
                    before.cannyLow, before.cannyHigh); work = e;
            }
            if (before.toggleInvert) {
                cv::Mat inv; ImageProcessor::invert(work, inv); work = inv;
            }

            // 跨线程切回主线程
            if (hostGuard.isNull()) return;  // ImageWindow 已销毁
            ImageWindow* host = hostGuard.data();
            // 拷 work 到回调闭包 (move 进去, work 不再需要)
            QMetaObject::invokeMethod(host,
                [host, myTaskId, work = std::move(work), before]() -> void {
                    // 再次校验: 已被新 task 取代
                    if (host->currentTaskId().load() != myTaskId) return;
                    host->setCurrentImage(work);
                    // 更新 m_lastAppliedParams: 这要在 panel 持有端做
                    // 找 m_adjustment -> 通过 m_host 访问
                    auto* adj = host->m_adjustment.get();
                    if (adj) {
                        adj->m_lastAppliedParams = before;
                        adj->m_hasLastApplied = true;
                    }
                },
                Qt::QueuedConnection);
        },
        "ImageAdjustmentPanel::asyncApply",
        vistella::tp::Priority::Normal);

    if (!submitted) {
        // 提交失败 (池满 / shutting down), 走同步兜底
        // 但 sync 会让 UI 阻塞, 这是 P0-2 阶段接受的兜底行为
        syncApplyCurrentParams(repaint);
    }
}

// 阶段 1 Step A Bug 3 (2026-09-04): ParamSet 字段逐一比较
//   不依赖 memcmp (struct 有 padding), 也不依赖 hash (粒度粗)
//   直接字段比较, 编译器自动短路
bool ImageAdjustmentPanel::paramsUnchanged() const {
    if (m_lastAppliedParams.toggleGray    != m_params.toggleGray)    return false;
    if (m_lastAppliedParams.toggleInvert  != m_params.toggleInvert)  return false;
    if (m_lastAppliedParams.toggleBinary  != m_params.toggleBinary)  return false;
    if (m_lastAppliedParams.toggleSharpen != m_params.toggleSharpen) return false;
    if (m_lastAppliedParams.toggleEdge    != m_params.toggleEdge)    return false;
    if (m_lastAppliedParams.toggleBlur    != m_params.toggleBlur)    return false;
    if (m_lastAppliedParams.alphaPct      != m_params.alphaPct)      return false;
    if (m_lastAppliedParams.beta          != m_params.beta)          return false;
    if (m_lastAppliedParams.binaryThresh  != m_params.binaryThresh)  return false;
    if (m_lastAppliedParams.ksizeBlur     != m_params.ksizeBlur)     return false;
    if (m_lastAppliedParams.sharpenAmt    != m_params.sharpenAmt)    return false;
    if (m_lastAppliedParams.cannyLow      != m_params.cannyLow)      return false;
    if (m_lastAppliedParams.cannyHigh     != m_params.cannyHigh)     return false;
    if (m_lastAppliedParams.satPct        != m_params.satPct)        return false;
    if (m_lastAppliedParams.hueShift      != m_params.hueShift)      return false;
    if (m_lastAppliedParams.exposurePct   != m_params.exposurePct)   return false;
    return true;
}

// 阶段 1 Step A Bug (2026-09-04) 关键根因修复
//   .ui 文件里 sliders 全部没设 range, QSlider 默认 0-99
//   代码 setValue(100) (alphaPct 默认 100) 被 clamp 到 99 → alpha=0.99 vs 1.00 用户完全看不出
//   修法: 在代码里 setRange, 默认值 + step 一并设
//   对应 ParamSet 默认值:
//     alphaPct=100 (0-200, 100=原图)  / beta=0 (-100..100, 0=原图)
//     binaryThresh=128 (0-255)        / ksizeBlur=5 (1-99, 必须奇数, step=2)
//     sharpenAmt=1 (1-5, 整数)         / cannyLow/High=80/180 (0-500, step=10)
//     satPct=100 (0-200, 100=原图)     / hueShift=0 (-180..180)
//     exposurePct=0 (-100..100, 0=原图)
void ImageAdjustmentPanel::setupSliderRanges() {
    if (!m_host || !m_ui) return;
    // 阶段 1 Step A Bug DEBUG (2026-09-04): 临时 debug log
    qDebug() << "[setupSliderRanges] START";
    // 对比度 0-200, 100=原图, step=1
    m_ui->sliderAlpha->setRange(0, 200);
    qDebug() << "[setupSliderRanges] sliderAlpha range=" << m_ui->sliderAlpha->minimum() << "-" << m_ui->sliderAlpha->maximum();
    m_ui->sliderAlpha->setValue(m_params.alphaPct);  // 100
    // 亮度 -100..100, 0=原图
    m_ui->sliderBeta->setRange(-100, 100);
    m_ui->sliderBeta->setValue(m_params.beta);       // 0
    // 二值化阈值 0-255
    m_ui->sliderBinary->setRange(0, 255);
    m_ui->sliderBinary->setValue(m_params.binaryThresh);  // 128
    // 模糊 1-99 (奇数), step=2 保证奇数 (1,3,5,...,99)
    m_ui->sliderBlur->setRange(1, 99);
    m_ui->sliderBlur->setSingleStep(2);
    m_ui->sliderBlur->setValue(m_params.ksizeBlur);  // 5
    // 锐化强度 1-5, 整数
    m_ui->sliderSharpen->setRange(1, 5);
    m_ui->sliderSharpen->setValue(m_params.sharpenAmt);  // 1
    // Canny 0-500, step=10
    m_ui->sliderCannyLow->setRange(0, 500);
    m_ui->sliderCannyLow->setSingleStep(10);
    m_ui->sliderCannyLow->setValue(m_params.cannyLow);  // 80
    m_ui->sliderCannyHigh->setRange(0, 500);
    m_ui->sliderCannyHigh->setSingleStep(10);
    m_ui->sliderCannyHigh->setValue(m_params.cannyHigh);  // 180
    // 饱和度 0-200, 100=原图
    m_ui->sliderSat->setRange(0, 200);
    m_ui->sliderSat->setValue(m_params.satPct);  // 100
    // 色相 -180..180
    m_ui->sliderHue->setRange(-180, 180);
    m_ui->sliderHue->setValue(m_params.hueShift);  // 0
    // 曝光 -100..100, 0=原图
    m_ui->sliderExposure->setRange(-100, 100);
    m_ui->sliderExposure->setValue(m_params.exposurePct);  // 0
    qDebug() << "[setupSliderRanges] END";
}

void ImageAdjustmentPanel::syncUiFromParams(const ImageEditCommand::ParamSet &p) {
    if (!m_ui) return;
    auto setBtn = [this](QToolButton *b, bool v) {
        b->blockSignals(true);
        b->setChecked(v);
        b->blockSignals(false);
    };
    setBtn(m_ui->btnGray,    p.toggleGray);
    setBtn(m_ui->btnInvert,  p.toggleInvert);
    setBtn(m_ui->btnBinary,  p.toggleBinary);
    setBtn(m_ui->btnSharpen, p.toggleSharpen);
    setBtn(m_ui->btnEdge,    p.toggleEdge);

    auto setSlider = [this](QSlider *s, int v) {
        s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(false);
    };
    setSlider(m_ui->sliderAlpha,    p.alphaPct);
    setSlider(m_ui->sliderBeta,     p.beta);
    setSlider(m_ui->sliderBinary,   p.binaryThresh);
    setSlider(m_ui->sliderBlur,     p.ksizeBlur);
    setSlider(m_ui->sliderSharpen,  p.sharpenAmt);
    setSlider(m_ui->sliderCannyLow, p.cannyLow);
    setSlider(m_ui->sliderCannyHigh,p.cannyHigh);
    setSlider(m_ui->sliderSat,      p.satPct);
    setSlider(m_ui->sliderHue,      p.hueShift);
    setSlider(m_ui->sliderExposure, p.exposurePct);
}

void ImageAdjustmentPanel::onAnyParamChanged() {
    if (!m_host || !m_ui) return;
    // 阶段 1 Step A Bug DEBUG (2026-09-04): 临时 debug log
    qDebug() << "[ImageAdjustmentPanel] onAnyParamChanged m_inUndoRedo=" << m_host->m_inUndoRedo
             << "m_original.empty=" << m_host->originalImage().empty();
    if (m_host->m_inUndoRedo) return;
    if (m_host->originalImage().empty()) return;

    // 读取 UI 当前状态作为 after
    auto snap = [this]() {
        ImageEditCommand::ParamSet p = m_params;  // 起点是当前
        p.toggleGray    = m_ui->btnGray->isChecked();
        p.toggleInvert  = m_ui->btnInvert->isChecked();
        p.toggleBinary  = m_ui->btnBinary->isChecked();
        p.toggleSharpen = m_ui->btnSharpen->isChecked();
        p.toggleEdge    = m_ui->btnEdge->isChecked();
        p.alphaPct   = m_ui->sliderAlpha->value();
        p.beta       = m_ui->sliderBeta->value();
        p.binaryThresh = m_ui->sliderBinary->value();
        p.ksizeBlur  = m_ui->sliderBlur->value();
        if (p.ksizeBlur % 2 == 0) {
            p.ksizeBlur += 1;
            m_ui->sliderBlur->blockSignals(true);
            m_ui->sliderBlur->setValue(p.ksizeBlur);
            m_ui->sliderBlur->blockSignals(false);
        }
        // 阶段 1 Step A Bug 2 (2026-09-04): 用户动 blur slider → 自动开启 blur
        //   没有 UI checkbox, 简化策略: 一旦动 slider 就开 blur
        //   后续可加 btnBlur toggle, 默认 false
        if (m_ui->sliderBlur->value() != m_params.ksizeBlur) {
            p.toggleBlur = true;
        }
        p.sharpenAmt = m_ui->sliderSharpen->value();
        int low  = m_ui->sliderCannyLow->value();
        int high = m_ui->sliderCannyHigh->value();
        if (high < low + 10) {
            high = low + 10;
            m_ui->sliderCannyHigh->blockSignals(true);
            m_ui->sliderCannyHigh->setValue(high);
            m_ui->sliderCannyHigh->blockSignals(false);
        }
        p.cannyLow  = low;
        p.cannyHigh = high;
        p.satPct     = m_ui->sliderSat->value();
        p.hueShift   = m_ui->sliderHue->value();
        p.exposurePct = m_ui->sliderExposure->value();
        return p;
    };

    ImageEditCommand::ParamSet after = snap();
    if (after.toggleGray    == m_params.toggleGray    &&
        after.toggleInvert  == m_params.toggleInvert  &&
        after.toggleBinary  == m_params.toggleBinary  &&
        after.toggleSharpen == m_params.toggleSharpen &&
        after.toggleEdge    == m_params.toggleEdge    &&
        after.toggleBlur    == m_params.toggleBlur    &&   // 阶段 1 Step A Bug 2
        after.alphaPct      == m_params.alphaPct      &&
        after.beta          == m_params.beta          &&
        after.binaryThresh  == m_params.binaryThresh  &&
        after.ksizeBlur     == m_params.ksizeBlur     &&
        after.sharpenAmt    == m_params.sharpenAmt    &&
        after.cannyLow      == m_params.cannyLow      &&
        after.cannyHigh     == m_params.cannyHigh     &&
        after.satPct        == m_params.satPct        &&
        after.hueShift      == m_params.hueShift      &&
        after.exposurePct   == m_params.exposurePct) {
        // 没变化, 不入栈
        updateLabel();
        return;
    }

    // 描述
    QString text;
    if (after.toggleGray    != m_params.toggleGray)    text = "灰度";
    else if (after.toggleInvert  != m_params.toggleInvert)  text = "反色";
    else if (after.toggleBinary  != m_params.toggleBinary)  text = "二值化";
    else if (after.toggleSharpen != m_params.toggleSharpen) text = "锐化";
    else if (after.toggleEdge    != m_params.toggleEdge)    text = "边缘";
    else if (after.alphaPct      != m_params.alphaPct)      text = "对比度";
    else if (after.beta          != m_params.beta)          text = "亮度";
    else if (after.binaryThresh  != m_params.binaryThresh)  text = "二值阈值";
    else if (after.ksizeBlur     != m_params.ksizeBlur)     text = "模糊";
    else if (after.sharpenAmt    != m_params.sharpenAmt)    text = "锐化强度";
    else if (after.cannyLow      != m_params.cannyLow)      text = "Canny 低";
    else if (after.cannyHigh     != m_params.cannyHigh)     text = "Canny 高";
    else if (after.satPct        != m_params.satPct)        text = "饱和度";
    else if (after.hueShift      != m_params.hueShift)      text = "色相";
    else if (after.exposurePct   != m_params.exposurePct)   text = "曝光";
    else text = "编辑";

    // push 命令: before = m_params (当前栈顶), after = 新的
    m_host->undoStack()->push(new ImageEditCommand(m_host, m_params, after, text));

    // push 后 m_params 还是旧值, 需要在 command 触发 redo 之后才更新
    // 但我们直接调 redo 后 m_params = after; 这里手动同步
    m_params = after;
    updateLabel();
}

void ImageAdjustmentPanel::onSatChanged(int v) {
    if (!m_ui) return;
    m_ui->lblSatVal->setText(QString::number(v / 100.0, 'f', 2));
    onAnyParamChanged();
}

void ImageAdjustmentPanel::onHueChanged(int v) {
    if (!m_ui) return;
    m_ui->lblHueVal->setText(QStringLiteral("%1°").arg(v));
    onAnyParamChanged();
}

void ImageAdjustmentPanel::onExposureChanged(int v) {
    if (!m_ui) return;
    m_ui->lblExposureVal->setText(QString::number(v / 100.0, 'f', 2));
    onAnyParamChanged();
}

void ImageAdjustmentPanel::updateLabel() {
    if (!m_ui) return;
    m_ui->lblAlphaVal->setText(QString::number(m_params.alphaPct / 100.0, 'f', 2));
    m_ui->lblBetaVal->setText(QString::number(m_params.beta));
    m_ui->lblBinaryVal->setText(QString::number(m_params.binaryThresh));
    m_ui->lblBlurVal->setText(QString::number(m_params.ksizeBlur));
    m_ui->lblSharpenVal->setText(QString::number(m_params.sharpenAmt));
    m_ui->lblCannyLowVal->setText(QString::number(m_params.cannyLow));
    m_ui->lblCannyHighVal->setText(QString::number(m_params.cannyHigh));
    m_ui->lblSatVal->setText(QString::number(m_params.satPct / 100.0, 'f', 2));
    m_ui->lblHueVal->setText(QStringLiteral("%1°").arg(m_params.hueShift));
    m_ui->lblExposureVal->setText(QString::number(m_params.exposurePct / 100.0, 'f', 2));
}
