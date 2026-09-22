// SPDX-License-Identifier: MIT
//
// Lasso implementation - F-L (2026-09-10) + P0-4.5 (2026-09-10) + P2.1 (2026-09-22)
//
#include "Lasso.h"
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
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

Lasso::Lasso(QWidget* /*parent*/) : ToolState()
{
    m_strategy = std::make_unique<selection::LassoSelectionStrategy>();
}

Lasso::~Lasso() = default;

void Lasso::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    LOG_DEBUG("[Lasso] onEnter");
}

void Lasso::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_strategy) m_strategy->cancel();
    LOG_DEBUG("[Lasso] onExit");
}

void Lasso::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->begin(scenePos);
    LOG_DEBUG("[Lasso] onMousePress: scene=({}, {})", scenePos.x(), scenePos.y());
}

void Lasso::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(host);
    if (m_strategy) m_strategy->update(scenePos);
}

void Lasso::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strategy || !host) return;
    QImage mask = m_strategy->end(host->currentImageAsQImage());
    if (mask.isNull()) return;
    auto* sel = host->selectionModel();
    if (!sel) return;
    auto mode = selection::SelectionModel::modifierKeyToMode(e->modifiers());
    sel->setMask(mask, mode);
    LOG_INFO("[Lasso] onMouseRelease: mask={}x{} mode={}",
             mask.width(), mask.height(), static_cast<int>(mode));
}

mediators::ToolId Lasso::id() const { return mediators::ToolId::Lasso; }

QString Lasso::pageTitle() const { return QCoreApplication::translate("tools::Lasso", "Lasso"); }

QCursor Lasso::cursor() const
{
    // F-N (2026-09-10): PS Lasso uses crosshair
    return Qt::CrossCursor;
}

void Lasso::setFeather(int r)
{
    if (m_strategy) m_strategy->setFeather(r);
}

void Lasso::setAntiAlias(bool b)
{
    if (m_strategy) m_strategy->setAntiAlias(b);
}

QWidget* Lasso::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // Feather slider (0..50 px)
    auto* feaRow = new QWidget(page);
    auto* feaLay = new QHBoxLayout(feaRow);
    feaLay->setContentsMargins(0, 0, 0, 0);
    auto* feaLbl = new QLabel(QString::number(0), feaRow);
    m_featherSlider = new QSlider(Qt::Horizontal, feaRow);
    m_featherSlider->setRange(0, 50);
    m_featherSlider->setValue(m_strategy ? m_strategy->feather() : 0);
    m_featherSlider->setTickInterval(10);
    m_featherSlider->setTickPosition(QSlider::TicksBelow);
    feaLay->addWidget(m_featherSlider, 1);
    feaLay->addWidget(feaLbl);
    layout->addRow(QCoreApplication::translate("tools::Lasso", "Feather:"), feaRow);
    // P2.1: 3-arg connect (ToolState is not QObject — see MagicWand.cpp note)
    QObject::connect(m_featherSlider, &QSlider::valueChanged,
                     [this, feaLbl](int v) {
                         setFeather(v);
                         feaLbl->setText(QString::number(v));
                     });

    // Anti-alias checkbox (default on, since feathering wants smooth edges)
    m_antiAlias = new QCheckBox(QCoreApplication::translate("tools::Lasso", "Anti-alias"), page);
    m_antiAlias->setChecked(m_strategy ? m_strategy->antiAlias() : true);
    layout->addRow(QString(), m_antiAlias);
    QObject::connect(m_antiAlias, &QCheckBox::toggled,
                     [this](bool b) { setAntiAlias(b); });

    LOG_DEBUG("[Lasso] optionPage created (P2.1 with feather + anti-alias)");
    return page;
}

} // namespace tools
