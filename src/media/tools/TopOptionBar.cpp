#include "TopOptionBar.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

namespace tools {

TopOptionBar::TopOptionBar(QWidget* parent) : QWidget(parent)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 0, 6, 0);
    root->setSpacing(8);

    // 模式 (PS 笔刷模式): 正常 / 背后 / 清除  (PS 完整还有 实色 / 滤色 / 叠加 等, P7.9 扩)
    m_cmbBlendMode = new QComboBox(this);
    m_cmbBlendMode->setToolTip(tr("模式"));
    m_cmbBlendMode->addItem(tr("正常"));
    m_cmbBlendMode->addItem(tr("背后"));
    m_cmbBlendMode->addItem(tr("清除"));
    m_cmbBlendMode->setCurrentIndex(static_cast<int>(BlendMode::Normal));
    root->addWidget(m_cmbBlendMode);

    // 不透明度 (PS 笔刷 0..100%) — 双击恢复 100%
    m_spnOpacity = new QDoubleSpinBox(this);
    m_spnOpacity->setRange(0.0, 100.0);
    m_spnOpacity->setSuffix(QStringLiteral("%"));
    m_spnOpacity->setDecimals(0);
    m_spnOpacity->setValue(100.0);
    m_spnOpacity->setToolTip(tr("不透明度 (Opacity)"));
    root->addWidget(m_spnOpacity);

    // 流量 — PS 标准
    m_spnFlow = new QDoubleSpinBox(this);
    m_spnFlow->setRange(0.0, 100.0);
    m_spnFlow->setSuffix(QStringLiteral("%"));
    m_spnFlow->setDecimals(0);
    m_spnFlow->setValue(100.0);
    m_spnFlow->setToolTip(tr("流量 (Flow)"));
    root->addWidget(m_spnFlow);

    // 平滑 — PS 笔刷 Smooth 0..100% (Q4.2.3 Brush 已默认启用 smoothing)
    m_spnSmoothing = new QDoubleSpinBox(this);
    m_spnSmoothing->setRange(0.0, 100.0);
    m_spnSmoothing->setSuffix(QStringLiteral("%"));
    m_spnSmoothing->setDecimals(0);
    m_spnSmoothing->setValue(10.0);
    m_spnSmoothing->setToolTip(tr("平滑 (Smoothing)"));
    root->addWidget(m_spnSmoothing);

    // 角度 — 跟 大小 各自一颗 lock btn (PS 标准图标)
    m_spnAngle = new QSpinBox(this);
    m_spnAngle->setRange(0, 359);
    m_spnAngle->setSuffix(QStringLiteral("°"));
    m_spnAngle->setValue(0);
    m_spnAngle->setToolTip(tr("角度 (Angle)"));
    root->addWidget(m_spnAngle);

    // 角度 lock btn — PS 风格, 锁住不让 用户改角度 (形状笔刷)
    m_btnAngleLock = new QToolButton(this);
    m_btnAngleLock->setText(QStringLiteral("🔓"));
    m_btnAngleLock->setCheckable(true);
    m_btnAngleLock->setToolTip(tr("锁角度 (L)"));
    root->addWidget(m_btnAngleLock);

    // 大小 input — PS 风格 (px)
    m_spnSize = new QSpinBox(this);
    m_spnSize->setRange(1, 2000);
    m_spnSize->setSuffix(QStringLiteral(" px"));
    m_spnSize->setValue(13);
    m_spnSize->setToolTip(tr("大小 (Size, 像素)"));
    root->addWidget(m_spnSize);

    // 大小 link btn — PS 风格 (多个工具共享大小)
    m_btnSizeLink = new QToolButton(this);
    m_btnSizeLink->setText(QStringLiteral("🔗"));
    m_btnSizeLink->setCheckable(true);
    m_btnSizeLink->setChecked(true);
    m_btnSizeLink->setToolTip(tr("跨工具共享大小"));
    root->addWidget(m_btnSizeLink);

    root->addStretch(1);   // 右吸贴边, 让 QToolBar push bar 到左

    // 7 控件变化 → signal 转发
    connect(m_cmbBlendMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TopOptionBar::blendModeChanged);
    connect(m_spnOpacity, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TopOptionBar::opacityChanged);
    connect(m_spnFlow, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TopOptionBar::flowChanged);
    connect(m_spnSmoothing, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TopOptionBar::smoothingChanged);
    connect(m_spnAngle, qOverload<int>(&QSpinBox::valueChanged),
            this, &TopOptionBar::angleChanged);
    connect(m_spnSize, qOverload<int>(&QSpinBox::valueChanged),
            this, &TopOptionBar::brushSizeChanged);
}

TopOptionBar::~TopOptionBar() = default;

TopOptionBar::BlendMode TopOptionBar::blendMode() const
{
    return static_cast<BlendMode>(m_cmbBlendMode->currentIndex());
}

double TopOptionBar::opacity() const { return m_spnOpacity->value(); }
double TopOptionBar::flow() const { return m_spnFlow->value(); }
double TopOptionBar::smoothing() const { return m_spnSmoothing->value(); }
int    TopOptionBar::angle() const { return m_spnAngle->value(); }
int    TopOptionBar::brushSize() const { return m_spnSize->value(); }

// P7.6 (2026-10-13): setter impls — all wrap QSignalBlocker.
//   Prevents loop ToolContext.setX -> TopOptionBar.setX -> control valueChanged ->
//   TopOptionBar.xxxChanged -> ToolContext.setX -> infinite loop.
//   Caller just sets, no need to blocker.
void TopOptionBar::setBlendMode(BlendMode mode)
{
    setBlendModeIndex(static_cast<int>(mode));
}

void TopOptionBar::setBlendModeIndex(int idx)
{
    if (idx < 0 || idx >= m_cmbBlendMode->count()) return;
    if (m_cmbBlendMode->currentIndex() == idx) return;
    QSignalBlocker block(m_cmbBlendMode);
    m_cmbBlendMode->setCurrentIndex(idx);
}

void TopOptionBar::setOpacity(double pct)
{
    if (m_spnOpacity->value() == pct) return;
    QSignalBlocker block(m_spnOpacity);
    m_spnOpacity->setValue(pct);
}

void TopOptionBar::setFlow(double pct)
{
    if (m_spnFlow->value() == pct) return;
    QSignalBlocker block(m_spnFlow);
    m_spnFlow->setValue(pct);
}

void TopOptionBar::setSmoothing(double pct)
{
    if (m_spnSmoothing->value() == pct) return;
    QSignalBlocker block(m_spnSmoothing);
    m_spnSmoothing->setValue(pct);
}

void TopOptionBar::setAngle(int deg)
{
    if (m_spnAngle->value() == deg) return;
    QSignalBlocker block(m_spnAngle);
    m_spnAngle->setValue(deg);
}

void TopOptionBar::setBrushSize(int px)
{
    if (m_spnSize->value() == px) return;
    QSignalBlocker block(m_spnSize);
    m_spnSize->setValue(px);
}

} // namespace tools
