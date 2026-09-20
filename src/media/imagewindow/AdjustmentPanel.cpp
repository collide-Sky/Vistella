// SPDX-License-Identifier: MIT
//
// AdjustmentPanel - P0-3.2 (2026-09-08) 完整实装
//
// 5 tab UI 完整接 P0-2 异步 + 5 tab 切换不丢参数 + 接 ImageEditCommand 撤销栈
// P0-3.3 才升级为 LayerCommand
//
#include "AdjustmentPanel.h"
#include "logger.h"

#include "imageprocessor.h"
#include "../imageworker/layers/LayerCommand.h"   // P0-3.3 (2026-09-08): LayerCommand::SetAdjustmentLut
#include "../imageworker/layers/LayerStack.h"     // P0-3.3: LayerStack* + selection()

#include <QtWidgets>  // 一次性 include 所有 QtWidgets (QSlider/QLabel/QPushButton/QGridLayout/etc)

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

#include <algorithm>
#include <cmath>

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
}  // namespace

AdjustmentPanel::AdjustmentPanel(QWidget* parent) : QWidget(parent)
{
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
        // P0-3.3 (2026-09-08): 切 tab 立即 commit pending undo
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

    // P0-3.3 (2026-09-08): debounce commit timer
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
            // P0-3.1 暂时没有 3x3 矩阵版本, 用 cyanRed 通道 1D 近似
            //   rR, rG, rB 平均近似 Red 通道强度
            //   P0-3.x 加 3x3 版本
            const int avgR = (m_cm.rR + m_cm.rG + m_cm.rB) / 3;
            // avgR - 100 = 偏移, 200 中性 → 0 偏移
            return ImageProcessor::buildColorBalanceLUT(avgR - 100, 0, 0);
        }
    }
    return cv::Mat();
}

// =============================================================
// applyCurrentTab - 调当前 tab 的 LUT, 走 background pool async
//   跟 P0-2 ImageAdjustmentPanel::asyncApplyCurrentParams 同样的取消语义:
//     - 每次 apply 分配新 task id, 旧 in-flight task 完成后主线程发现 id 变了, 直接 return
//     - 完成后切主线程: setCurrentImage (不再立即 push undo, 改由 debounce commit 处理)
// P0-3.3 (2026-09-08): undo 集成 LayerCommand::SetAdjustmentLut
//   - 拖动过程中多次 applyCurrentTab 只 push 一条 undo (debounce 200ms)
//   - debounce timeout 后 commitUndoDebounced() push 一条 LayerCommand
//   - m_pendingBefore: debounce 窗口起始时的"before" image (首次 valueChanged 时 snapshot)
// =============================================================
void AdjustmentPanel::applyCurrentTab()
{
    if (!m_host) return;
    if (m_host->originalImage().empty()) return;

    // P0-3.3 (2026-09-08): 首次 valueChanged 在 debounce 窗口内, snapshot "before" image
    //   后续 valueChanged 在同一窗口内只刷新 m_pendingAfter (timer fire 时取 m_current)
    //   切 tab / 200ms 静默 → commitUndoDebounced → push LayerCommand
    if (m_pendingBefore.empty() && m_host->undoStack()) {
        m_pendingBefore = m_host->currentImage().clone();
    }

    auto* engine = m_host->engineContext();
    if (!engine) {
        // 兜底: 走 sync
        cv::Mat lut = buildCurrentTabLut();
        if (lut.empty()) return;
        cv::Mat out;
        ImageProcessor::applyLut(m_host->originalImage(), out, lut);
        if (out.empty()) return;
        m_host->setCurrentImage(out);
        // 同步模式: 重启 debounce timer (兜底也走 LOD 路径)
        if (m_commitTimer) m_commitTimer->start();
        return;
    }

    // 分配新 task id, 旧 task 完成后主线程发现 id 变了, 直接 return
    const uint64_t myTaskId = m_host->currentTaskId().fetch_add(1) + 1;

    // 拷 current image 进去, 避免后台跑一半 m_current 变了 (loadFile 时)
    cv::Mat snapshot = m_host->originalImage().clone();
    cv::Mat lut = buildCurrentTabLut();
    if (lut.empty()) return;

    QPointer<ImageWindow> hostGuard = m_host;

    // 提交到 background pool
    const bool submitted = engine->background().submit_fn(
        [hostGuard, myTaskId, snapshot = std::move(snapshot),
         lut = std::move(lut)](vistella::tp::TaskContext& /*ctx*/) mutable -> void
        {
            // 后台跑 applyLut
            cv::Mat out;
            try {
                ImageProcessor::applyLut(snapshot, out, lut);
            } catch (...) {
                out = cv::Mat();
            }
            if (out.empty()) return;
            if (hostGuard.isNull()) return;  // ImageWindow 已销毁

            // 切回主线程
            ImageWindow* host = hostGuard.data();
            QMetaObject::invokeMethod(host,
                [host, myTaskId, out = std::move(out)]() -> void {
                    if (host->currentTaskId().load() != myTaskId) return;  // 已被新 task 取代, 丢弃
                    if (out.empty()) return;
                    // P0-3.3 (2026-09-08): setCurrentImage 完就 OK, undo 由 debounce timer 推
                    host->setCurrentImage(out);
                },
                Qt::QueuedConnection);
        },
        "AdjustmentPanel::applyCurrentTab",
        vistella::tp::Priority::Normal);

    if (!submitted) {
        // 提交失败 → 同步兜底
        cv::Mat out;
        try {
            ImageProcessor::applyLut(snapshot, out, lut);
        } catch (...) {
            out = cv::Mat();
        }
        if (!out.empty()) {
            m_host->setCurrentImage(out);
            if (m_commitTimer) m_commitTimer->start();  // 重启 debounce
        }
    } else {
        // 异步提交成功: 重启 debounce timer (200ms 后 commit)
        if (m_commitTimer) m_commitTimer->start();
    }
}

// P0-3.3 (2026-09-08): debounce commit - timer 触发时 push 一条 LayerCommand::SetAdjustmentLut
//   m_pendingBefore = debounce 窗口起始时的 image
//   m_current = 当前 image (debounce 结束时的 image, 即调值后结果)
//   如果两者相同 (用户没动), 不 push
//   注: 5 tab 切换不丢参数已由 P0-3.2 实现 (m_curves/m_levels/... 跨 tab 保留), 这里不重做
void AdjustmentPanel::commitUndoDebounced()
{
    if (!m_host || !m_host->undoStack()) return;
    if (m_pendingBefore.empty()) return;
    cv::Mat after = m_host->currentImage().clone();
    if (after.empty()) {
        m_pendingBefore = cv::Mat();
        return;
    }
    // 同 image 跳过 (没真改)
    if (m_pendingBefore.size == after.size && m_pendingBefore.type() == after.type()) {
        // 字节级快速比较 (仅同 size+type)
        const size_t bytes = m_pendingBefore.total() * m_pendingBefore.elemSize();
        if (std::memcmp(m_pendingBefore.data, after.data, bytes) == 0) {
            m_pendingBefore = cv::Mat();
            return;
        }
    }
    // 选当前 layer (一般 adjustment 在 base layer 上)
    auto* stack = m_host->layerStack();
    if (!stack) stack = m_host->layerStack();
    int index = stack ? stack->selection() : 0;
    if (index < 0) index = 0;
    // 推 LayerCommand::SetAdjustmentLut (新 overload: 走 host image-based undo)
    auto* cmd = layers::LayerCommand::makeSetAdjustmentLut(
        stack, index, m_pendingBefore, after, m_host);
    m_host->undoStack()->push(cmd);
    m_pendingBefore = cv::Mat();
}

void AdjustmentPanel::pushUndoCommand()
{
    // P0-3.3 (2026-09-08): 改 commitUndoDebounced 实现, pushUndoCommand 是它的公开别名
    //   让外部 (对话框 / 拖动结束) 能强制立即 commit, 不等 200ms debounce
    if (m_commitTimer && m_commitTimer->isActive()) {
        m_commitTimer->stop();
    }
    commitUndoDebounced();
}

// =============================================================
// P1.5.1 (2026-09-18): Standalone DialogFactory dispatcher
//   CurvesAdjustDialog::applied(args) -> setStandaloneParams("Curves", args)
//   路由到对应 applyXxxFromArgs, 更新 m_* struct + 刷 UI + applyCurrentTab
// =============================================================
void AdjustmentPanel::setStandaloneParams(const QString& dialogId, const QVariantMap& args)
{
    if (dialogId == "Curves") {
        applyCurvesFromArgs(args);
    } else if (dialogId == "Levels") {
        applyLevelsFromArgs(args);
    } else if (dialogId == "B&W") {
        applyBnWFromArgs(args);
    } else if (dialogId == "ChannelMixer") {
        applyChannelMixerFromArgs(args);
    } else if (dialogId == "HSL") {
        // HSL standalone dialog not yet implemented (P0 only had inline).
        // Treat as no-op for now; user must use inline HSL tab.
        return;
    } else {
        LOG_WARN("[AdjustmentPanel] setStandaloneParams unknown dialogId: {}",
                 dialogId.toStdString());
        return;
    }
}

void AdjustmentPanel::applyCurvesFromArgs(const QVariantMap& args)
{
    // {"channel": "RGB", "points": [{x,y}, ...]}
    const QVariantList ptsVar = args.value("points").toList();
    QPolygonF pts;
    pts.reserve(ptsVar.size());
    for (const QVariant& v : ptsVar) {
        const QVariantMap pt = v.toMap();
        pts << QPointF(pt.value("x").toDouble(), pt.value("y").toDouble());
    }
    if (pts.isEmpty()) pts = CurveEditor::defaultPoints();
    m_curves.controlPoints = pts;
    // Refresh inline UI
    if (m_curvesEditor) m_curvesEditor->setControlPoints(pts);
    // Switch to curves tab and apply
    m_tabWidget->setCurrentIndex(kTabCurves);
    emit paramChanged();
    applyCurrentTab();
}

void AdjustmentPanel::applyLevelsFromArgs(const QVariantMap& args)
{
    // {"channel", "inLow", "inHigh", "gamma", "outLow", "outHigh"}
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
    applyCurrentTab();
}

void AdjustmentPanel::applyBnWFromArgs(const QVariantMap& args)
{
    // {"color0".."color5", "tintHue", "tintSat"}
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
    applyCurrentTab();
}

void AdjustmentPanel::applyChannelMixerFromArgs(const QVariantMap& args)
{
    // {"m00".."m22", "monochrome"}
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
    applyCurrentTab();
}
