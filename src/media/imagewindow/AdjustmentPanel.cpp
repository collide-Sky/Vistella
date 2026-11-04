// SPDX-License-Identifier: MIT
//
// AdjustmentPanel - 完整实装
//
// 5 tab UI 完整接 P0-2 异步 + 5 tab 切换不丢参数 + 接 ImageEditCommand 撤销栈
// P0-3.3 才升级为 LayerCommand
//
#include "AdjustmentPanel.h"
#include "logger.h"

#include "imageprocessor.h"
#include "../imageworker/layers/LayerCommand.h"   // LayerCommand::SetAdjustmentLut
#include "../imageworker/layers/LayerStack.h"     // P0-3.3: LayerStack* + selection()

#include <QtWidgets>  // one include for all the QtWidgets classes (QSlider/QLabel/QPushButton/QGridLayout/etc)

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QPointF>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainterPath>
#include <QMetaObject>
#include <QPointer>
#include <QEvent>
#include <QPaintEvent>

#include <opencv2/imgproc.hpp>

#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <cstring>

// =============================================================
// CurveEditor - 自定义 QWidget, 显示 + 编辑 8 控制点曲线
//   控件空间: 左上 (0, 0), 右下 (w, h)
//   control 空间: 左下 (0, 0), 右上 (255, 255) — Y 轴向上
// =============================================================

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent), m_points(defaultPoints())
{
    setMinimumSize(256, 256);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QPolygonF CurveEditor::defaultPoints()
{
    // 8 个控制点: (0,0) + 6 个均匀分布 + (255, 255) — 全部 y == x 表示恒等曲线
    QPolygonF p;
    p << QPointF(0,   0)
      << QPointF(42,  42)
      << QPointF(85,  85)
      << QPointF(128, 128)
      << QPointF(170, 170)
      << QPointF(213, 213)
      << QPointF(240, 240)
      << QPointF(255, 255);
    return p;
}

void CurveEditor::setControlPoints(const QPolygonF& p)
{
    m_points = p;
    update();
}

QPointF CurveEditor::ctrlToWidget(const QPointF& p) const
{
    // control (0..255) -> widget (左上是 0)
    const double w = std::max(1, width());
    const double h = std::max(1, height());
    const double x = (p.x() / 255.0) * w;
    const double y = (1.0 - p.y() / 255.0) * h;  // Y 反转
    return QPointF(x, y);
}

QPointF CurveEditor::widgetToCtrl(const QPointF& p) const
{
    const double w = std::max(1, width());
    const double h = std::max(1, height());
    const double x = (p.x() / w) * 255.0;
    const double y = (1.0 - p.y() / h) * 255.0;  // Y 反转
    return QPointF(std::clamp(x, 0.0, 255.0), std::clamp(y, 0.0, 255.0));
}

void CurveEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();

    // 背景
    p.fillRect(0, 0, w, h, QColor(245, 245, 245));

    // 网格 (8x8)
    p.setPen(QPen(QColor(220, 220, 220), 1));
    for (int i = 1; i < 8; ++i) {
        const int x = i * w / 8;
        const int y = i * h / 8;
        p.drawLine(x, 0, x, h);
        p.drawLine(0, y, w, y);
    }

    // 边框
    p.setPen(QPen(QColor(180, 180, 180), 1));
    p.drawRect(0, 0, w - 1, h - 1);

    // 对角线 (恒等曲线参考)
    p.setPen(QPen(QColor(200, 200, 200), 1, Qt::DashLine));
    p.drawLine(0, h, w, 0);

    // 曲线 (spline-like 折线, 简单 polyline)
    if (m_points.size() >= 2) {
        QPolygonF pts;
        pts.reserve(m_points.size());
        for (const QPointF& cp : m_points) {
            pts << ctrlToWidget(cp);
        }
        // 平滑曲线: 用 QPainterPath catmull-rom-ish (简单版: 折线 + 2 段平滑)
        QPainterPath path;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i) {
            path.lineTo(pts[i]);
        }
        p.setPen(QPen(QColor(70, 130, 180), 2));
        p.drawPath(path);
    }

    // 控制点
    p.setBrush(QColor(220, 60, 60));
    p.setPen(QPen(QColor(120, 30, 30), 1));
    for (const QPointF& cp : m_points) {
        const QPointF wp = ctrlToWidget(cp);
        p.drawEllipse(wp, kPointRadius, kPointRadius);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const QPointF click = e->pos();
    // 找最近控制点
    int bestIdx = -1;
    double bestDist = kPointRadius * 2 + 4;
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF wp = ctrlToWidget(m_points[i]);
        const double d = std::hypot(wp.x() - click.x(), wp.y() - click.y());
        if (d < bestDist) {
            bestDist = d;
            bestIdx = i;
        }
    }
    if (bestIdx < 0) {
        // 内部点击空白 -> 不动
        return;
    }
    m_dragIndex = bestIdx;
    // 立即更新 (drag 起始点)
    QPointF newCtrl = widgetToCtrl(click);
    m_points[bestIdx] = newCtrl;
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragIndex < 0) return;
    if (m_dragIndex < 0 || m_dragIndex >= m_points.size()) return;
    QPointF newCtrl = widgetToCtrl(e->pos());
    // 端点 (index 0 或 last) 锁 X (PS 也是: 端点不水平拖)
    if (m_dragIndex == 0) {
        newCtrl.setX(0);
    } else if (m_dragIndex == m_points.size() - 1) {
        newCtrl.setX(255);
    }
    m_points[m_dragIndex] = newCtrl;
    update();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* e)
{
    Q_UNUSED(e);
    if (m_dragIndex < 0) return;
    m_dragIndex = -1;
    emit pointsChanged(m_points);
}

void CurveEditor::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    update();
}

// =============================================================
// AdjustmentPanel - 5 tab 容器
// =============================================================

namespace {
// 8 个色相区段标签 (PS HSL 顺序: Master + 7 个色相段, 留 1 备用)
const char* kHslHueNames[8] = {
    "Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Reserved"
};
const char* kHslSatNames[8] = {
    "Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Reserved"
};
const char* kBwColorNames[6] = {
    "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"
};

// Two adjustment layers carry the same adjustment when their LUT, their
// per-pixel payload and their per-channel LUTs all match.
//
// Used in two places:
//   - the debounce commit, to tell whether the user really changed
//     anything (a drag that ends on the old value must not push an
//     undo command),
//   - the preview session, to tell whether the dialog changed anything
//     at all.
bool sameAdjustmentState(const layers::Layer& a, const layers::Layer& b)
{
    if (a.adjustmentParams != b.adjustmentParams) return false;

    const cv::Mat& la = a.adjustmentLut;
    const cv::Mat& lb = b.adjustmentLut;
    if (la.empty() != lb.empty()) return false;
    if (!la.empty()) {
        if (la.size() != lb.size() || la.type() != lb.type()) return false;
        const size_t bytes = static_cast<size_t>(la.total())
                           * static_cast<size_t>(la.elemSize());
        if (std::memcmp(la.data, lb.data, bytes) != 0) return false;
    }

    if (a.adjustmentChannelLuts.size() != b.adjustmentChannelLuts.size()) {
        return false;
    }
    for (int i = 0; i < a.adjustmentChannelLuts.size(); ++i) {
        const cv::Mat& x = a.adjustmentChannelLuts.at(i);
        const cv::Mat& y = b.adjustmentChannelLuts.at(i);
        if (x.empty() != y.empty()) return false;
        if (!x.empty()) {
            if (x.size() != y.size() || x.type() != y.type()) return false;
            const size_t bytes = static_cast<size_t>(x.total())
                               * static_cast<size_t>(x.elemSize());
            if (std::memcmp(x.data, y.data, bytes) != 0) return false;
        }
    }
    return true;
}
}  // namespace

AdjustmentPanel::AdjustmentPanel(QWidget* parent) : QWidget(parent)
{
    //  每个 tab 的调整图层还没建过
    for (int i = 0; i < kTabCount; ++i) m_adjLayerIndex[i] = -1;
    // 初始化 HSL 8 hue + 8 sat 默认值 0
    m_hsl.hueShifts.resize(8, 0);
    m_hsl.satShifts.resize(8, 0);
    m_hsl.lightness = 0;
    // 初始化 B&W 6 默认 100 (中性)
    m_bw.rgbMixer.resize(6, 100.0);
    // Curves 默认 (8 control points 直线)
    m_curves.controlPoints = CurveEditor::defaultPoints();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(2, 2, 2, 2);
    rootLayout->setSpacing(2);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::North);
    rootLayout->addWidget(m_tabWidget);

    // 5 个 page
    buildCurvesPage();
    buildLevelsPage();
    buildHslPage();
    buildBlackWhitePage();
    buildChannelMixerPage();

    // tab 切换
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        // 切 tab 立即 commit pending undo
        //   用户在 tab A 调了一堆后切到 tab B, debounce 200ms 可能还没到
        //   立即 push 之前 tab A 的 undo, 避免 tab B 的 adjust 把"before"覆盖
        //   设计原则: 切 tab = "完成当前 tab 调整", 强制 commit
        if (m_commitTimer && m_commitTimer->isActive()) {
            m_commitTimer->stop();
            commitUndoDebounced();
        }
        // 切 tab 之前先缓存当前 tab 的 UI 值
        // (5 tab 内部 cache + UI 控件是 source of truth, 切回去时 restoreCurrentTabToUi 还原)
        switchToTab(idx);
    });

    // debounce commit timer
    //   200ms 静默期到就 push 一条 LayerCommand, 避免拖动时撤销栈爆炸
    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(200);
    connect(m_commitTimer, &QTimer::timeout, this, &AdjustmentPanel::commitUndoDebounced);

    // 初始 tab: Curves
    m_tabWidget->setCurrentIndex(kTabCurves);
    switchToTab(kTabCurves);
}

AdjustmentPanel::~AdjustmentPanel() = default;

QString AdjustmentPanel::tabName(int tabIndex) const
{
    switch (tabIndex) {
        case kTabCurves:       return QStringLiteral("Curves");
        case kTabLevels:       return QStringLiteral("Levels");
        case kTabHsl:          return QStringLiteral("HSL");
        case kTabBlackWhite:   return QStringLiteral("B&W");
        case kTabChannelMixer: return QStringLiteral("ChannelMixer");
        default:               return QStringLiteral("Adjustment");
    }
}

void AdjustmentPanel::switchToTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= kTabCount) return;
    if (m_currentTab == tabIndex) return;

    // 缓存当前 tab UI 值到内部 struct
    // (5 tab 内部 cache + UI 控件是 source of truth, 切回去时 restoreCurrentTabToUi 还原)
    // 这里不需要做 - 因为 UI 控件在切换时会保留 (QTabWidget 子 widget 不销毁)

    m_currentTab = tabIndex;
    // 切回新 tab 时 UI 已经显示, 控件值跟之前一致
    // 5 tab 切换不重置其他 tab 缓存
    emit tabChanged(tabIndex);
}

// =============================================================
// buildCurvesPage
// =============================================================
void AdjustmentPanel::buildCurvesPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* info = new QLabel(tr("拖动控制点调整曲线 (8 个) — 实时 buildCurvesLUT"));
    info->setWordWrap(true);
    info->setStyleSheet("color: #666; font-size: 9pt;");
    layout->addWidget(info);

    m_curvesEditor = new CurveEditor(page);
    m_curvesEditor->setControlPoints(m_curves.controlPoints);
    layout->addWidget(m_curvesEditor, /*stretch*/1);

    m_curvesResetBtn = new QPushButton(tr("重置曲线"), page);
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_curvesResetBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_curvesEditor, &CurveEditor::pointsChanged, this, [this](const QPolygonF& p) {
        m_curves.controlPoints = p;
        emit paramChanged();
        applyCurrentTab();
    });
    connect(m_curvesResetBtn, &QPushButton::clicked, this, [this]() {
        m_curves.controlPoints = CurveEditor::defaultPoints();
        m_curvesEditor->setControlPoints(m_curves.controlPoints);
        emit paramChanged();
        applyCurrentTab();
    });

    m_tabWidget->addTab(page, tr("曲线"));
}

// =============================================================
// buildLevelsPage
// =============================================================
void AdjustmentPanel::buildLevelsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* info = new QLabel(tr("色阶: 输入范围 + Gamma + 输出范围"));
    info->setStyleSheet("color: #666; font-size: 9pt;");
    layout->addWidget(info);

    auto* inGroup = new QGroupBox(tr("输入"));
    auto* inLayout = new QGridLayout(inGroup);
    inLayout->setContentsMargins(4, 4, 4, 4);

    inLayout->addWidget(new QLabel(tr("暗部 (inLow)")), 0, 0);
    m_levelsInLow = new QSlider(Qt::Horizontal);
    m_levelsInLow->setRange(0, 254);  // 不能 = inHigh
    m_levelsInLowVal = new QLabel("0");
    inLayout->addWidget(m_levelsInLow, 0, 1);
    inLayout->addWidget(m_levelsInLowVal, 0, 2);

    inLayout->addWidget(new QLabel(tr("亮部 (inHigh)")), 1, 0);
    m_levelsInHigh = new QSlider(Qt::Horizontal);
    m_levelsInHigh->setRange(1, 255);
    m_levelsInHighVal = new QLabel("255");
    inLayout->addWidget(m_levelsInHigh, 1, 1);
    inLayout->addWidget(m_levelsInHighVal, 1, 2);

    inLayout->addWidget(new QLabel(tr("Gamma")), 2, 0);
    m_levelsGamma = new QSlider(Qt::Horizontal);
    m_levelsGamma->setRange(10, 300);  // 0.1..3.0, 实际用 v/100
    m_levelsGamma->setValue(100);
    m_levelsGammaVal = new QLabel("1.00");
    inLayout->addWidget(m_levelsGamma, 2, 1);
    inLayout->addWidget(m_levelsGammaVal, 2, 2);

    layout->addWidget(inGroup);

    auto* outGroup = new QGroupBox(tr("输出"));
    auto* outLayout = new QGridLayout(outGroup);
    outLayout->setContentsMargins(4, 4, 4, 4);

    outLayout->addWidget(new QLabel(tr("暗部 (outLow)")), 0, 0);
    m_levelsOutLow = new QSlider(Qt::Horizontal);
    m_levelsOutLow->setRange(0, 255);
    m_levelsOutLowVal = new QLabel("0");
    outLayout->addWidget(m_levelsOutLow, 0, 1);
    outLayout->addWidget(m_levelsOutLowVal, 0, 2);

    outLayout->addWidget(new QLabel(tr("亮部 (outHigh)")), 1, 0);
    m_levelsOutHigh = new QSlider(Qt::Horizontal);
    m_levelsOutHigh->setRange(0, 255);
    m_levelsOutHighVal = new QLabel("255");
    outLayout->addWidget(m_levelsOutHigh, 1, 1);
    outLayout->addWidget(m_levelsOutHighVal, 1, 2);

    layout->addWidget(outGroup);

    // 信号 - 任何 slider 变化都触发
    auto onInLow = [this](int v) {
        // 防止 inLow >= inHigh
        if (v >= m_levelsInHigh->value()) {
            v = m_levelsInHigh->value() - 1;
            m_levelsInLow->blockSignals(true);
            m_levelsInLow->setValue(v);
            m_levelsInLow->blockSignals(false);
        }
        m_levelsInLowVal->setText(QString::number(v));
        emit paramChanged();
        applyCurrentTab();
    };
    auto onInHigh = [this](int v) {
        if (v <= m_levelsInLow->value()) {
            v = m_levelsInLow->value() + 1;
            m_levelsInHigh->blockSignals(true);
            m_levelsInHigh->setValue(v);
            m_levelsInHigh->blockSignals(false);
        }
        m_levelsInHighVal->setText(QString::number(v));
        emit paramChanged();
        applyCurrentTab();
    };
    auto onGamma = [this](int v) {
        m_levelsGammaVal->setText(QString::number(v / 100.0, 'f', 2));
        emit paramChanged();
        applyCurrentTab();
    };
    auto onOutLow = [this](int v) {
        m_levelsOutLowVal->setText(QString::number(v));
        emit paramChanged();
        applyCurrentTab();
    };
    auto onOutHigh = [this](int v) {
        m_levelsOutHighVal->setText(QString::number(v));
        emit paramChanged();
        applyCurrentTab();
    };
    connect(m_levelsInLow,  &QSlider::valueChanged, this, onInLow);
    connect(m_levelsInHigh, &QSlider::valueChanged, this, onInHigh);
    connect(m_levelsGamma,  &QSlider::valueChanged, this, onGamma);
    connect(m_levelsOutLow, &QSlider::valueChanged, this, onOutLow);
    connect(m_levelsOutHigh,&QSlider::valueChanged, this, onOutHigh);

    layout->addStretch();

    m_tabWidget->addTab(page, tr("色阶"));
}

// =============================================================
// buildHslPage
// =============================================================
void AdjustmentPanel::buildHslPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* info = new QLabel(tr("HSL: 8 色相 hue + 8 饱和度 sat + 1 明度 light"));
    info->setStyleSheet("color: #666; font-size: 9pt;");
    layout->addWidget(info);

    // 8 hue sliders
    auto* hueGroup = new QGroupBox(tr("色相 (Hue, -180..180)"));
    auto* hueLayout = new QGridLayout(hueGroup);
    hueLayout->setContentsMargins(4, 4, 4, 4);
    m_hslHueSliders.reserve(8);
    for (int i = 0; i < 8; ++i) {
        hueLayout->addWidget(new QLabel(tr(kHslHueNames[i])), i, 0);
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(-180, 180);
        s->setValue(0);
        m_hslHueSliders.push_back(s);
        hueLayout->addWidget(s, i, 1);
        connect(s, &QSlider::valueChanged, this, [this, i](int v) {
            m_hsl.hueShifts[i] = v;
            emit paramChanged();
            applyCurrentTab();
        });
    }
    layout->addWidget(hueGroup);

    // 8 sat sliders
    auto* satGroup = new QGroupBox(tr("饱和度 (Sat, -100..100)"));
    auto* satLayout = new QGridLayout(satGroup);
    satLayout->setContentsMargins(4, 4, 4, 4);
    m_hslSatSliders.reserve(8);
    for (int i = 0; i < 8; ++i) {
        satLayout->addWidget(new QLabel(tr(kHslSatNames[i])), i, 0);
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(-100, 100);
        s->setValue(0);
        m_hslSatSliders.push_back(s);
        satLayout->addWidget(s, i, 1);
        connect(s, &QSlider::valueChanged, this, [this, i](int v) {
            m_hsl.satShifts[i] = v;
            emit paramChanged();
            applyCurrentTab();
        });
    }
    layout->addWidget(satGroup);

    // 1 lightness slider
    auto* lightGroup = new QGroupBox(tr("明度 (Light, -100..100)"));
    auto* lightLayout = new QHBoxLayout(lightGroup);
    lightLayout->addWidget(new QLabel(tr("Lightness")));
    m_hslLightness = new QSlider(Qt::Horizontal);
    m_hslLightness->setRange(-100, 100);
    m_hslLightness->setValue(0);
    lightLayout->addWidget(m_hslLightness);
    connect(m_hslLightness, &QSlider::valueChanged, this, [this](int v) {
        m_hsl.lightness = v;
        emit paramChanged();
        applyCurrentTab();
    });
    layout->addWidget(lightGroup);

    layout->addStretch();

    m_tabWidget->addTab(page, tr("HSL"));
}

// =============================================================
// buildBlackWhitePage
// =============================================================
void AdjustmentPanel::buildBlackWhitePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* info = new QLabel(tr("黑白 (B&W): 6 颜色混色 0..200 (100 = 中性)"));
    info->setStyleSheet("color: #666; font-size: 9pt;");
    layout->addWidget(info);

    auto* group = new QGroupBox(tr("颜色 mixer"));
    auto* grid = new QGridLayout(group);
    grid->setContentsMargins(4, 4, 4, 4);

    m_bwSliders.reserve(6);
    for (int i = 0; i < 6; ++i) {
        grid->addWidget(new QLabel(tr(kBwColorNames[i])), i, 0);
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(0, 200);
        s->setValue(100);
        m_bwSliders.push_back(s);
        grid->addWidget(s, i, 1);
        connect(s, &QSlider::valueChanged, this, [this, i](int v) {
            m_bw.rgbMixer[i] = v;
            emit paramChanged();
            applyCurrentTab();
        });
    }
    layout->addWidget(group);

    layout->addStretch();

    m_tabWidget->addTab(page, tr("黑白"));
}

// =============================================================
// buildChannelMixerPage
// =============================================================
void AdjustmentPanel::buildChannelMixerPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* info = new QLabel(tr("通道混合器: RGB -> RGB 3x3 矩阵 (0..200, 200 = 100% 保留)"));
    info->setStyleSheet("color: #666; font-size: 9pt;");
    layout->addWidget(info);

    auto* group = new QGroupBox(tr("输出通道 = (R, G, B) · 3x3 矩阵"));
    auto* grid = new QGridLayout(group);
    grid->setContentsMargins(4, 4, 4, 4);

    // 表头: 列 = 输入, 行 = 输出
    grid->addWidget(new QLabel(""),        0, 0);
    grid->addWidget(new QLabel(tr("R 输入")), 0, 1);
    grid->addWidget(new QLabel(tr("G 输入")), 0, 2);
    grid->addWidget(new QLabel(tr("B 输入")), 0, 3);

    // 3 行: 输出 R/G/B
    // 默认: r=(200,0,0), g=(0,200,0), b=(0,0,200)
    int defaultMatrix[3][3] = {
        { 200, 0,   0   },
        { 0,   200, 0   },
        { 0,   0,   200 },
    };
    m_cmSliders.reserve(9);
    for (int row = 0; row < 3; ++row) {
        const char* label = (row == 0) ? "R 输出" : (row == 1) ? "G 输出" : "B 输出";
        grid->addWidget(new QLabel(tr(label)), row + 1, 0);
        for (int col = 0; col < 3; ++col) {
            auto* s = new QSlider(Qt::Horizontal);
            s->setRange(0, 200);
            s->setValue(defaultMatrix[row][col]);
            m_cmSliders.push_back(s);
            grid->addWidget(s, row + 1, col + 1);
            connect(s, &QSlider::valueChanged, this, [this, row, col](int v) {
                // 写回 3 个字段 (3x3 flatten: row*3 + col)
                switch (row) {
                    case 0:
                        if (col == 0) m_cm.rR = v;
                        else if (col == 1) m_cm.rG = v;
                        else m_cm.rB = v;
                        break;
                    case 1:
                        if (col == 0) m_cm.gR = v;
                        else if (col == 1) m_cm.gG = v;
                        else m_cm.gB = v;
                        break;
                    case 2:
                        if (col == 0) m_cm.bR = v;
                        else if (col == 1) m_cm.bG = v;
                        else m_cm.bB = v;
                        break;
                }
                emit paramChanged();
                applyCurrentTab();
            });
        }
    }
    layout->addWidget(group);

    layout->addStretch();

    m_tabWidget->addTab(page, tr("通道"));
}

// =============================================================
// buildCurrentTabLut - 根据当前 tab 调用对应 build*LUT 函数
// =============================================================
cv::Mat AdjustmentPanel::buildCurrentTabLut() const
{
    switch (m_currentTab) {
        case kTabCurves: {
            return ImageProcessor::buildCurvesLUT(m_curves.controlPoints);
        }
        case kTabLevels: {
            return ImageProcessor::buildLevelsLUT(
                m_levels.inLow, m_levels.inHigh, m_levels.gamma,
                m_levels.outLow, m_levels.outHigh);
        }
        case kTabHsl: {
            // 把 8 hue + 8 sat 合成 QVector<QPair<int,int>>
            QVector<QPair<int, int>> pairs;
            pairs.reserve(8);
            for (int i = 0; i < 8; ++i) {
                const int hue = (i < m_hsl.hueShifts.size()) ? m_hsl.hueShifts[i] : 0;
                const int sat = (i < m_hsl.satShifts.size()) ? m_hsl.satShifts[i] : 0;
                pairs << qMakePair(hue, sat);
            }
            return ImageProcessor::buildHueSatLUT(pairs, m_hsl.lightness);
        }
        case kTabBlackWhite: {
            return ImageProcessor::buildBlackWhiteLUT(m_bw.rgbMixer);
        }
        case kTabChannelMixer: {
            // The 1D LUT is only a fallback: the tab carries the real
            // 3x3 matrix as a per-pixel payload (see buildTabParams),
            // and the render path prefers the payload.
            const int avgR = (m_cm.rR + m_cm.rG + m_cm.rB) / 3;
            // avgR - 100 = 偏移, 200 中性 → 0 偏移
            return ImageProcessor::buildColorBalanceLUT(avgR - 100, 0, 0);
        }
    }
    return cv::Mat();
}

// M2-6b: the per-pixel payload of a tab.
//
// HSL / black & white / channel mixer are not per-channel functions:
//   - HSL shifts the hue and the saturation PER COLOUR BAND, so the
//     result of a pixel depends on the hue of THAT pixel,
//   - black & white turns every pixel grey with six colour-weighted
//     mixers,
//   - the channel mixer is a 3x3 matrix over R / G / B.
// None of them fits into a 256 entry table, so they travel as an
// AdjustmentParams and the render path applies them per pixel.
//
// Curves and levels keep the 1D LUT path (kind == Lut).
layers::AdjustmentParams AdjustmentPanel::buildTabParams(int tab) const
{
    layers::AdjustmentParams p;
    switch (tab) {
        case kTabHsl: {
            p.kind = layers::AdjustKind::Hsl;
            p.hueShifts = m_hsl.hueShifts;
            p.satShifts  = m_hsl.satShifts;
            p.lightness  = m_hsl.lightness;
            break;
        }
        case kTabBlackWhite: {
            p.kind = layers::AdjustKind::BlackWhite;
            p.bwMixers = m_bw.rgbMixer;
            break;
        }
        case kTabChannelMixer: {
            p.kind = layers::AdjustKind::ChannelMixer;
            // The rows are the output channels (red, green, blue) and
            // the columns are the input channels (red, green, blue).
            p.cmMatrix[0] = m_cm.rR; p.cmMatrix[1] = m_cm.rG; p.cmMatrix[2] = m_cm.rB;
            p.cmMatrix[3] = m_cm.gR; p.cmMatrix[4] = m_cm.gG; p.cmMatrix[5] = m_cm.gB;
            p.cmMatrix[6] = m_cm.bR; p.cmMatrix[7] = m_cm.bG; p.cmMatrix[8] = m_cm.bB;
            p.cmMonochrome = m_cm.monochrome;
            break;
        }
        default:
            break;   // curves / levels: the 1D LUT path
    }
    return p;
}

// M2-6b: the per-channel LUTs of the curves tab.
//
// The Curves dialog has a channel selector, so the tab keeps one curve
// per channel. A channel that was never edited follows the master
// curve.
//
// The three LUTs are returned in the OpenCV BGR order (blue, green,
// red), which is the order applyChannelLuts() expects.
QVector<cv::Mat> AdjustmentPanel::buildTabChannelLuts(int tab) const
{
    QVector<cv::Mat> luts;
    if (tab != kTabCurves) return luts;

    const QPolygonF& red   = m_curves.redPoints;
    const QPolygonF& green = m_curves.greenPoints;
    const QPolygonF& blue  = m_curves.bluePoints;
    if (red.isEmpty() && green.isEmpty() && blue.isEmpty()) {
        return luts;   // master curve only
    }

    const QPolygonF& master = m_curves.controlPoints;
    luts.push_back(ImageProcessor::buildCurvesLUT(
        blue.isEmpty() ? master : blue));      // blue
    luts.push_back(ImageProcessor::buildCurvesLUT(
        green.isEmpty() ? master : green));    // green
    luts.push_back(ImageProcessor::buildCurvesLUT(
        red.isEmpty() ? master : red));        // red
    return luts;
}

// =============================================================
// applyCurrentTab - 调当前 tab 的 LUT, 走 background pool async
//   跟 P0-2 ImageAdjustmentPanel::asyncApplyCurrentParams 同样的取消语义:
//     - 每次 apply 分配新 task id, 旧 in-flight task 完成后主线程发现 id 变了, 直接 return
//     - 完成后切主线程: setCurrentImage (不再立即 push undo, 改由 debounce commit 处理)
// undo 集成 LayerCommand::SetAdjustmentLut
//   - 拖动过程中多次 applyCurrentTab 只 push 一条 undo (debounce 200ms)
//   - debounce timeout 后 commitUndoDebounced() push 一条 LayerCommand
//   - m_pendingBefore: debounce 窗口起始时的"before" image (首次 valueChanged 时 snapshot)
// =============================================================
void AdjustmentPanel::applyCurrentTab()
{
    if (!m_host) return;
    const int tab = m_currentTab;
    cv::Mat lut = buildCurrentTabLut();
    const layers::AdjustmentParams params = buildTabParams(tab);
    const QVector<cv::Mat> channelLuts = buildTabChannelLuts(tab);

    auto* stack = m_host->layerStack();
    if (!stack) return;
    if (lut.empty() && params.kind == layers::AdjustKind::Lut
        && channelLuts.isEmpty()) {
        return;
    }

    int layerIdx = m_adjLayerIndex[tab];
    bool needCreate = false;
    if (layerIdx < 0 || layerIdx >= stack->count()
        || !stack->at(layerIdx)
        || stack->at(layerIdx)->kind != layers::Layer::Adjustment) {
        needCreate = true;
    }

    if (needCreate) {
        //  第一次用这个 tab: 新建一个调整图层放在栈顶.
        //  它套用的是它下面的所有图层, 底图像素不动.
        layerIdx = stack->addAdjustmentLayer(tabName(tab),
                                            tabName(tab).toLower(), lut);
        if (layerIdx < 0) return;
        m_adjLayerIndex[tab] = layerIdx;
        m_pendingLut[tab].release();
        m_pendingValid[tab] = false;
        m_pendingParams[tab] = layers::AdjustmentParams();
        m_pendingChannelLuts[tab].clear();
    } else {
        auto l = stack->at(layerIdx);
        //  the start of the debounce window: remember the whole adjustment state from before the change, so the undo can restore it
        if (!m_pendingValid[tab]) {
            m_pendingLut[tab] = l->adjustmentLut.empty() ? cv::Mat()
                                                          : l->adjustmentLut.clone();
            m_pendingParams[tab] = l->adjustmentParams;
            m_pendingChannelLuts[tab] = l->adjustmentChannelLuts;
            m_pendingValid[tab] = true;
        }
        stack->setAdjustmentLut(layerIdx, lut);
    }

    //  M2-6b: the per-pixel payload and the per-channel LUTs.
    //  The render path prefers the payload over the LUT, so a tab that
    //  carries one does not need its 1D LUT at all.
    stack->setAdjustmentParams(layerIdx, params);
    stack->setAdjustmentChannelLuts(layerIdx, channelLuts);

    //  重绘画布 (图层内容变了 -> 合成缓存失效 -> 重新渲染)
    m_host->refreshCanvasNow();

    //  重启 debounce timer (200ms 静默后提交一条撤销命令)
    if (m_commitTimer) m_commitTimer->start();
}

// debounce commit - timer 触发时 push 一条 LayerCommand::SetAdjustmentLut
//   m_pendingBefore = debounce 窗口起始时的 image
//   m_current = 当前 image (debounce 结束时的 image, 即调值后结果)
//   如果两者相同 (用户没动), 不 push
//   注: 5 tab 切换不丢参数已由 P0-3.2 实现 (m_curves/m_levels/... 跨 tab 保留), 这里不重做
void AdjustmentPanel::commitUndoDebounced()
{
    if (!m_host || !m_host->undoStack()) return;
    auto* stack = m_host->layerStack();
    if (!stack) return;
    for (int tab = 0; tab < kTabCount; ++tab) {
        if (!m_pendingValid[tab]) continue;
        const int layerIdx = m_adjLayerIndex[tab];
        if (layerIdx < 0 || layerIdx >= stack->count()) {
            m_pendingLut[tab].release();
            m_pendingValid[tab] = false;
            m_pendingParams[tab] = layers::AdjustmentParams();
            m_pendingChannelLuts[tab].clear();
            continue;
        }
        auto l = stack->at(layerIdx);
        if (!l) {
            m_pendingLut[tab].release();
            m_pendingValid[tab] = false;
            m_pendingParams[tab] = layers::AdjustmentParams();
            m_pendingChannelLuts[tab].clear();
            continue;
        }
        //  nothing changed -> no undo command (the user did not really change anything)
        layers::Layer pending;
        pending.adjustmentLut = m_pendingLut[tab].empty() ? cv::Mat()
                                                          : m_pendingLut[tab].clone();
        pending.adjustmentParams = m_pendingParams[tab];
        pending.adjustmentChannelLuts = m_pendingChannelLuts[tab];
        if (sameAdjustmentState(pending, *l)) {
            m_pendingLut[tab].release();
            m_pendingValid[tab] = false;
            m_pendingParams[tab] = layers::AdjustmentParams();
            m_pendingChannelLuts[tab].clear();
            continue;
        }
        //  the undo command holds the OLD adjustment state -- restoring it restores the effect from before the adjustment
        auto* cmd = layers::LayerCommand::makeSetAdjustmentState(
            stack, layerIdx, m_pendingLut[tab], m_pendingParams[tab],
            m_pendingChannelLuts[tab]);
        m_host->undoStack()->push(cmd);
        m_pendingLut[tab].release();
        m_pendingValid[tab] = false;
        m_pendingParams[tab] = layers::AdjustmentParams();
        m_pendingChannelLuts[tab].clear();
    }
}

void AdjustmentPanel::pushUndoCommand()
{
    // 改 commitUndoDebounced 实现, pushUndoCommand 是它的公开别名
    //   让外部 (对话框 / 拖动结束) 能强制立即 commit, 不等 200ms debounce
    if (m_commitTimer && m_commitTimer->isActive()) {
        m_commitTimer->stop();
    }
    commitUndoDebounced();
}

// =============================================================
// Standalone DialogFactory dispatcher
//   CurvesAdjustDialog::applied(args) -> setStandaloneParams("Curves", args)
//   路由到对应 applyXxxFromArgs, 更新 m_* struct + 刷 UI + applyCurrentTab
// =============================================================
void AdjustmentPanel::setStandaloneParams(const QString& dialogId, const QVariantMap& args)
{
    if (dialogId == "Curves") {
        applyCurvesFromArgs(args, /*previewMode=*/false);
    } else if (dialogId == "Levels") {
        applyLevelsFromArgs(args, /*previewMode=*/false);
    } else if (dialogId == "B&W") {
        applyBnWFromArgs(args, /*previewMode=*/false);
    } else if (dialogId == "ChannelMixer") {
        applyChannelMixerFromArgs(args, /*previewMode=*/false);
    } else if (dialogId == "HSL") {
        // HslAdjustDialog rewritten to use
        //   8 hue + 8 sat + 1 lightness (PS-compatible). Args land here.
        applyHslFromArgs(args, /*previewMode=*/false);
    } else {
        LOG_WARN("[AdjustmentPanel] setStandaloneParams unknown dialogId: {}",
                 dialogId.toStdString());
        return;
    }
}

void AdjustmentPanel::applyCurvesFromArgs(const QVariantMap& args, bool previewMode)
{
    // {"channel": "RGB", "points": [{x,y}, ...]}
    // M2-6b: switch the tab BEFORE the values go into the inline UI.
    //   Setting a slider fires valueChanged, and the slider handler
    //   calls applyCurrentTab() -- so the tab has to be the right one
    //   already, otherwise the apply lands on the previous tab and
    //   creates an adjustment layer of the wrong kind.
    m_tabWidget->setCurrentIndex(kTabCurves);
    const QVariantList ptsVar = args.value("points").toList();
    QPolygonF pts;
    pts.reserve(ptsVar.size());
    for (const QVariant& v : ptsVar) {
        const QVariantMap pt = v.toMap();
        pts << QPointF(pt.value("x").toDouble(), pt.value("y").toDouble());
    }
    if (pts.isEmpty()) pts = CurveEditor::defaultPoints();
    // M2-6b: the dialog edits ONE channel at a time, so the curve goes
    // into the slot of that channel. The inline editor always shows the
    // master (RGB) curve.
    const QString channel = args.value(QString("channel"),
                                        QStringLiteral("RGB")).toString();
    if (channel.compare(QStringLiteral("Red"), Qt::CaseInsensitive) == 0) {
        m_curves.redPoints = pts;
    } else if (channel.compare(QStringLiteral("Green"), Qt::CaseInsensitive) == 0) {
        m_curves.greenPoints = pts;
    } else if (channel.compare(QStringLiteral("Blue"), Qt::CaseInsensitive) == 0) {
        m_curves.bluePoints = pts;
    } else {
        m_curves.controlPoints = pts;
        // Refresh inline UI
        if (m_curvesEditor) m_curvesEditor->setControlPoints(pts);
    }
    // Switch to curves tab and apply
    emit paramChanged();
    if (previewMode) {
        applyTabLutInPreviewMode(kTabCurves, buildCurrentTabLut());
    } else {
        applyCurrentTab();
    }
}

void AdjustmentPanel::applyLevelsFromArgs(const QVariantMap& args, bool previewMode)
{
    // {"channel", "inLow", "inHigh", "gamma", "outLow", "outHigh"}
    // M2-6b: switch the tab BEFORE the values go into the inline UI.
    //   Setting a slider fires valueChanged, and the slider handler
    //   calls applyCurrentTab() -- so the tab has to be the right one
    //   already, otherwise the apply lands on the previous tab and
    //   creates an adjustment layer of the wrong kind.
    m_tabWidget->setCurrentIndex(kTabLevels);
    m_levels.inLow  = args.value("inLow", 0).toInt();
    m_levels.inHigh = args.value("inHigh", 255).toInt();
    m_levels.gamma  = args.value("gamma", 1.0).toDouble();
    m_levels.outLow  = args.value("outLow", 0).toInt();
    m_levels.outHigh = args.value("outHigh", 255).toInt();
    // Cross-over guard: inLow < inHigh
    if (m_levels.inLow >= m_levels.inHigh) m_levels.inLow = m_levels.inHigh - 1;
    // Refresh 5 inline sliders if they exist (find them by index in m_levelsSliders if available;
    // current AdjustmentPanel stores m_levelsSliders in m_levels, but inline sliders are stored
    // in m_levelsInLow/m_levelsInHigh/m_levelsGamma/m_levelsOutLow/m_levelsOutHigh. Look those up.)
    if (m_levelsInLow)   m_levelsInLow->setValue(m_levels.inLow);
    if (m_levelsInHigh)  m_levelsInHigh->setValue(m_levels.inHigh);
    if (m_levelsGamma)   m_levelsGamma->setValue(int(m_levels.gamma * 100));
    if (m_levelsOutLow)  m_levelsOutLow->setValue(m_levels.outLow);
    if (m_levelsOutHigh) m_levelsOutHigh->setValue(m_levels.outHigh);
    // Refresh value labels
    if (m_levelsInLowVal)   m_levelsInLowVal->setText(QString::number(m_levels.inLow));
    if (m_levelsInHighVal)  m_levelsInHighVal->setText(QString::number(m_levels.inHigh));
    if (m_levelsGammaVal)   m_levelsGammaVal->setText(QString::number(m_levels.gamma, 'f', 2));
    if (m_levelsOutLowVal)  m_levelsOutLowVal->setText(QString::number(m_levels.outLow));
    if (m_levelsOutHighVal) m_levelsOutHighVal->setText(QString::number(m_levels.outHigh));
    m_tabWidget->setCurrentIndex(kTabLevels);
    emit paramChanged();
    if (previewMode) {
        applyTabLutInPreviewMode(kTabLevels, buildCurrentTabLut());
    } else {
        applyCurrentTab();
    }
}

void AdjustmentPanel::applyBnWFromArgs(const QVariantMap& args, bool previewMode)
{
    // {"color0".."color5", "tintHue", "tintSat"}
    // M2-6b: switch the tab BEFORE the values go into the inline UI.
    //   Setting a slider fires valueChanged, and the slider handler
    //   calls applyCurrentTab() -- so the tab has to be the right one
    //   already, otherwise the apply lands on the previous tab and
    //   creates an adjustment layer of the wrong kind.
    m_tabWidget->setCurrentIndex(kTabBlackWhite);
    if (m_bw.rgbMixer.size() != 6) m_bw.rgbMixer.resize(6, 100.0);
    for (int i = 0; i < 6; ++i) {
        const int v = args.value(QString("color%1").arg(i), 100).toInt();
        m_bw.rgbMixer[i] = v;
        if (i < m_bwSliders.size()) m_bwSliders[i]->setValue(v);
    }
    m_bw.tintHue = args.value("tintHue", 0).toInt();
    m_bw.tintSat = args.value("tintSat", 0).toInt();
    // Tint sliders are not in inline tab (buildBlackWhitePage doesn't add tint controls).
    // Store values; LUT extension to apply tint is deferred to P0-3.x v2.
    m_tabWidget->setCurrentIndex(kTabBlackWhite);
    emit paramChanged();
    if (previewMode) {
        applyTabLutInPreviewMode(kTabBlackWhite, buildCurrentTabLut());
    } else {
        applyCurrentTab();
    }
}

void AdjustmentPanel::applyChannelMixerFromArgs(const QVariantMap& args, bool previewMode)
{
    // {"m00".."m22", "monochrome"}
    // M2-6b: switch the tab BEFORE the values go into the inline UI.
    //   Setting a slider fires valueChanged, and the slider handler
    //   calls applyCurrentTab() -- so the tab has to be the right one
    //   already, otherwise the apply lands on the previous tab and
    //   creates an adjustment layer of the wrong kind.
    m_tabWidget->setCurrentIndex(kTabChannelMixer);
    m_cm.rR = args.value("m00", 200).toInt();
    m_cm.rG = args.value("m01", 0).toInt();
    m_cm.rB = args.value("m02", 0).toInt();
    m_cm.gR = args.value("m10", 0).toInt();
    m_cm.gG = args.value("m11", 200).toInt();
    m_cm.gB = args.value("m12", 0).toInt();
    m_cm.bR = args.value("m20", 0).toInt();
    m_cm.bG = args.value("m21", 0).toInt();
    m_cm.bB = args.value("m22", 200).toInt();
    m_cm.monochrome = args.value("monochrome", false).toBool();
    // Refresh 9 inline sliders (m_cmSliders indexed row*3+col)
    if (m_cmSliders.size() == 9) {
        m_cmSliders[0]->setValue(m_cm.rR); m_cmSliders[1]->setValue(m_cm.rG); m_cmSliders[2]->setValue(m_cm.rB);
        m_cmSliders[3]->setValue(m_cm.gR); m_cmSliders[4]->setValue(m_cm.gG); m_cmSliders[5]->setValue(m_cm.gB);
        m_cmSliders[6]->setValue(m_cm.bR); m_cmSliders[7]->setValue(m_cm.bG); m_cmSliders[8]->setValue(m_cm.bB);
    }
    // Monochrome checkbox not in inline tab yet (deferred).
    m_tabWidget->setCurrentIndex(kTabChannelMixer);
    emit paramChanged();
    if (previewMode) {
        applyTabLutInPreviewMode(kTabChannelMixer, buildCurrentTabLut());
    } else {
        applyCurrentTab();
    }
}

// HSL standalone dialog args land
//   here. 8 hue + 8 sat + 1 lightness (PS-compatible band layout:
//   master + R, Y, G, C, B, M).
void AdjustmentPanel::applyHslFromArgs(const QVariantMap& args, bool previewMode)
{
    // M2-6b: switch the tab BEFORE the values go into the inline UI.
    //   Setting a slider fires valueChanged, and the slider handler
    //   calls applyCurrentTab() -- so the tab has to be the right one
    //   already, otherwise the apply lands on the previous tab and
    //   creates an adjustment layer of the wrong kind.
    m_tabWidget->setCurrentIndex(kTabHsl);
    // m_hsl.hueShifts / m_hsl.satShifts default to 8 zeros in ctor.
    if (m_hsl.hueShifts.size() != 8) m_hsl.hueShifts.resize(8);
    if (m_hsl.satShifts.size() != 8) m_hsl.satShifts.resize(8);
    const QVariantList hueList = args.value("hueShifts").toList();
    const QVariantList satList = args.value("satShifts").toList();
    for (int i = 0; i < 8; ++i) {
        if (i < hueList.size()) m_hsl.hueShifts[i] = hueList[i].toInt();
        if (i < m_hslHueSliders.size() && m_hslHueSliders[i]) {
            m_hslHueSliders[i]->setValue(m_hsl.hueShifts[i]);
        }
        if (i < satList.size()) m_hsl.satShifts[i] = satList[i].toInt();
        if (i < m_hslSatSliders.size() && m_hslSatSliders[i]) {
            m_hslSatSliders[i]->setValue(m_hsl.satShifts[i]);
        }
    }
    m_hsl.lightness = args.value("lightness", 0).toInt();
    if (m_hslLightness) m_hslLightness->setValue(m_hsl.lightness);
    m_tabWidget->setCurrentIndex(kTabHsl);
    emit paramChanged();
    if (previewMode) {
        applyTabLutInPreviewMode(kTabHsl, buildCurrentTabLut());
    } else {
        applyCurrentTab();
    }
}
// =============================================================
// M2-6a: the live preview session
//
// The live preview of the standalone adjustment dialogs (Curves/Levels/HSL/B&W/ChannelMixer).
//
// Why applyCurrentTab cannot be reused:
//   applyCurrentTab is the "write the adjustment + push one undo command after a 200ms debounce" path.
//   The preview would push a command per slider drag, the stack would explode, and Cancel
//   would leave the junk entries the preview left in the undo stack.
//
// So the preview takes another route:
//   1. beginPreviewSession  -- takes the baseline snapshot (the state of the adjustment layer of that tab)
//   2. previewStandaloneParams -- writes the adjustment, pushes no undo command
//   3. commitPreviewSession -- freezes it into EXACTLY ONE undo command
//   4. cancelPreviewSession -- restores the baseline snapshot (and removes the layer the preview created)
//
// The undo command that freezes the preview is the PreviewCommitCommand of this file:
//   it holds the state of the adjustment layer of that tab before the session started (exists / does not exist, LUT,
//   opacity, full Layer snapshot), undo writes it back as it was; redo writes the state at the end of the
//   session back. One undo = the picture from before the dialog opened, one redo =
//   the picture from when the dialog closed.
//
// Why not LayerCommand::SetAdjustmentLut:
//   that command holds one old LUT and writes it back on undo -- but it does NOT remove the
//   adjustment layer the preview created. Undoing after the preview created a layer leaves an identity LUT
//   adjustment layer in the stack: the picture may look the same, but the layer structure is dirty.
// =============================================================

namespace {

// The undo command that freezes the preview (only used in this file).
//
// The before / after states describe the adjustment layer of a tab at two moments:
//   existed  -- did the layer exist then
//   index   -- its index if it existed (a layer the session created sits on top of the stack at the end)
//   lut     -- its LUT if it existed
//   opacity -- its opacity if it existed
//   layer   -- its full Layer snapshot if it existed (used to put the layer back in its place)
//
// undo(): writes the before state. redo(): writes the after state.
class PreviewCommitCommand : public QUndoCommand
{
public:
    struct State {
        bool        existed = false;
        int         index   = -1;
        float       opacity = 1.0f;
        cv::Mat     lut;
        layers::Layer layer;
    };

    PreviewCommitCommand(layers::LayerStack* stack,
                         const State& before,
                         const State& after,
                         const QString& text)
        : QUndoCommand(text),
          m_stack(stack),
          m_before(before),
          m_after(after)
    {
    }

    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }

private:
    void apply(const State& s)
    {
        if (!m_stack) return;

        // First remove the adjustment layer the session may have created -- it belongs to
        // the history of no tab, only this command knows its stableId.
        if (m_after.existed && m_after.layer.stableId != 0) {
            const int idx = m_stack->indexById(m_after.layer.stableId);
            if (idx >= 0) {
                m_stack->removeLayer(idx);
            }
        }

        // Then write the target state back.
        if (!s.existed) return;

        // The layer does not exist -> create one (at s.index, or on top of the stack)
        const int insertAt = (s.index >= 0 && s.index <= m_stack->count())
                                 ? s.index
                                 : m_stack->count();
        layers::Layer l = s.layer;
        l.adjustmentLut = s.lut.empty() ? cv::Mat() : s.lut.clone();
        l.opacity = s.opacity;
        const int added = m_stack->insertLayer(insertAt, l);
        if (added >= 0) {
            m_stack->notifyLayerChanged(added);
        }
    }

    layers::LayerStack* m_stack = nullptr;
    State m_before;
    State m_after;
};

}  // namespace

int AdjustmentPanel::tabIndexForDialogId(const QString& dialogId)
{
    if (dialogId == "Curves")       return kTabCurves;
    if (dialogId == "Levels")       return kTabLevels;
    if (dialogId == "HSL")          return kTabHsl;
    if (dialogId == "B&W")          return kTabBlackWhite;
    if (dialogId == "ChannelMixer") return kTabChannelMixer;
    return -1;
}

int AdjustmentPanel::adjustmentLayerIndexForTab(int tab) const
{
    if (tab < 0 || tab >= kTabCount) return -1;
    int idx = m_adjLayerIndex[tab];
    if (idx < 0) return -1;
    if (!m_host) return -1;
    auto* stack = m_host->layerStack();
    if (!stack) return -1;
    if (idx >= stack->count()) return -1;
    auto l = stack->at(idx);
    if (!l || l->kind != layers::Layer::Adjustment) return -1;
    return idx;
}

bool AdjustmentPanel::beginPreviewSession(const QString& dialogId)
{
    const int tab = tabIndexForDialogId(dialogId);
    if (tab < 0) {
        LOG_WARN("[AdjustmentPanel] beginPreviewSession unknown dialogId: {}",
                 dialogId.toStdString());
        return false;
    }
    if (!m_host) return false;
    auto* stack = m_host->layerStack();
    if (!stack) return false;

    PreviewSession s;
    s.active   = true;
    s.dialogId = dialogId;
    s.tab      = tab;

    // The baseline: the state of the adjustment layer of this tab right now
    const int idx = adjustmentLayerIndexForTab(tab);
    if (idx >= 0) {
        auto l = stack->at(idx);
        s.layerExisted    = true;
        s.layerIndex      = idx;
        s.baselineOpacity = l->opacity;
        s.baselineLut     = l->adjustmentLut.empty() ? cv::Mat() : l->adjustmentLut.clone();
        s.baselineLayerSnapshot = *l;
    } else {
        s.layerExisted    = false;
        s.layerIndex      = -1;
        s.baselineOpacity = 1.0f;
        s.baselineLut.release();
    }

    m_previewSession = s;
    LOG_DEBUG("[AdjustmentPanel] beginPreviewSession: id={} tab={} layerExisted={}",
              dialogId.toStdString(), tab, s.layerExisted);
    return true;
}

void AdjustmentPanel::previewStandaloneParams(const QString& dialogId,
                                              const QVariantMap& args)
{
    const int tab = tabIndexForDialogId(dialogId);
    if (tab < 0) {
        LOG_WARN("[AdjustmentPanel] previewStandaloneParams unknown dialogId: {}",
                 dialogId.toStdString());
        return;
    }
    if (!m_host) return;
    auto* stack = m_host->layerStack();
    if (!stack) return;

    // The parameters are routed exactly like setStandaloneParams -- only the final write
    // pushes no undo command.
    if (dialogId == "Curves") {
        applyCurvesFromArgs(args, /*previewMode=*/true);
    } else if (dialogId == "Levels") {
        applyLevelsFromArgs(args, /*previewMode=*/true);
    } else if (dialogId == "B&W") {
        applyBnWFromArgs(args, /*previewMode=*/true);
    } else if (dialogId == "ChannelMixer") {
        applyChannelMixerFromArgs(args, /*previewMode=*/true);
    } else if (dialogId == "HSL") {
        applyHslFromArgs(args, /*previewMode=*/true);
    } else {
        LOG_WARN("[AdjustmentPanel] previewStandaloneParams unknown dialogId: {}",
                 dialogId.toStdString());
        return;
    }
    (void)tab;
}

void AdjustmentPanel::applyTabLutInPreviewMode(int tab, const cv::Mat& lut)
{
    if (!m_host) return;
    auto* stack = m_host->layerStack();
    if (!stack) return;
    if (lut.empty()) return;

    int layerIdx = adjustmentLayerIndexForTab(tab);
    if (layerIdx < 0) {
        // This tab has no adjustment layer yet -> create one. A layer the preview creates does not go into the undo stack,
        // the undo / Cancel restores the baseline (see cancelPreviewSession).
        layerIdx = stack->addAdjustmentLayer(tabName(tab), tabName(tab).toLower(), lut);
        if (layerIdx < 0) return;
        m_adjLayerIndex[tab] = layerIdx;
        m_pendingLut[tab].release();
        m_pendingValid[tab] = false;
    } else {
        // Write the adjustment, but do NOT touch m_pendingLut (that is the "before" of the debounce commit).
        // The before of the preview lives in the baseline snapshot.
        stack->setAdjustmentLut(layerIdx, lut);
    }

    //  M2-6b: the preview writes the per-pixel payload and the per-channel LUTs too, otherwise the previewed picture
    //  would differ from the picture of a real apply.
    stack->setAdjustmentParams(layerIdx, buildTabParams(tab));
    stack->setAdjustmentChannelLuts(layerIdx, buildTabChannelLuts(tab));

    // The canvas changes at once (that is what a preview is for)
    m_host->refreshCanvasNow();

    // Note: the commit timer is deliberately NOT started here -- the preview pushes no undo command.
}

void AdjustmentPanel::commitPreviewSession(const QString& dialogId)
{
    if (!m_previewSession.active) return;
    if (!m_host) return;
    auto* stack = m_host->layerStack();
    if (!stack) return;

    const int tab = m_previewSession.tab;

    // The after state = the state of the adjustment layer of this tab right now
    PreviewCommitCommand::State after;
    const int idx = adjustmentLayerIndexForTab(tab);
    if (idx >= 0) {
        auto l = stack->at(idx);
        after.existed = true;
        after.index   = idx;
        after.opacity = l->opacity;
        after.lut     = l->adjustmentLut.empty() ? cv::Mat() : l->adjustmentLut.clone();
        after.layer   = *l;
    }

    PreviewCommitCommand::State before;
    before.existed = m_previewSession.layerExisted;
    before.index   = m_previewSession.layerIndex;
    before.opacity = m_previewSession.baselineOpacity;
    before.lut     = m_previewSession.baselineLut;
    before.layer   = m_previewSession.baselineLayerSnapshot;

    // A layer the preview created is not in the baseline -> before.existed == false,
    // and the undo command removes it.

    m_previewSession.active = false;

    // Clean up the tail of the debounce path.
    //
    // OK / Apply go through applied -> setStandaloneParams -> applyCurrentTab(),
    // and that path writes the adjustment and starts the 200ms debounce timer. Left alone, 200ms later
    // commitUndoDebounced() would push ANOTHER undo command -- one OK would leave two.
    // So the timer is stopped here and the pending "before" is dropped: the preview session holds
    // the baseline itself and does not need the one of the debounce path.
    if (m_commitTimer && m_commitTimer->isActive()) {
        m_commitTimer->stop();
    }
    m_pendingLut[tab].release();

    // Nothing changed -> no undo command (the user opened the dialog and closed it again)
    //
    // M2-6b: the comparison covers the WHOLE adjustment state, not just the LUT. The HSL hue slider
    // does not change the 1D LUT at all, so comparing the LUT alone would miss the whole adjustment.
    if (!before.existed && !after.existed) return;
    if (before.existed && after.existed) {
        if (sameAdjustmentState(before.layer, after.layer)) {
            // The adjustment state did not change -> there is nothing to undo
            return;
        }
    }

    auto* cmd = new PreviewCommitCommand(
        stack, before, after,
        QStringLiteral("Adjustment ") + dialogId);
    if (m_host->undoStack()) {
        m_host->undoStack()->push(cmd);
    } else {
        delete cmd;
    }
    LOG_DEBUG("[AdjustmentPanel] commitPreviewSession: id={} undoCount={}",
              dialogId.toStdString(),
              m_host->undoStack() ? m_host->undoStack()->count() : -1);
}

void AdjustmentPanel::cancelPreviewSession(const QString& dialogId)
{
    if (!m_previewSession.active) return;
    if (!m_host) return;
    auto* stack = m_host->layerStack();
    if (!stack) return;

    const int tab = m_previewSession.tab;
    const bool existed  = m_previewSession.layerExisted;
    const int  baseIdx  = m_previewSession.layerIndex;
    const float baseOp  = m_previewSession.baselineOpacity;
    const cv::Mat baseLut = m_previewSession.baselineLut;
    const layers::Layer baseLayer = m_previewSession.baselineLayerSnapshot;

    m_previewSession.active = false;

    // The current state (the preview may have created a layer)
    const int idx = adjustmentLayerIndexForTab(tab);

    if (!existed) {
        // The baseline has no adjustment layer -> remove the one the preview created
        if (idx >= 0) {
            stack->removeLayer(idx);
            m_adjLayerIndex[tab] = -1;
        }
    } else {
        // The baseline has one -> restore its whole adjustment state and its opacity
        if (idx >= 0) {
            auto l = stack->at(idx);
            l->adjustmentLut = baseLut.empty() ? cv::Mat() : baseLut.clone();
            // M2-6b: the per-pixel payload and the per-channel LUTs have to be restored too, otherwise after Cancel
            // the layer would still carry the HSL / black & white parameters of the preview.
            l->adjustmentParams = baseLayer.adjustmentParams;
            l->adjustmentChannelLuts = baseLayer.adjustmentChannelLuts;
            l->opacity = baseOp;
            stack->notifyLayerChanged(idx);
        } else {
            // Something else removed the layer during the preview -> put it back
            const int insertAt = (baseIdx >= 0 && baseIdx <= stack->count())
                                     ? baseIdx
                                     : stack->count();
            layers::Layer l = baseLayer;
            l.adjustmentLut = baseLut.empty() ? cv::Mat() : baseLut.clone();
            l.opacity = baseOp;
            const int added = stack->insertLayer(insertAt, l);
            if (added >= 0) {
                m_adjLayerIndex[tab] = added;
                stack->notifyLayerChanged(added);
            }
        }
    }

    // The canvas returns to the state before the session started
    m_host->refreshCanvasNow();
    LOG_DEBUG("[AdjustmentPanel] cancelPreviewSession: id={}", dialogId.toStdString());
}
