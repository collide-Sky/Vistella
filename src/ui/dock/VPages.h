// VPages.h
//
// Concrete VTabPageBase adapters for each existing tab widget in the
// project. Each adapter wraps the existing widget in a VBoxLayout and
// declares its PS调研-derived preferredRenderPath. Commit 4 will rewrite
// the containing docks (ColorDock, RightPanelDock, etc) to host these
// adapters inside a VDraggableTabWidget.
//
// Render-path rationale (PS调研第十点整合):
//   - CpuOnly   : small UI controls (color picker, swatch click, simple
//                 list) where GPU adds overhead without gain.
//   - GpuPreferred : real pixel work (curves/levels, channel split,
//                    gradient render) that benefits from SIMD/GPU.
//   - Auto      : pure UI (tree views, layer panel list) where the page
//                 does no heavy pixel work; capability layer decides.
//
//   P7.6.x v3 commit 3 (2026-10-14).
#pragma once

#include "VTabPageBase.h"

#include "../../media/docks/ColorTab.h"
#include "../../media/docks/SwatchTab.h"
#include "../../media/docks/GradientTab.h"
#include "../../media/docks/PatternTab.h"
#include "../../media/docks/ChannelPathPanel.h"
#include "../../media/docks/LayersDock.h"
#include "../../media/docks/PropertiesDock.h"
#include "../../media/docks/HistoryDock.h"
#include "../../media/imagewindow/AdjustmentPanel.h"

#include <QWidget>
#include <QVBoxLayout>

namespace vistella::ui {

// ---------------------------------------------------------------------------
// Sub-dock 1: 颜色 / 色板 / 渐变 / 图章 (颜色 tab 4 个)
// ---------------------------------------------------------------------------

class VColorPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VColorPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("color"); }
    QString pageTitle() const { return QStringLiteral("Color"); }
    RenderPath preferredRenderPath() const { return RenderPath::CpuOnly; }
    docks::ColorTab* inner() const { return m_inner; }
private:
    docks::ColorTab* m_inner = nullptr;
};

class VSwatchPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VSwatchPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("swatch"); }
    QString pageTitle() const { return QStringLiteral("Swatch"); }
    RenderPath preferredRenderPath() const { return RenderPath::Auto; }
    docks::SwatchTab* inner() const { return m_inner; }
private:
    docks::SwatchTab* m_inner = nullptr;
};

class VGradientPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VGradientPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("gradient"); }
    QString pageTitle() const { return QStringLiteral("Gradient"); }
    RenderPath preferredRenderPath() const { return RenderPath::CpuOnly; }
    docks::GradientTab* inner() const { return m_inner; }
private:
    docks::GradientTab* m_inner = nullptr;
};

class VPatternPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VPatternPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("pattern"); }
    QString pageTitle() const { return QStringLiteral("Pattern"); }
    RenderPath preferredRenderPath() const { return RenderPath::CpuOnly; }
    docks::PatternTab* inner() const { return m_inner; }
private:
    docks::PatternTab* m_inner = nullptr;
};

// ---------------------------------------------------------------------------
// Sub-dock 2: 图层 / 调整
// ---------------------------------------------------------------------------

class VLayerPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VLayerPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("layer"); }
    QString pageTitle() const { return QStringLiteral("Layers"); }
    // Layer list: pure UI. P8.1 incremental composite invalidation will
    // hook this page's view refresh.
    RenderPath preferredRenderPath() const { return RenderPath::Auto; }
    docks::LayersDock* inner() const { return m_inner; }
private:
    docks::LayersDock* m_inner = nullptr;
};

class VAdjustmentPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VAdjustmentPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("adjustment"); }
    QString pageTitle() const { return QStringLiteral("Adjustments"); }
    // Curves/levels/brightness — real pixel work; GPU helps.
    RenderPath preferredRenderPath() const { return RenderPath::GpuPreferred; }
    AdjustmentPanel* inner() const { return m_inner; }
private:
    AdjustmentPanel* m_inner = nullptr;
};

// ---------------------------------------------------------------------------
// Sub-dock 3: 属性 / 通道 / 路径
// ---------------------------------------------------------------------------

class VPropertyPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VPropertyPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("property"); }
    QString pageTitle() const { return QStringLiteral("Properties"); }
    // Properties: form-style, no heavy pixel work.
    RenderPath preferredRenderPath() const { return RenderPath::CpuOnly; }
    docks::PropertiesDock* inner() const { return m_inner; }
private:
    docks::PropertiesDock* m_inner = nullptr;
};

class VChannelPathPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VChannelPathPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("channelpath"); }
    QString pageTitle() const { return QStringLiteral("Channels / Paths"); }
    // Channel mask render + path tessellation — real pixel work, GPU wins.
    RenderPath preferredRenderPath() const { return RenderPath::GpuPreferred; }
    docks::ChannelPathPanel* inner() const { return m_inner; }
private:
    docks::ChannelPathPanel* m_inner = nullptr;
};

class VHistoryPage : public VTabPageBase {
    Q_OBJECT
public:
    explicit VHistoryPage(QWidget* parent = nullptr);
    QString pageTag()   const { return QStringLiteral("history"); }
    QString pageTitle() const { return QStringLiteral("History"); }
    // List view with no pixel work.
    RenderPath preferredRenderPath() const { return RenderPath::CpuOnly; }

    // Forward the QUndoStack binding through to the inner dock.
    void setUndoStack(class QUndoStack* stack);
    docks::HistoryDock* inner() const { return m_inner; }
private:
    docks::HistoryDock* m_inner = nullptr;
};

} // namespace vistella::ui
