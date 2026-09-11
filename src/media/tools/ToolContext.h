// SPDX-License-Identifier: MIT
//
// ToolContext - F-C (2026-09-09)
//
// F-C stage only: holds current ToolState + forwards events to it.
// Mediator integration (signal-driven setState) is F-D stage.
//
#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

class QMouseEvent;
class QKeyEvent;
class QPointF;
class QWidget;
class ImageWindow;

#include "../mediators/ToolMediator.h"

namespace tools {

class ToolState;

class ToolContext : public QObject
{
    Q_OBJECT
public:
    explicit ToolContext(QObject* parent = nullptr);
    ~ToolContext() override;

    // Wiring (called after ImageWindow is constructed)
    void attach(ImageWindow* host, mediators::ToolMediator* toolMed);
    void detach();

    // Current tool
    mediators::ToolId currentToolId() const;
    ToolState* currentState() const { return m_current.get(); }

    // Switch tool (main entry, takes ownership via std::unique_ptr)
    void setState(std::unique_ptr<ToolState> state);

    // Event forwarding (ImageWindow eventFilter calls these)
    //   host is forwarded to ToolState, lets tool access image/layer/undo
    void onMousePress(QMouseEvent* e, const QPointF& scenePos);
    void onMouseMove(QMouseEvent* e, const QPointF& scenePos);
    void onMouseRelease(QMouseEvent* e, const QPointF& scenePos);
    void onKeyPress(QKeyEvent* e);
    void onKeyRelease(QKeyEvent* e);

signals:
    // Subscribers listen:
    //   ImageCanvas: change cursor
    //   ImageOptionBar: switch stack page
    void toolChanged(mediators::ToolId newId);
    // Current tool's cursor changed (e.g. internal state change)
    void cursorChanged(const QCursor& cursor);

private:
    // Do NOT own host / toolMed (weak ref only)
    QPointer<ImageWindow>           m_host;
    mediators::ToolMediator*         m_toolMed = nullptr;  // weak ref (F-D wires Mediator listener)

    // Own current tool
    std::unique_ptr<ToolState>       m_current;

    // Initial default tool (None state, no event response)
    void installDefaultState();
};

} // namespace tools
