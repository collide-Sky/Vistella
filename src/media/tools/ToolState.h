// SPDX-License-Identifier: MIT
//
// ToolState - F-C (2026-09-09)
//
// State 模式抽象类 — 每个基础工具一个 ToolState 子类
//   onEnter: 切到此工具时调 (装 eventFilter / 改 cursor / 通知 Mediator)
//   onExit:  离开此工具时调 (卸 eventFilter / 恢复 cursor)
//   onXxx:   事件转发 (从 ImageWindow eventFilter 转过来)
//
// 设计原则 (一次性到位):
//   1) 抽象接口跟 Qt 事件系统解耦 — 工具只关心"做什么", 不关心"事件从哪来"
//   2) 每个 ToolState 子类只持 config 快照 (QVariantMap), 不持 ImageWindow
//      真正的 host 通过 onEnter / onExit 临时传入, 避免循环引用
//   3) 不持 Mediator 引用 — 状态变化通过 ToolContext 转发, ToolContext 通知 Mediator
//   4) 强约束: 不动 setMode/toggleMode/m_normalSize (主窗口状态机)
//
// 用法:
//   ToolContext ctx(host, &toolMed);
//   ctx.setState(new MoveTool());   // 切到 Move 工具
//   ctx.setState(new EyedropperTool());  // 切到吸管
//
#pragma once

#include <QObject>
#include <QCursor>
#include <QPointF>
#include <QString>
#include "../mediators/ToolMediator.h"

class QMouseEvent;
class QKeyEvent;
class ImageWindow;

namespace tools {

class ToolState
{
public:
    virtual ~ToolState() = default;

    // ===== 生命周期 =====
    // 切到此工具时调, host 提供当前 ImageWindow
    virtual void onEnter(ImageWindow* host) = 0;
    // 离开此工具时调 (切到其他 tool 前)
    virtual void onExit(ImageWindow* host)  = 0;

    // ===== 事件转发 (ImageWindow eventFilter → ToolContext → 当前 ToolState) =====
    // 默认 no-op, 子类按需 override
    //   host 参数: 让子类能访问 ImageWindow 的 image / layer / undo 等
    //              (F-N 阶段 eventFilter 实装后, 鼠标事件会带 host 进来)
    virtual void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)   { Q_UNUSED(e); Q_UNUSED(host); Q_UNUSED(scenePos); }
    virtual void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)    { Q_UNUSED(e); Q_UNUSED(host); Q_UNUSED(scenePos); }
    virtual void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) { Q_UNUSED(e); Q_UNUSED(host); Q_UNUSED(scenePos); }
    virtual void onKeyPress(QKeyEvent* e, ImageWindow* host) { Q_UNUSED(e); Q_UNUSED(host); }
    virtual void onKeyRelease(QKeyEvent* e, ImageWindow* host) { Q_UNUSED(e); Q_UNUSED(host); }

    // ===== 标识 / UI =====
    // 工具 ID (跟 ToolMediator::ToolId 一一对应)
    //   必须用 mediators::ToolId 显式限定, 不会跟 tools 命名空间的 ToolId 冲突
    virtual mediators::ToolId id() const = 0;
    // 工具在 ImageOptionBar 的 page 标题 (默认 = 工具名)
    virtual QString pageTitle() const = 0;
    // 鼠标在画布上时的 cursor
    virtual QCursor cursor() const = 0;
    // 工具的二级工具栏 page (后续 F-E 阶段接 QStackedWidget)
    //   F-C 阶段返 nullptr, 留 F-E 阶段实装
    virtual QWidget* optionPage(QWidget* parent = nullptr) { Q_UNUSED(parent); return nullptr; }
};

} // namespace tools

Q_DECLARE_METATYPE(tools::ToolState*)
