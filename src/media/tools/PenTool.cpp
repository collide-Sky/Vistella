// SPDX-License-Identifier: MIT
//
// PenTool implementation - P0-9.2 (2026-09-15)
//
// 详见 PenTool.h 头注释
//
#include "PenTool.h"
#include "../imagewindow.h"
#include "../imageworker/layers/LayerStack.h"
#include "../imageworker/layers/LayerCommand.h"
#include "logger.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

PenTool::PenTool(QWidget* /*parent*/) : ToolState() {}

void PenTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_anchors.clear();
    LOG_DEBUG("[PenTool] onEnter host={}", host ? "yes" : "null");
}

void PenTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_anchors.clear();
    m_host = nullptr;
    LOG_DEBUG("[PenTool] onExit");
}

void PenTool::commitPath(ImageWindow* host)
{
    if (!host || m_anchors.size() < 2) {
        m_anchors.clear();
        return;
    }
    QPainterPath path;
    path.moveTo(m_anchors.first());
    for (int i = 1; i < m_anchors.size(); ++i) {
        path.lineTo(m_anchors[i]);
    }
    // PS 同款: 双击/Enter 闭合 (但 PenTool 第一次闭合不算 closed, 留 path)
    //   P0-9.2 简化: 不 close, 走 open polyline. user 后续 Edit Path 可 close.

    layers::Layer vecLayer;
    vecLayer.kind = layers::Layer::Vector;
    vecLayer.name = QStringLiteral("路径 (vector)");
    vecLayer.vectorPaths.append(path);
    vecLayer.vectorFillColors.append(QColor(255, 255, 255, 0));   // 透明 fill
    vecLayer.vectorStrokeWidths.append(2.0);
    vecLayer.vectorStrokeColors.append(QColor(0, 120, 215));     // 蓝色 stroke

    if (auto* stack = host->layerStack()) {
        stack->addLayer(vecLayer);
        if (auto* undoStack = host->undoStack()) {
            undoStack->push(new layers::LayerCommand(stack,
                                                    layers::LayerCommand::Add,
                                                    vecLayer));
        }
        LOG_INFO("[PenTool] committed path (anchors={})", m_anchors.size());
    }
    m_anchors.clear();
}

void PenTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host || e->button() != Qt::LeftButton) return;

    if (m_mode == Mode::RemoveAnchor && !m_anchors.isEmpty()) {
        // 删除最近 anchor (PS 同款: Alt + 单击)
        m_anchors.removeLast();
        LOG_DEBUG("[PenTool] removed last anchor, remaining={}", m_anchors.size());
        return;
    }
    // AddAnchor: 加 anchor
    m_anchors.append(scenePos);
    LOG_DEBUG("[PenTool] anchor added at ({}, {}), total={}",
              scenePos.x(), scenePos.y(), m_anchors.size());
}

void PenTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& /*scenePos*/)
{
    // Pen tool 不响应 mouseMove (anchor 是 click-driven, 不是 drag-driven)
}

void PenTool::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& /*scenePos*/) {}

void PenTool::onKeyPress(QKeyEvent* e, ImageWindow* host)
{
    if (!m_host || !host) return;

    if (e->key() == Qt::Key_Escape) {
        // Esc: 取消 collecting
        m_anchors.clear();
        LOG_DEBUG("[PenTool] Escape: cleared anchors");
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        // Enter: 提交 path (open polyline, 不 close)
        commitPath(host);
    }
}

void PenTool::onKeyRelease(QKeyEvent* /*e*/, ImageWindow* /*host*/) {}

QWidget* PenTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    auto* modeLabel = new QLabel(QStringLiteral("模式:"), page);
    layout->addWidget(modeLabel);

    auto* modeCombo = new QComboBox(page);
    modeCombo->addItem(QStringLiteral("加锚点"),    static_cast<int>(Mode::AddAnchor));
    modeCombo->addItem(QStringLiteral("删锚点"),    static_cast<int>(Mode::RemoveAnchor));
    modeCombo->setCurrentIndex(static_cast<int>(m_mode));
    layout->addWidget(modeCombo);

    auto* hint = new QLabel(QStringLiteral("(Enter 提交 / Esc 取消 / / 双击闭合)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addWidget(hint);

    auto* commitBtn = new QPushButton(QStringLiteral("完成"), page);
    layout->addWidget(commitBtn);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), page);
    layout->addWidget(cancelBtn);

    layout->addStretch(1);

    QObject::connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     page, [this, modeCombo](int idx) {
        m_mode = static_cast<Mode>(modeCombo->itemData(idx).toInt());
    });
    QObject::connect(commitBtn, &QPushButton::clicked,
                     page, [this]() {
        if (m_host) commitPath(m_host);
    });
    QObject::connect(cancelBtn, &QPushButton::clicked,
                     page, [this]() {
        m_anchors.clear();
    });

    LOG_DEBUG("[PenTool] optionPage created");
    return page;
}

} // namespace tools