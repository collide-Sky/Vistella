// SPDX-License-Identifier: MIT
//
// Text - P0-7.1 (2026-09-14)
//
// PS 风格文字工具 (T):
//   - click 空白: createTextItem + startEditing (主流做法, 跟 PS/Word 一致)
//   - click 已有 text item: 选中 (setSelected2(true)) + 准备拖动
//   - mouse drag: 调 setPosition, mouseRelease push TextItemCommand::Move
//   - optionPage: PS 完整文字工具栏 (字体/字号/颜色/Bold/Italic/对齐 + 提交/取消)
//   - cursor: I-beam
//
// 集成:
//   - ImageWindow eventFilter 已经处理 handle drag (Plan B)
//     m_ctx->onMousePress 转发过来时, handle 已经被 hitTestHandle 拿走
//   - 点击空白 (itemAt = null) 时, imagewindow 已经 unselect 全部 + 退出编辑
//     Text 工具拿到的是"空白 click", 直接 createTextItem + startEditing
//   - 点击已有 text item (itemAt != null), 进入 DragMove 状态
//
// 跟 TextOverlayController 协作:
//   - createTextItem 走 host->createTextItem (imagewindow.cpp 已经实装)
//   - 字体/字号/颜色/Bold/Italic: 写到 m_textCtrl (走 host->textOverlay())
//
#pragma once

#include "ToolState.h"
#include "../graphicstextitem.h"

#include <QCursor>
#include <QPointF>
#include <QPointer>
#include <memory>

class QMouseEvent;
class QKeyEvent;
class QWidget;
class ImageWindow;

namespace tools {

class Text : public ToolState
{
public:
    explicit Text(QWidget* parent = nullptr);
    ~Text() override = default;

    // ===== 生命周期 =====
    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    // ===== 事件 =====
    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;

    // ===== 标识 / UI =====
    mediators::ToolId id() const override { return mediators::ToolId::Text; }
    QString pageTitle() const override { return QStringLiteral("文字"); }
    QCursor cursor() const override { return Qt::IBeamCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

private:
    // 拖动状态机 (P0-7.1: 跟 P0-6 TransformTool 模式一致)
    enum State { Idle, DragMove };
    State m_state = Idle;

    GraphicsTextItem* m_dragItem = nullptr;     // 正在拖的 item (null if Idle)
    QPointF m_pressScenePos;                    // 鼠标起始 scene pos
    QPointF m_pressItemPos;                     // m_dragItem->position() 起始值

    QPointer<ImageWindow> m_host;

    // 辅助: hitTest scenePos 处的 GraphicsTextItem (走 QGraphicsScene::itemAt)
    //   返回 nullptr 表示空白
    GraphicsTextItem* itemAtScenePos(const QPointF& scenePos) const;
};

} // namespace tools
