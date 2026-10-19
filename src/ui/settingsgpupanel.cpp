#include "settingsgpupanel.h"

#include "../media/gpu/BackendCapabilities.h"
#include "../media/brushes/BrushKind.h"        // BrushKind::HardRound
#include "../media/tools/ToolContext.h"         // BrushSharedState

#include <QButtonGroup>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

// Map RenderMode <-> radio button id for the QButtonGroup lookup.
int modeToId(gpu::RenderMode m) {
    return static_cast<int>(m);
}
gpu::RenderMode idToMode(int id) {
    return static_cast<gpu::RenderMode>(id);
}

}   // namespace

SettingsGpuPanel::SettingsGpuPanel(QWidget *parent)
    : QWidget(parent)
{
    // ---- 渲染模式 group ----
    auto* modeBox = new QGroupBox(QStringLiteral("渲染模式"), this);
    m_autoRadio = new QRadioButton(QStringLiteral("自动 (Auto)\n"
        "由 BackendRegistry 按笔刷种类和共享状态自动选择 CPU / GPU"), modeBox);
    m_gpuRadio  = new QRadioButton(QStringLiteral("强制 GPU\n"
        "所有渲染走 GPU backend (若不可用则降级到 CPU)"), modeBox);
    m_cpuRadio  = new QRadioButton(QStringLiteral("强制 CPU\n"
        "所有渲染走 CPU backend (忽略 GPU 探测结果)"), modeBox);

    m_group = new QButtonGroup(this);
    m_group->addButton(m_autoRadio, modeToId(gpu::RenderMode::Auto));
    m_group->addButton(m_gpuRadio,  modeToId(gpu::RenderMode::ForceGpu));
    m_group->addButton(m_cpuRadio,  modeToId(gpu::RenderMode::ForceCpu));

    auto* modeLayout = new QVBoxLayout(modeBox);
    modeLayout->addWidget(m_autoRadio);
    modeLayout->addWidget(m_gpuRadio);
    modeLayout->addWidget(m_cpuRadio);
    modeLayout->setContentsMargins(12, 8, 12, 12);

    // ---- Apply + Probe row ----
    m_applyBtn = new QPushButton(QStringLiteral("应用"), this);
    m_probeBtn = new QPushButton(QStringLiteral("探测 OpenGL"), this);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_probeBtn);
    btnRow->addStretch(1);

    // ---- Status area ----
    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #333; font-size: 10pt;"));

    m_probeLabel = new QLabel(this);
    m_probeLabel->setTextFormat(Qt::PlainText);
    m_probeLabel->setWordWrap(true);
    m_probeLabel->setStyleSheet(QStringLiteral("color: #555; font-size: 9pt;"));

    // ---- Root layout ----
    auto* title = new QLabel(QStringLiteral("GPU / 渲染后端"), this);
    title->setStyleSheet(QStringLiteral("font-size: 14pt; font-weight: 600; padding-bottom: 4px;"));

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(title);
    root->addWidget(sep);
    root->addWidget(modeBox);
    root->addLayout(btnRow);
    root->addWidget(m_statusLabel);
    root->addWidget(m_probeLabel);
    root->addStretch(1);

    // Wire signals
    connect(m_applyBtn, &QPushButton::clicked, this, &SettingsGpuPanel::onApplyClicked);
    connect(m_probeBtn, &QPushButton::clicked, this, &SettingsGpuPanel::onProbeClicked);

    refreshFromRegistry();
}

SettingsGpuPanel::~SettingsGpuPanel() = default;

gpu::RenderMode SettingsGpuPanel::selectedMode() const
{
    return idToMode(m_group->checkedId());
}

void SettingsGpuPanel::applySelected()
{
    const gpu::RenderMode m = selectedMode();
    gpu::BackendRegistry::setModeStatic(m);
    emit modeApplied(m);
    refreshStatusLabel();
}

void SettingsGpuPanel::onApplyClicked()
{
    applySelected();
}

void SettingsGpuPanel::onProbeClicked()
{
    // Force a fresh probe (does not change the registry's "failed"
    // sticky bit if the probe fails; callers decide what to do with
    // the result via the displayed text).
    const bool available = gpu::BackendCapabilities::probeOpenGL();
    const QStringList summary = gpu::BackendCapabilities::instance().summary();
    QString txt = available
        ? QStringLiteral("OpenGL 探测: 可用\n\n当前 BackendCapabilities:\n%1")
              .arg(summary.join('\n'))
        : QStringLiteral("OpenGL 探测: 不可用 (无 GPU / 驱动 / 离屏 surface)\n\n%1")
              .arg(summary.join('\n'));
    m_probeLabel->setText(txt);
}

QString SettingsGpuPanel::currentModeLabel() const
{
    return gpu::BackendRegistry::modeLabel(gpu::BackendRegistry::modeStatic());
}

QString SettingsGpuPanel::currentBackendName() const
{
    // P8.3 / P8.4.x: a single sample kind/state is enough to show the
    // resolved backend name in the status text. HardRound + default
    // shared state -> whatever Auto mode decides for Basic kinds.
    tools::BrushSharedState ss;
    return gpu::BackendRegistry::instance().currentBackendName(
        brushes::BrushKind::HardRound, ss);
}

bool SettingsGpuPanel::gpuAvailable() const
{
    return gpu::BackendCapabilities::isGpuAvailable();
}

void SettingsGpuPanel::refreshFromRegistry()
{
    const gpu::RenderMode m = gpu::BackendRegistry::modeStatic();
    if (auto* rb = qobject_cast<QRadioButton*>(m_group->button(modeToId(m)))) {
        rb->setChecked(true);
    }
    refreshStatusLabel();
}

void SettingsGpuPanel::refreshStatusLabel()
{
    const QString mode   = currentModeLabel();
    const QString back   = currentBackendName();
    const QString avail  = gpuAvailable()
        ? QStringLiteral("available")
        : QStringLiteral("unavailable");
    m_statusLabel->setText(
        QStringLiteral("当前状态: Mode=%1 | Backend=%2 | GPU=%3")
            .arg(mode, back, avail));
}