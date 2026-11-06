// SPDX-License-Identifier: MIT
//
// RightPanelDock implementation.
//
// M2-2: the self written drag layer (VDraggableTabWidget +
// V*Page adapters) was replaced by native QTabWidget groups that hold the
// real panels directly. See RightPanelDock.h for the why.
#include "RightPanelDock.h"

#include "ColorTab.h"
#include "SwatchTab.h"
#include "GradientTab.h"
#include "PatternTab.h"
#include "PropertiesDock.h"
#include "LayersDock.h"
#include "ChannelPathPanel.h"
#include "HistoryDock.h"
#include "../imagewindow/AdjustmentPanel.h"
#include "../mediators/WorkspaceMediator.h"
#include "logger.h"

#include <QSplitter>
#include <QTabWidget>
#include <QUndoStack>
#include <QSize>
#include <QVBoxLayout>

namespace docks {

namespace {

// the smallest height a panel group may take. Below
//   this the tab bar is all that is left and the group is useless.
constexpr int kMinGroupHeight = 80;

// A tab widget whose height follows the CURRENT page.
//
// QTabWidget::sizeHint() returns the max over ALL pages. For the right
// panel that is wrong: the property group holds the PropertiesDock (a
// short label list) AND the AdjustmentPanel (a tall form of sliders and
// dropdowns). The aggregate hint is the tall one, so the property group
// would claim most of the panel even while the short page is showing.
//
// The old self written VDraggableTabWidget did this correctly -- its
// sizeHint() was "tab bar + current page". This class restores that
// behaviour on top of the native widget.
class CurrentPageTabWidget : public QTabWidget
{
public:
    using QTabWidget::QTabWidget;

    QSize sizeHint() const override
    {
        QSize s = tabBar()->sizeHint();
        if (QWidget* page = currentWidget())
            s = s.expandedTo(page->sizeHint());
        return s;
    }

    QSize minimumSizeHint() const override
    {
        QSize s = tabBar()->sizeHint();
        if (QWidget* page = currentWidget())
            s = s.expandedTo(page->minimumSizeHint());
        // The tab bar is the only way to reach the other pages of this
        // group, so it must never be squeezed out of existence.
        s.setHeight(qMax(s.height(), 24));
        s.setWidth(qMax(s.width(), 60));
        return s;
    }
};

// Build a group: a native tab widget whose pages are the real panels.
//
// The tab label is the panel's window title, so a panel keeps its own
// identity. The old adapter layer published the title with
// setWindowTitle() for exactly this reason -- with the adapters gone the
// panel itself has to carry it.
//
// `expanding` decides who absorbs the leftover height:
//   false -> the group sizes to its content (Maximum policy)
//   true  -> the group grows with the panel (Expanding policy)
//
// The policy matters as much as the stretch factor: a Maximum policy
// caps the widget at its sizeHint(), so a QSplitter can never hand it
// more space than the hint -- the stretch factor alone is not enough.
QTabWidget* makeGroup(QWidget* parent, const QString& objectName, bool expanding)
{
    auto* tabs = new CurrentPageTabWidget(parent);
    tabs->setObjectName(objectName);
    // The tabs follow the CURRENT page: the group is as tall as the page
    // it shows, not as tall as the tallest page. That is what the old
    // VDraggableTabWidget had to reimplement by hand.
    tabs->setSizePolicy(QSizePolicy::Preferred,
                        expanding ? QSizePolicy::Expanding : QSizePolicy::Maximum);
    tabs->setDocumentMode(true);
    return tabs;
}

} // namespace

RightPanelDock::RightPanelDock(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(4);
    outer->addWidget(m_splitter);

    buildColorGroup();
    buildPropertyGroup();
    buildLayerGroup();

    // the colour group and the property group size to
    //   their content; only the layer group absorbs the leftover height.
    //
    //   The old code asked for a fixed 1/3 split (200/200/200). Qt's
    //   QSplitter turns that into three equally tall groups - with no
    //   stretch factor set, every widget is "expansive" and the leftover
    //   space is shared out proportionally. The colour group therefore
    //   carried hundreds of pixels of empty space and the property text
    //   floated in the middle of a blank panel.
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 1);
    applyContentSizedSplit();

    LOG_INFO("[RightPanelDock] created: 3 native tab groups "
             "(color/property/layer)");
}

void RightPanelDock::applyContentSizedSplit()
{
    if (!m_splitter || !m_colorGroup || !m_propertyGroup || !m_layerGroup)
        return;

    // A group is never smaller than its header plus a usable strip of
    // content, otherwise the tab bar would be the only thing left.
    const int hColor    = qMax(kMinGroupHeight, m_colorGroup->sizeHint().height());
    const int hProperty = qMax(kMinGroupHeight, m_propertyGroup->sizeHint().height());

    // The layer group takes whatever is left: it is the one panel whose
    // content (the layer list) is meant to grow with the window.
    //
    // The layer group's own hint is small (the layer list is a table that
    // scrolls), so it must NOT be part of the request -- it is the one
    // that receives the remainder.
    m_splitter->setSizes({ hColor, hProperty, 0 });
}

void RightPanelDock::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // The first show is the first moment the dock knows its real height,
    // which is when the content-sized split becomes meaningful.
    if (!m_splitApplied) {
        m_splitApplied = true;
        applyContentSizedSplit();
    }
}

RightPanelDock::~RightPanelDock() = default;

void RightPanelDock::buildColorGroup()
{
    m_colorGroup = makeGroup(this, QStringLiteral("rightPanelColorGroup"), false);

    m_colorTab    = new ColorTab(m_colorGroup);
    m_swatchTab   = new SwatchTab(m_colorGroup);
    m_gradientTab = new GradientTab(m_colorGroup);
    m_patternTab  = new PatternTab(m_colorGroup);

    m_colorTab->setWindowTitle(tr("颜色"));
    m_swatchTab->setWindowTitle(tr("色板"));
    m_gradientTab->setWindowTitle(tr("渐变"));
    m_patternTab->setWindowTitle(tr("图案"));

    m_colorGroup->addTab(m_colorTab,    m_colorTab->windowTitle());
    m_colorGroup->addTab(m_swatchTab,   m_swatchTab->windowTitle());
    m_colorGroup->addTab(m_gradientTab, m_gradientTab->windowTitle());
    m_colorGroup->addTab(m_patternTab,  m_patternTab->windowTitle());

    m_splitter->addWidget(m_colorGroup);
}

void RightPanelDock::buildPropertyGroup()
{
    m_propertyGroup = makeGroup(this, QStringLiteral("rightPanelPropertyGroup"), false);

    m_propertiesDock  = new PropertiesDock(m_propertyGroup);
    m_adjustmentPanel = new AdjustmentPanel(m_propertyGroup);

    m_propertiesDock->setWindowTitle(tr("属性"));
    m_adjustmentPanel->setWindowTitle(tr("调整"));

    m_propertyGroup->addTab(m_propertiesDock,  m_propertiesDock->windowTitle());
    m_propertyGroup->addTab(m_adjustmentPanel, m_adjustmentPanel->windowTitle());

    m_splitter->addWidget(m_propertyGroup);
}

void RightPanelDock::buildLayerGroup()
{
    m_layerGroup = makeGroup(this, QStringLiteral("rightPanelLayerGroup"), true);

    m_layerDock        = new LayersDock(m_layerGroup);
    m_channelPathPanel = new ChannelPathPanel(m_layerGroup);
    m_historyDock      = new HistoryDock(m_layerGroup);

    m_layerDock->setWindowTitle(tr("图层"));
    m_channelPathPanel->setWindowTitle(tr("通道/路径"));
    m_historyDock->setWindowTitle(tr("历史"));

    // the panel group provides the page switcher, so
    //   the inner tab bar of the LayersDock would be a duplicate row of
    //   tabs (and its channel/path page was only ever a placeholder).
    m_layerDock->setEmbeddedInPanelGroup(true);

    m_layerGroup->addTab(m_layerDock,        m_layerDock->windowTitle());
    m_layerGroup->addTab(m_channelPathPanel, m_channelPathPanel->windowTitle());
    m_layerGroup->addTab(m_historyDock,      m_historyDock->windowTitle());

    m_splitter->addWidget(m_layerGroup);
}

void RightPanelDock::attach(mediators::WorkspaceMediator* wsMed)
{
    m_wsMed = wsMed;
    if (m_wsMed) {
        connect(m_wsMed, &mediators::WorkspaceMediator::workspaceChanged,
                this, &RightPanelDock::onWorkspaceChanged);
        connect(m_wsMed, &mediators::WorkspaceMediator::dockVisibilityChanged,
                this, &RightPanelDock::onDockVisibilityChanged);
        onWorkspaceChanged(m_wsMed->currentWorkspace());
    }
    LOG_DEBUG("[RightPanelDock] attach wsMed={}", wsMed ? "yes" : "null");
}

void RightPanelDock::onWorkspaceChanged(mediators::WorkspaceId id)
{
    if (!m_wsMed) return;
    LOG_INFO("[RightPanelDock] workspaceChanged: id={}", static_cast<int>(id));
    // workspace visibility now controls the colour group and the property
    // group. The layer group is always visible (core editing area).
    bool visColor   = m_wsMed->isDockVisible(0);
    bool visProperty = m_wsMed->isDockVisible(1);
    if (m_colorGroup)   m_colorGroup->setVisible(visColor);
    if (m_propertyGroup) m_propertyGroup->setVisible(visProperty);
    Q_UNUSED(id);
}

void RightPanelDock::onDockVisibilityChanged(int dockIndex, bool visible)
{
    if (!m_wsMed) return;
    if (dockIndex == 0 && m_colorGroup)    m_colorGroup->setVisible(visible);
    if (dockIndex == 1 && m_propertyGroup) m_propertyGroup->setVisible(visible);
}

void RightPanelDock::setUndoStack(QUndoStack* stack)
{
    if (m_historyDock) m_historyDock->setUndoStack(stack);
}

void RightPanelDock::setPropertyImageInfo(const PropertiesDock::ImageInfo& info)
{
    if (!m_propertiesDock) return;
    m_propertiesDock->setImageInfo(info);
}

void RightPanelDock::setLayerPanel(QWidget* panel)
{
    // The LayersDock hosts the layer list as its first tab content.
    if (!m_layerDock) return;
    m_layerDock->setContentWidget(0, panel);
}

void RightPanelDock::setChannelPathQuickMask(bool on)
{
    if (!m_channelPathPanel) return;
    m_channelPathPanel->setQuickMask(on);
}

void RightPanelDock::invalidateAllPageCaches()
{
    // The properties panel has no internal cache to drop (its data is
    // pushed in from outside via setImageInfo / setSelectionBbox / etc),
    // so invalidating it is a repaint trigger.
    if (m_propertiesDock) {
        ++m_propertyInvalidateCount;
        m_propertiesDock->update();
    }
    // The channel panel re-reads the active channel on the next paint.
    if (m_channelPathPanel) {
        ++m_channelPathInvalidateCount;
        m_channelPathPanel->update();
    }
    // The history panel listens to QUndoStack::indexChanged via its own
    // setUndoStack(); the only path that bypasses it is direct mutation
    // of the stack, which we do not do.
    if (m_historyDock) {
        ++m_historyInvalidateCount;
    }
}

QSize RightPanelDock::sizeHint() const
{
    return QSize(340, 800);
}

QSize RightPanelDock::minimumSizeHint() const
{
    // 右侧面板是 dock, 它不能决定主窗口的高度
    //
    //   Qt 会把 dock 的 minimumSizeHint 累加进 QMainWindow 的布局最小值:
    //   三个组的最小高度加起来 (颜色 ~110 + 属性 ~390 + 图层 ~240) 超过
    //   窗口高度时, 窗口就被强行撑大 —— 打开曲线调整页时窗口长到 1186px,
    //   直接超出 1080 的屏幕.
    //
    //   正确做法是 dock 给一个"有界下限": 窗口给多少高度, 三个组就怎么分,
    //   需要更多空间的用户自己拖分隔条 (splitter 的拖动不受这个下限限制).
    return QSize(280, 240);
}

} // namespace docks
