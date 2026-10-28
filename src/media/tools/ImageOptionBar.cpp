// SPDX-License-Identifier: MIT
//
// ImageOptionBar implementation
//
// Include order: imagewindow.h MUST come before ImageOptionBar.h because
//   MosaicTool.h declares a `QPointer<ImageWindow> m_host` member which
//   requires the full ImageWindow Q_OBJECT definition to instantiate
//   QPointer<T>::data()'s `static_cast<QObject*>` validity check.
//
#include "../imagewindow.h"

#include "ImageOptionBar.h"
#include "ui_ImageOptionBar.h"
#include "ToolContext.h"
#include "TransformTool.h"
#include "ShapeTool.h"
#include "PenTool.h"
#include "CloneTool.h"
#include "HealTool.h"
#include "PatchTool.h"
#include "RedEyeTool.h"
#include "MaskBrushTool.h"
#include "Brush.h"        // presetChanged -> brush->setPreset
#include "../brushes/BrushPickerDialog.h"  // P7.6.x: picker button -> modal dialog

#include "../imagewindow/MosaicOptionPanel.h"
#include "../imagewindow/TextOptionPanel.h"

#include "BrushOptionsPanel.h"
#include "logger.h"

#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace tools {

ImageOptionBar::ImageOptionBar(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::ImageOptionBar;
    ui->setupUi(this);
    // F-E stage: default page = None (0)
    ui->stackedWidget->setCurrentIndex(static_cast<int>(mediators::ToolId::None));

    // the "no tool selected" page gets a short hint
    //   The options column is ~300px wide; an empty box of that size reads
    //   as a broken panel rather than as "nothing is selected yet".
    if (QWidget* nonePage = ui->stackedWidget->widget(
            static_cast<int>(mediators::ToolId::None))) {
        auto* noneLayout = new QVBoxLayout(nonePage);
        noneLayout->setContentsMargins(12, 12, 12, 12);
        auto* hint = new QLabel(tr("未选择工具\n请在左侧工具栏中选取工具"), nonePage);
        hint->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        hint->setStyleSheet(QStringLiteral("color: gray;"));
        noneLayout->addWidget(hint);
        noneLayout->addStretch(1);
    }
}

ImageOptionBar::~ImageOptionBar()
{
    delete ui;
}

void ImageOptionBar::attach(ToolContext* ctx,
                            MosaicTool* mosaic,
                            TextOverlayController* textCtrl,
                            BrushOptionsPanel* brushOpt)
{
    // 先断开上一个文档: 不 detach 的话旧连接会留着, 一次点击触发 N 次,
    // 而且旧文档销毁后页面里的裸指针就悬垂了.
    detach();

    m_ctx = ctx;
    m_mosaic = mosaic;
    m_textCtrl = textCtrl;
    m_brushOpt = brushOpt;  // weak ref, optional

    // 已经建好的 Mosaic/Text 页面重新绑到新文档 (页面数量不变,
    // ToolId -> page index 的映射要保持). 页面里只存当前文档的指针,
    // 换文档时立刻换掉, 旧 tool/controller 随文档销毁后不会有人再碰.
    if (m_mosaicPageIdx >= 0) {
        if (auto* page = qobject_cast<MosaicOptionPanel*>(
                ui->stackedWidget->widget(m_mosaicPageIdx))) {
            page->setTool(m_mosaic.data());
        }
    }
    if (m_textPageIdx >= 0) {
        if (auto* page = qobject_cast<TextOptionPanel*>(
                ui->stackedWidget->widget(m_textPageIdx))) {
            page->setController(m_textCtrl.data());
        }
    }

    if (m_ctx) {
        connect(m_ctx.data(), &ToolContext::toolChanged,
                this, &ImageOptionBar::onToolChanged);
        // P5.1: MosaicTool mode changes while on Brush tool must reroute
        //   so we never get stuck showing the wrong page.
        if (m_mosaic) {
            connect(m_mosaic.data(), &MosaicTool::modeChanged,
                    this, &ImageOptionBar::onMosaicModeChanged);
        }
        // BrushOptionsPanel <-> ToolContext.brushSharedState
        //   双向同步 (取代旧 TopOptionBar). 仅在 m_brushOpt 有效时撮合.
        if (m_brushOpt) {
            connect(m_brushOpt, &BrushOptionsPanel::sharedStateChanged,
                    this, &ImageOptionBar::onBrushSharedStateChangedFromPanel);
            connect(m_ctx.data(), &ToolContext::brushSettingsChanged,
                    this, &ImageOptionBar::onToolContextBrushStateChanged);
        }
        // Synchronize initial state (None / Brush / Text depending on
        //   current tool and mosaic enabled flag).
        onToolChanged(m_ctx->currentToolId());
    }
}

void ImageOptionBar::detach()
{
    if (m_ctx) {
        disconnect(m_ctx.data(), nullptr, this, nullptr);
    }
    if (m_mosaic) {
        disconnect(m_mosaic.data(), nullptr, this, nullptr);
    }
    if (m_brushOpt) {
        disconnect(m_brushOpt, nullptr, this, nullptr);
    }
    // 页面里的裸指针要一起断开: 文档销毁后 tool/controller 也没了,
    // 页面还指着就是悬垂指针.
    if (m_mosaicPageIdx >= 0) {
        if (auto* page = qobject_cast<MosaicOptionPanel*>(
                ui->stackedWidget->widget(m_mosaicPageIdx))) {
            page->setTool(nullptr);
        }
    }
    if (m_textPageIdx >= 0) {
        if (auto* page = qobject_cast<TextOptionPanel*>(
                ui->stackedWidget->widget(m_textPageIdx))) {
            page->setController(nullptr);
        }
    }
    m_ctx = nullptr;
    m_mosaic = nullptr;
    m_textCtrl = nullptr;
    m_brushOpt = nullptr;
}

void ImageOptionBar::installBrushOptionsPage(BrushOptionsPanel* panel)
{
    if (!panel) return;
    const int idx = static_cast<int>(mediators::ToolId::Brush);
    if (idx < 0 || idx >= ui->stackedWidget->count()) {
        LOG_WARN("[OptionBar] installBrushOptionsPage: no slot for ToolId::Brush "
                 "(idx={}), page count={}", idx, ui->stackedWidget->count());
        return;
    }

    const int cur = ui->stackedWidget->currentIndex();

    QWidget* old = ui->stackedWidget->widget(idx);
    if (old == panel) {
        m_brushOpt = panel;
        return;
    }
    if (old) {
        // The .ui ships an empty placeholder page for the Brush slot. Swap
        // it out, but keep the page count (and therefore the ToolId -> page
        // index mapping) exactly as it was.
        ui->stackedWidget->removeWidget(old);
        old->hide();
        old->deleteLater();
    }

    // From here on the panel is laid out by the stacked widget instead of
    // floating over the main window.
    panel->setParent(ui->stackedWidget);
    panel->setObjectName(QStringLiteral("page7"));
    ui->stackedWidget->insertWidget(idx, panel);
    m_brushOpt = panel;

    if (cur >= 0 && cur < ui->stackedWidget->count())
        ui->stackedWidget->setCurrentIndex(cur);

    LOG_INFO("[OptionBar] installed BrushOptionsPanel as page {}", idx);
}

void ImageOptionBar::ensureMosaicPage()
{
    if (!m_mosaic) return;
    if (m_mosaicPageIdx >= 0) {
        // Already added.
        return;
    }
    auto* page = new MosaicOptionPanel(m_mosaic, this);
    page->setObjectName(QStringLiteral("pageMosaic"));
    ui->stackedWidget->addWidget(page);
    m_mosaicPageIdx = ui->stackedWidget->count() - 1;
    LOG_INFO("[OptionBar] added Mosaic page (idx={}, size/type toggle)",
             m_mosaicPageIdx);
}

void ImageOptionBar::ensureTextPage()
{
    if (!m_textCtrl) return;
    if (m_textPageIdx >= 0) {
        // Already added.
        return;
    }
    auto* page = new TextOptionPanel(m_textCtrl, this);
    page->setObjectName(QStringLiteral("pageText"));
    ui->stackedWidget->addWidget(page);
    m_textPageIdx = ui->stackedWidget->count() - 1;
    LOG_INFO("[OptionBar] added Text page (idx={}, font/size/color/content)",
             m_textPageIdx);
}

// TODO(P8.5): the mosaic branch always wins, so the brush options page is unreachable -- see docs/DEBT.md#A-BRUSHOPTIONS
void ImageOptionBar::onToolChanged(mediators::ToolId id)
{
    const int idx = static_cast<int>(id);
    if (idx < 0 || idx >= ui->stackedWidget->count()) {
        // Transform tool (idx=9) dynamic addPage
        // MaskBrush (idx=16) same dynamic path
        if (id == mediators::ToolId::Transform) {
            const int transformIdx = static_cast<int>(mediators::ToolId::Transform);
            if (transformIdx >= ui->stackedWidget->count()) {
                if (m_ctx) {
                    if (auto* tool = dynamic_cast<TransformTool*>(m_ctx->currentState())) {
                        if (QWidget* page = tool->optionPage(this)) {
                            page->setObjectName(QStringLiteral("pageTransform"));
                            ui->stackedWidget->addWidget(page);
                            LOG_INFO("[OptionBar] added Transform page (4 mode combo + Shift toggle)");
                        }
                    }
                }
            }
            ui->stackedWidget->setCurrentIndex(transformIdx);
        } else if (id == mediators::ToolId::MaskBrush) {
            const int maskIdx = static_cast<int>(mediators::ToolId::MaskBrush);
            if (maskIdx >= ui->stackedWidget->count()) {
                if (m_ctx) {
                    if (auto* tool = dynamic_cast<MaskBrushTool*>(m_ctx->currentState())) {
                        if (QWidget* page = tool->optionPage(this)) {
                            page->setObjectName(QStringLiteral("pageMaskBrush"));
                            ui->stackedWidget->addWidget(page);
                            LOG_INFO("[OptionBar] added MaskBrush page (MaskOptionsPanel)");
                        }
                    }
                }
            }
            ui->stackedWidget->setCurrentIndex(maskIdx);
        } else {
            LOG_WARN("[OptionBar] toolChanged id={} out of range (count={})", idx, ui->stackedWidget->count());
        }
        return;
    }

    // P5.1 route Brush -> Mosaic page and Text -> Text page BEFORE the
    //   generic ToolState::optionPage swap, otherwise the existing swap
    //   would plant BrushOptionsPanel at idx 7 and overwrite our Mosaic page.
    if (id == mediators::ToolId::Brush && m_mosaic) {
        ensureMosaicPage();
        ui->stackedWidget->setCurrentIndex(m_mosaicPageIdx);
        return;
    }
    if (id == mediators::ToolId::Text && m_textCtrl) {
        ensureTextPage();
        ui->stackedWidget->setCurrentIndex(m_textPageIdx);
        return;
    }

    // Shape tool (idx=10) dynamic load optionPage
    //   P0-9.2/9.3 same pattern (Pen/Clone/Heal/Patch/RedEye).
    // Brush (idx=7) 走 m_brushOpt 持久 widget, 不再调
    //   Brush::optionPage() 每次新建 (老代码会换掉我们挂的 BrushOptionsPanel,
    //   导致撮合丢失). 重新接 presetChanged / pickerRequested 到当前 Brush 工具.
    if (m_ctx) {
        if (idx == static_cast<int>(mediators::ToolId::Brush) && m_brushOpt) {
            // The brush panel is installed up front (installBrushOptionsPage),
            // so it already owns the Brush slot. This call is idempotent and
            // only repairs the slot if something else took it over.
            if (ui->stackedWidget->widget(idx) != m_brushOpt)
                installBrushOptionsPage(m_brushOpt);
            // 重接 presetChanged / pickerRequested (Brush 实例每次切工具重建).
            // 捕获 m_ctx raw pointer — imagewindow 生命周期内稳定. m_brushOpt 作为
            //   receiver context, panel 销毁时 Qt 自动 disconnect.
            QObject::connect(m_brushOpt, &BrushOptionsPanel::presetChanged,
                             m_brushOpt, [this](const brushes::BrushPreset& p) {
                if (auto* brush = dynamic_cast<Brush*>(m_ctx->currentState())) {
                    brush->setPreset(p);
                }
            });
            QObject::connect(m_brushOpt, &BrushOptionsPanel::pickerRequested,
                             m_brushOpt, [this]() {
                brushes::BrushPickerDialog dlg(nullptr, m_brushOpt);
                if (dlg.exec() == QDialog::Accepted) {
                    const auto p = dlg.selectedPreset();
                    if (!p.name.isEmpty()) {
                        if (auto* brush = dynamic_cast<Brush*>(m_ctx->currentState())) {
                            brush->setPreset(p);
                        }
                        m_brushOpt->setPreset(p);
                    }
                }
            });
            // 启动时初始化 UI 显示当前 Brush preset (如果当前已经是 Brush 工具).
            if (auto* brush = dynamic_cast<Brush*>(m_ctx->currentState())) {
                m_brushOpt->setPreset(brush->preset());
            }
            LOG_DEBUG("[OptionBar] onToolChanged: Brush -> m_brushOpt (persistent)");
            ui->stackedWidget->setCurrentIndex(idx);
            return;
        }
        if (auto* tool = m_ctx->currentState()) {
            if (auto* page = tool->optionPage(this)) {
                page->setObjectName(QString(QStringLiteral("page%1")).arg(idx));
                QWidget* old = ui->stackedWidget->widget(idx);
                if (old && old != page) {
                    ui->stackedWidget->removeWidget(old);
                    old->deleteLater();
                }
                ui->stackedWidget->insertWidget(idx, page);
            }
        }
    }

    LOG_DEBUG("[OptionBar] onToolChanged: id={} idx={}", idx, idx);
    ui->stackedWidget->setCurrentIndex(idx);
}

void ImageOptionBar::onMosaicModeChanged(bool /*on*/)
{
    // P5.1: MosaicOptionPanel itself listens to MosaicTool::modeChanged and
    //   updates the on/off button label, so no page reroute is needed here.
    //   The slot exists because we connect MosaicTool::modeChanged in attach()
    //   so test code (and external tooling) can verify the route is wired.
    //   If you switch this to actually reroute, remember to also flip the
    //   Brush default page swap so idx=7 stays reachable as a fallback option.
    if (!m_ctx) return;
    if (m_ctx->currentToolId() != mediators::ToolId::Brush) return;
    // Page stays on kMosaicPageIdx (17). MosaicOptionPanel reflects new mode.
}

// BrushOptionsPanel 撮合
//   取代旧 TopOptionBar (imagewindow.cpp 顶部 6 控件). 6 字段:
//     blendMode / size / opacity / flow / angle / smoothing.
//   已经在 BrushOptionsPanel 现有控件里的 5 项通过 sharedStateChanged 撮合,
//   BlendMode 单独由 onBlendModeChanged 触发共享 signal.
//   撮合策略:
//     BrushOptionsPanel 改 UI -> sharedStateChanged -> 我们读 sharedStatePulledIn()
//       -> 写 6 个 ToolContext setter -> ToolContext 发 brushSettingsChanged ->
//       onToolContextBrushStateChanged -> applySharedState 回灌 UI (UI 没变, QSignalBlocker
//       抑制二次 emit, 防止循环).
//   反向: ToolContext::setX (切 tool / load file) -> brushSettingsChanged -> 我们
//     applySharedState 回灌 UI.

void ImageOptionBar::onBrushSharedStateChangedFromPanel()
{
    if (!m_ctx || !m_brushOpt) return;
    const auto s = m_brushOpt->sharedStatePulledIn();
    // bulk set (1 signal instead of 9). The 3 new
    // fields (hardness/spacing/scatter) flow through automatically.
    // Use named-aggregate-init to avoid double<->int narrowing (C2397).
    m_ctx->setBrushSharedState(tools::BrushSharedState{
        /*blendMode*/ s.blendMode,
        /*opacity  */ s.opacityPct,
        /*flow     */ s.flowPct,
        /*smoothing*/ s.smoothing,
        /*angle    */ s.angle,
        /*brushSize*/ s.size,
        /*hardness */ s.hardness,
        /*spacing  */ s.spacing,
        /*scatter  */ s.scatter
    });
}

void ImageOptionBar::onToolContextBrushStateChanged()
{
    if (!m_ctx || !m_brushOpt) return;
    const auto& bs = m_ctx->brushSharedState();
    BrushOptionsPanel::SharedState s;
    s.blendMode  = bs.blendMode;
    s.size       = bs.brushSize;
    s.opacityPct = bs.opacity;
    s.flowPct    = bs.flow;
    s.smoothing  = bs.smoothing;
    s.angle      = bs.angle;
    // P7.9.1: 3 new advanced fields
    s.hardness   = bs.hardness;
    s.spacing    = bs.spacing;
    s.scatter    = bs.scatter;
    m_brushOpt->applySharedState(s);
}

} // namespace tools
