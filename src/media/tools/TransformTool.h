// SPDX-License-Identifier: MIT
//
// TransformTool - P0-6.2 (2026-09-14)
//
// PS 风格自由变换工具 (Ctrl+T / 8 handle / 4 mode):
//   - 跟 ToolContext 集成 (setState 时 onEnter, 切走时 onExit)
//   - onMousePress: TransformBox::hitTest 找 handle, 记录 active handle
//   - onMouseMove: TransformBox::dragHandle 更新 box
//   - onMouseRelease: 生成 TransformCommand 推 undoStack (m_w->pushUndoCommand)
//   - cursor: 按 active handle 返回 8 方向 resize cursor / move / crosshair
//   - optionPage: ImageOptionBar 4 mode combo (Scale/Rotate/Skew/Distort) + Shift = 等比
//
// 集成:
//   - ImageWindow: m_ctx->setState(std::make_unique<TransformTool>())
//   - m_canvas: 渲染 box + 8 handle (TransformBox::handlePos)
//   - m_selection: setRect(m_selection->boundingRect()) 初始化 box
//   - m_undoStack: push(TransformCommand(box 之前/之后))
//
#pragma once

#include "ToolState.h"
#include "../transform/TransformBox.h"

#include <QCursor>
#include <QPointF>
#include <QPointer>
#include <functional>
#include <memory>

class QMouseEvent;
class QKeyEvent;
class QWidget;
class ImageWindow;

namespace tools {

// P0-6.12 (2026-09-14): rotation 同步用 callback (ToolState 抽象类无 Q_OBJECT, 不能 emit signals)
//   父类 ToolState 保持不变, callback 模式让 ImageWindow 注入 propsDock setter
class TransformTool : public ToolState
{
public:
    TransformTool();
    ~TransformTool() override = default;

    // ===== 生命周期 =====
    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    // ===== 事件 =====
    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;
    void onKeyRelease(QKeyEvent* e, ImageWindow* host) override;

    // ===== 标识 / UI =====
    mediators::ToolId id() const override { return mediators::ToolId::Transform; }
    QString pageTitle() const override { return QStringLiteral("变换"); }
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== 4 mode 切换 (ImageOptionBar combo 调) =====
    void setMode(transform::TransformBox::Mode m);
    transform::TransformBox::Mode mode() const { return m_mode; }

    // Shift = 等比 / 约束 (Scale / Skew 用)
    bool isShiftHeld() const { return m_shiftHeld; }

    // 给 ImageCanvas 渲染 (TransformBox 重设 mode 后 emit)
    transform::TransformBox* box() { return m_box.get(); }
    const transform::TransformBox* box() const { return m_box.get(); }

    // 撤销栈用 — 拖动结束时 ImageWindow 抓取
    bool hasPendingTransform() const { return m_pending; }

    // P0-6.12 (2026-09-14): rotation 同步 callback (ToolState 无 Q_OBJECT, 不能 emit)
    //   ImageWindow::onFreeTransform 注入: [this](qreal deg) { propsDock->setRotation(deg); }
    using RotationCallback = std::function<void(qreal)>;
    void setRotationCallback(RotationCallback cb) { m_rotationCb = std::move(cb); }

private:
    std::unique_ptr<transform::TransformBox> m_box;

    // 拖动状态
    transform::TransformBox::Handle m_activeHandle = transform::TransformBox::Handle::None;
    QPointF                          m_lastScenePos;
    bool                             m_pending = false;
    bool                             m_shiftHeld = false;
    transform::TransformBox::Mode    m_mode = transform::TransformBox::Mode::Scale;

    // ImageWindow 弱引用 (不持所有权, host 销毁时工具也被销毁)
    QPointer<ImageWindow> m_host;
    RotationCallback       m_rotationCb;     // P0-6.12: rotation 同步 callback (ToolState 无 Q_OBJECT)

    // 生成 TransformCommand 并 push (P0-6.4 实装, P0-6.2 留 stub 调 host 接口)
    void commitTransform(ImageWindow* host);
};

}  // namespace tools
