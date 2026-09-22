// SPDX-License-Identifier: MIT
//
// MagicWand implementation - F-L (2026-09-10) + P0-4.5 (2026-09-10) + P2.1 (2026-09-22)
//
#include "MagicWand.h"
#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "../selection/SelectionStrategy.h"
#include "logger.h"

#include <QCoreApplication>

#include <QCheckBox>
#include <QCursor>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

MagicWand::MagicWand(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::MagicWandSelectionStrategy>();
}

MagicWand::~MagicWand() = default;

void MagicWand::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[MagicWand] onEnter");
}

void MagicWand::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    LOG_DEBUG("[MagicWand] onExit");
}

void MagicWand::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->begin(scenePos);
    LOG_DEBUG("[MagicWand] onMousePress: scene=({}, {})", scenePos.x(), scenePos.y());
}

void MagicWand::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->update(scenePos);
}

void MagicWand::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strategy || !host) return;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return;
    auto* sel = host->selectionModel();
    if (!sel) return;
    auto mode = selection::SelectionModel::modifierKeyToMode(e->modifiers());
    sel->setMask(mask, mode);
    LOG_INFO("[MagicWand] onMouseRelease: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
}

mediators::ToolId MagicWand::id() const { return mediators::ToolId::MagicWand; }

QString MagicWand::pageTitle() const { return QCoreApplication::translate("tools::MagicWand", "MagicWand"); }

QCursor MagicWand::cursor() const
{
    // F-N (2026-09-10): PS MagicWand uses crosshair
    return Qt::CrossCursor;
}

void MagicWand::setTolerance(int t)
{
    if (m_strategy) m_strategy->setTolerance(t);
}

void MagicWand::setContiguous(bool c)
{
    if (m_strategy) m_strategy->setContiguous(c);
}

QWidget* MagicWand::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // Tolerance slider + value label
    auto* tolRow  = new QWidget(page);
    auto* tolLay  = new QHBoxLayout(tolRow);
    tolLay->setContentsMargins(0, 0, 0, 0);
    auto* tolLbl  = new QLabel(QString::number(32), tolRow);
    m_tolSlider   = new QSlider(Qt::Horizontal, tolRow);
    m_tolSlider->setRange(0, 255);
    m_tolSlider->setValue(m_strategy ? m_strategy->tolerance() : 32);
    m_tolSlider->setTickInterval(32);
    m_tolSlider->setTickPosition(QSlider::TicksBelow);
    tolLay->addWidget(m_tolSlider, 1);
    tolLay->addWidget(tolLbl);
    layout->addRow(QCoreApplication::translate("tools::MagicWand", "Tolerance:"), tolRow);
    // P2.1: 3-arg connect (no context object) — ToolState is not a QObject,
    //   so we cannot use the 4-arg form. Lambda captures `this` directly;
    //   lifetime is tied to m_strategy (parented to this->m_strategy? No —
    //   parented to `page` QWidget, owned by ImageOptionBar). When the
    //   optionPage is destroyed, the slider/checkbox go with it and Qt
    //   auto-disconnects. Acceptable for tool option pages that are
    //   rebuilt on tool change.
    QObject::connect(m_tolSlider, &QSlider::valueChanged,
                     [this, tolLbl](int v) {
                         setTolerance(v);
                         tolLbl->setText(QString::number(v));
                     });

    // Contiguous checkbox
    m_contiguous  = new QCheckBox(QCoreApplication::translate("tools::MagicWand", "Contiguous"), page);
    m_contiguous->setChecked(m_strategy ? m_strategy->contiguous() : true);
    layout->addRow(QString(), m_contiguous);
    QObject::connect(m_contiguous, &QCheckBox::toggled,
                     [this](bool b) { setContiguous(b); });

    // Sample All Layers (P1 reserved; visible but disabled so users know we plan it)
    m_sampleAll   = new QSpinBox(page);
    m_sampleAll->setRange(1, 8);
    m_sampleAll->setValue(1);
    m_sampleAll->setEnabled(false);
    m_sampleAll->setToolTip(QCoreApplication::translate("tools::MagicWand",
        "P2.x reserved: Sample All Layers (cross-layer pixel sampling)."));
    layout->addRow(QCoreApplication::translate("tools::MagicWand", "Sample:"), m_sampleAll);

    LOG_DEBUG("[MagicWand] optionPage created (P2.1 with tolerance + contiguous)");
    return page;
}

} // namespace tools
