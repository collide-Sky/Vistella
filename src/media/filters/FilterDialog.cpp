// SPDX-License-Identifier: MIT
//
// FilterDialog implementation - P0-5.9 (2026-09-10), P3.1.1 (2026-09-22)
//
#include "FilterDialog.h"
#include "FilterFactory.h"
#include "../imagewindow.h"
#include "logger.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <cmath>

namespace filter {

namespace {

// Snap to nearest odd in [1, 31]
int snapOdd(int v, int minVal, int maxVal)
{
    if (v < minVal) v = minVal;
    if (v > maxVal) v = maxVal;
    if (v % 2 == 0) {
        if (v + 1 <= maxVal) return v + 1;
        return v - 1;
    }
    return v;
}

// Slider->spinbox sync helper. range [lo, hi], initial v.
QSlider* makeIntSlider(int lo, int hi, int v, QWidget* parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(lo, hi);
    s->setValue(v);
    s->setTickPosition(QSlider::TicksBelow);
    s->setTickInterval(qMax(1, (hi - lo) / 5));
    return s;
}

QSpinBox* makeIntSpin(int lo, int hi, int v, QWidget* parent)
{
    auto *sp = new QSpinBox(parent);
    sp->setRange(lo, hi);
    sp->setValue(v);
    return sp;
}

QSlider* makeDoubleSlider(double lo, double hi, double v, QWidget* parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    // Map to int range [0, 1000]
    constexpr int SLIDER_MAX = 1000;
    s->setRange(0, SLIDER_MAX);
    const double ratio = (v - lo) / (hi - lo);
    int iv = static_cast<int>(std::round(ratio * SLIDER_MAX));
    if (iv < 0) iv = 0;
    if (iv > SLIDER_MAX) iv = SLIDER_MAX;
    s->setValue(iv);
    s->setTickPosition(QSlider::TicksBelow);
    s->setTickInterval(SLIDER_MAX / 5);
    // Stash lo/hi in dynamic props for slot handler
    s->setProperty("dblLo", lo);
    s->setProperty("dblHi", hi);
    return s;
}

QDoubleSpinBox* makeDoubleSpin(double lo, double hi, double v, int decimals, QWidget* parent)
{
    auto *sp = new QDoubleSpinBox(parent);
    sp->setRange(lo, hi);
    sp->setDecimals(decimals);
    sp->setSingleStep(decimals >= 2 ? 0.05 : (decimals == 1 ? 0.1 : 1.0));
    sp->setValue(v);
    return sp;
}

}  // namespace

FilterDialog::FilterDialog(ImageWindow *host, FilterKind kind, QWidget *parent)
    : QDialog(parent), m_host(host), m_kind(kind)
{
    setWindowTitle(QString::fromUtf8(filterName(kind)));
    setMinimumWidth(360);

    m_strategy = FilterFactory::createFilter(kind);
    if (!m_strategy) {
        LOG_ERROR("[FilterDialog] createFilter returned null for kind={}",
                   static_cast<int>(kind));
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 滤镜名 (大字体)
    m_nameLabel = new QLabel(QString::fromUtf8(filterName(kind)), this);
    QFont bigFont = m_nameLabel->font();
    bigFont.setPointSize(bigFont.pointSize() + 2);
    bigFont.setBold(true);
    m_nameLabel->setFont(bigFont);
    layout->addWidget(m_nameLabel);

    // 参数 widgets (slider/spinbox/picker, 按 strategy 类型)
    buildParamWidgets();

    // 当前参数摘要
    m_paramLabel = new QLabel(this);
    m_paramLabel->setStyleSheet("color: gray;");
    refreshParamText();
    layout->addWidget(m_paramLabel);

    // PS 风格 (2026-09-10): 状态提示
    auto *hint = new QLabel(
        QStringLiteral("Apply: 实时预览\nOK: 应用 + 入撤销栈\nCancel: 取消"), this);
    hint->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(hint);

    // spacer
    layout->addStretch(1);

    // 按钮行 (PS 风格: Apply 在左, OK/Cancel 在右)
    auto *btnRow = new QHBoxLayout;
    m_applyBtn = new QPushButton(tr("Apply 预览"), this);
    connect(m_applyBtn, &QPushButton::clicked, this, &FilterDialog::onApplyClicked);
    btnRow->addWidget(m_applyBtn);

    btnRow->addStretch(1);
    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setDefault(true);
    connect(m_okBtn, &QPushButton::clicked, this, &FilterDialog::onOkClicked);
    btnRow->addWidget(m_okBtn);

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FilterDialog::onCancelClicked);
    btnRow->addWidget(m_cancelBtn);

    layout->addLayout(btnRow);

    LOG_DEBUG("[FilterDialog] created kind={} ({})", static_cast<int>(kind),
              filterName(kind));
}

FilterDialog::~FilterDialog() = default;

void FilterDialog::buildParamWidgets()
{
    if (!m_strategy) return;

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);

    int row = 0;
    auto addIntRow = [&](const QString& label, int& field,
                         int lo, int hi, bool odd) {
        int v = odd ? snapOdd(field, lo, hi) : field;
        auto *lab = new QLabel(label, this);
        auto *slider = makeIntSlider(lo, hi, v, this);
        auto *spin = makeIntSpin(lo, hi, v, this);
        // Stash pointers back to slider/spin for 2-way sync
        slider->setProperty("spinPtr", QVariant::fromValue<void*>(spin));
        spin->setProperty("sliderPtr", QVariant::fromValue<void*>(slider));
        spin->setProperty("oddFlag", odd);
        // Stash pointer to the int field
        spin->setProperty("intField", QVariant::fromValue<void*>(&field));

        connect(slider, &QSlider::valueChanged, this, [this, spin](int sv) {
            int lo2 = spin->minimum();
            int hi2 = spin->maximum();
            bool odd = spin->property("oddFlag").toBool();
            int nv = odd ? snapOdd(sv, lo2, hi2) : sv;
            spin->setValue(nv);
        });
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, slider](int nv) {
                    int* fp = static_cast<int*>(slider->property("intField").value<void*>());
                    if (fp) *fp = nv;
                    slider->setValue(nv);
                    refreshParamText();
                });

        grid->addWidget(lab, row, 1);
        grid->addWidget(slider, row, 2);
        grid->addWidget(spin, row, 3);
        ++row;
    };

    auto addDoubleRow = [&](const QString& label, double& field,
                            double lo, double hi, int decimals) {
        auto *lab = new QLabel(label, this);
        auto *slider = makeDoubleSlider(lo, hi, field, this);
        auto *spin = makeDoubleSpin(lo, hi, field, decimals, this);
        // Stash pointers for 2-way sync
        slider->setProperty("spinPtr", QVariant::fromValue<void*>(spin));
        spin->setProperty("sliderPtr", QVariant::fromValue<void*>(slider));
        spin->setProperty("dblField", QVariant::fromValue<void*>(&field));
        // Stash dblLo/dblHi on slider for slot computation
        slider->setProperty("dblLo", lo);
        slider->setProperty("dblHi", hi);

        connect(slider, &QSlider::valueChanged, this, [this, spin, lo, hi](int sv) {
            constexpr int SLIDER_MAX = 1000;
            double ratio = static_cast<double>(sv) / SLIDER_MAX;
            double dv = lo + ratio * (hi - lo);
            spin->setValue(dv);
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, slider, lo, hi](double dv) {
                    double* fp = static_cast<double*>(slider->property("dblField").value<void*>());
                    if (fp) *fp = dv;
                    constexpr int SLIDER_MAX = 1000;
                    double ratio = (hi > lo) ? (dv - lo) / (hi - lo) : 0.0;
                    int iv = static_cast<int>(std::round(ratio * SLIDER_MAX));
                    if (iv < 0) iv = 0;
                    if (iv > SLIDER_MAX) iv = SLIDER_MAX;
                    slider->setValue(iv);
                    refreshParamText();
                });

        grid->addWidget(lab, row, 1);
        grid->addWidget(slider, row, 2);
        grid->addWidget(spin, row, 3);
        ++row;
    };

    switch (m_kind) {
    case FilterKind::GaussianBlur: {
        auto *s = static_cast<GaussianBlurFilter*>(m_strategy.get());
        addIntRow(QStringLiteral("ksize"), s->ksize, 1, 31, /*odd*/true);
        addDoubleRow(QStringLiteral("sigma"), s->sigma, 0.1, 5.0, 2);
        break;
    }
    case FilterKind::BoxBlur: {
        auto *s = static_cast<BoxBlurFilter*>(m_strategy.get());
        addIntRow(QStringLiteral("ksize"), s->ksize, 1, 31, /*odd*/false);
        break;
    }
    case FilterKind::MedianBlur: {
        auto *s = static_cast<MedianBlurFilter*>(m_strategy.get());
        addIntRow(QStringLiteral("ksize"), s->ksize, 1, 31, /*odd*/true);
        break;
    }
    case FilterKind::BilateralBlur: {
        auto *s = static_cast<BilateralBlurFilter*>(m_strategy.get());
        addIntRow(QStringLiteral("d"), s->d, 1, 50, /*odd*/false);
        addDoubleRow(QStringLiteral("sigmaColor"), s->sigmaColor, 10.0, 150.0, 1);
        addDoubleRow(QStringLiteral("sigmaSpace"), s->sigmaSpace, 10.0, 150.0, 1);
        break;
    }
    case FilterKind::UnsharpMask: {
        auto *s = static_cast<UnsharpMaskFilter*>(m_strategy.get());
        addDoubleRow(QStringLiteral("amount"), s->amount, 0.5, 3.0, 2);
        addDoubleRow(QStringLiteral("radius"), s->radius, 0.5, 10.0, 2);
        addIntRow(QStringLiteral("threshold"), s->threshold, 0, 255, /*odd*/false);
        break;
    }
    case FilterKind::Threshold: {
        auto *s = static_cast<ThresholdFilter*>(m_strategy.get());
        addDoubleRow(QStringLiteral("level"), s->level, 0.0, 255.0, 0);
        break;
    }
    case FilterKind::Posterize: {
        auto *s = static_cast<PosterizeFilter*>(m_strategy.get());
        addIntRow(QStringLiteral("levels"), s->levels, 2, 32, /*odd*/false);
        break;
    }
    case FilterKind::PhotoFilter: {
        auto *s = static_cast<PhotoFilterFilter*>(m_strategy.get());
        addIntRow(QStringLiteral("cyan-red"), s->cyanRed, -100, 100, /*odd*/false);
        addIntRow(QStringLiteral("magenta-green"), s->magentaGreen, -100, 100, /*odd*/false);
        addIntRow(QStringLiteral("yellow-blue"), s->yellowBlue, -100, 100, /*odd*/false);
        addIntRow(QStringLiteral("density"), s->density, 0, 100, /*odd*/false);
        break;
    }
    case FilterKind::HighPass: {
        auto *s = static_cast<HighPassFilter*>(m_strategy.get());
        addDoubleRow(QStringLiteral("radius"), s->radius, 0.5, 20.0, 2);
        break;
    }
    case FilterKind::Solarize: {
        auto *s = static_cast<SolarizeFilter*>(m_strategy.get());
        addDoubleRow(QStringLiteral("threshold"), s->threshold, 0.0, 255.0, 0);
        break;
    }
    case FilterKind::GradientMap:
    case FilterKind::Sharpen:
    case FilterKind::SharpenMore:
    case FilterKind::Emboss:
    case FilterKind::FindEdges:
    case FilterKind::GlowingEdges:
    case FilterKind::Desaturate:
    case FilterKind::Invert:
    case FilterKind::BlurMore:
    case FilterKind::FilterGallery:
    default:
        // No editable fields — strategy is no-param or hasParam=true but
        // no public fields exposed (e.g. GradientMap placeholder).
        // Show a placeholder label in the grid; paramText below carries
        // the strategy's own descriptive string (P1 实装, 默认黑白, etc.).
        {
            auto *lab = new QLabel(m_strategy->hasParam()
                ? QStringLiteral("(固定参数)")
                : QStringLiteral("(无参数)"), this);
            lab->setStyleSheet("color: gray;");
            grid->addWidget(lab, row, 1, 1, 3);
        }
        break;
    }

    // 嵌入 grid 进主 layout.  ctor 顺序:
    //   addWidget(nameLabel) -> buildParamWidgets (addLayout grid here)
    //   -> addWidget(m_paramLabel) -> ... -> addLayout(btnRow)
    // 用 this->layout() 拿到 ctor 创建的 QVBoxLayout, 把 grid 插在 nameLabel 之后
    auto *mainLayout = qobject_cast<QVBoxLayout*>(this->layout());
    if (mainLayout && grid->count() > 0) {
        mainLayout->addLayout(grid);
    }
}

void FilterDialog::refreshParamText()
{
    if (!m_paramLabel || !m_strategy) return;
    const QString txt = m_strategy->paramText();
    if (m_strategy->hasParam()) {
        m_paramLabel->setText(QStringLiteral("参数: %1").arg(txt));
    } else {
        m_paramLabel->setText(QStringLiteral("参数: (无)"));
    }
}

void FilterDialog::onApplyClicked()
{
    LOG_INFO("[FilterDialog] Apply: kind={}", static_cast<int>(m_kind));
    emit applyRequested();
    if (m_host && m_strategy) {
        // P3.1.3 (2026-09-22): realtime preview through ImageWindow
        //   Apply 不动 m_current, 只通过 host->previewFilter(strategy) 触发
        //   临时预览层. OK/Cancel 各自负责 clearPreview.
        m_host->previewFilter(m_strategy.get());
        m_host->statusBar()->showMessage(
            QStringLiteral("Apply: %1 (%2)").arg(QString::fromUtf8(filterName(m_kind)),
                                                 m_strategy->paramText()),
            3000);
    }
}

void FilterDialog::onOkClicked()
{
    LOG_INFO("[FilterDialog] OK: kind={}", static_cast<int>(m_kind));
    // P3.1.3 (2026-09-22): clear preview first, then push FilterCommand via host
    //   host->applyFilterWithStrategy() 内部用 dialog 提供的 strategy
    //   (含用户在 slider/picker 上调好的当前参数) 走 FilterCommand 入撤销栈
    if (m_host && m_strategy) {
        m_host->clearPreview();
        m_host->applyFilterWithStrategy(m_strategy.get(), m_strategy->name());
    }
    emit okRequested();
    accept();
}

void FilterDialog::onCancelClicked()
{
    LOG_INFO("[FilterDialog] Cancel: kind={}", static_cast<int>(m_kind));
    if (m_host) {
        m_host->clearPreview();
    }
    reject();
}

} // namespace filter