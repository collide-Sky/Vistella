// VDragManager.h
//
// Process-wide registry + drag coordinator for VDraggableTabWidget.
//
// Why a manager at all (rather than QDrag + QMimeData only):
//   - A drag is a *cross-widget* event: the source VDraggableTabWidget starts
//     the drag, the target may be a different VDraggableTabWidget instance
//     (cross-dock merge in PS-style). We need a single place that knows
//     "which VDraggableTabWidget instances are alive right now" so the drop
//     side can resolve a global cursor position back to a target tab widget.
//   - The manager also owns the single active VDropIndicator and tells
//     source/target which side owns it (avoiding two indicators fighting
//     over the same cursor).
//
// We still use QDrag + QMimeData for the actual OS drag protocol (works with
// native cursor + cancel-on-Esc). The mime data carries a pointer to the
// source page; the manager just provides the registry and indicator owner.
//
#pragma once

#include <QObject>
#include <QPoint>
#include <QList>
#include <QPointer>

class QWidget;
class QDragMoveEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;

namespace vistella::ui {

class VDraggableTabWidget;
class VDropIndicator;

class VDragManager : public QObject {
    Q_OBJECT
public:
    static VDragManager& instance();

    // Live tab widget registry. VDraggableTabWidget ctor/dtor call this.
    void registerTabWidget(VDraggableTabWidget* w);
    void unregisterTabWidget(VDraggableTabWidget* w);

    // Hit-test: which registered tab widget is under the given global pos?
    // Returns nullptr if no tab widget is hit (drop would create a floating
    // dock or be cancelled).
    VDraggableTabWidget* tabWidgetAt(const QPoint& globalPos) const;

    // Drop indicator ownership. Only one indicator is "live" at a time.
    // VDraggableTabWidget grabs it on drag enter, releases on drag leave/drop.
    VDropIndicator* acquireIndicator(VDraggableTabWidget* owner);
    void            releaseIndicator(VDraggableTabWidget* owner);
    VDropIndicator* activeIndicator() const;
    VDraggableTabWidget* indicatorOwner() const;

    // Optional: tunable threshold (defaults to QApplication::startDragDistance() * 2).
    int  dragStartThreshold() const;
    // TODO(P8.9): setDragStartThreshold() has no caller, so the drag threshold is not configurable -- see docs/DEBT.md#A-DRAGTHRESHOLD
    void setDragStartThreshold(int px);

    // Cross-widget drag handshake. The *source* VDraggableTabWidget calls
    // beginDrag() right before QDrag::exec(); the *target* reads
    // activeDragSource()/activeDragPage() from dropEvent and calls back
    // into source->movePageTo(). endDrag() clears the bookkeeping.
    void               beginDrag(VDraggableTabWidget* source, QWidget* page);
    VDraggableTabWidget* activeDragSource() const;
    QWidget*           activeDragPage()    const;
    void               endDrag();

signals:
    // Observers (status bar, test harness) can listen for drag start/end.
    void dragStarted(VDraggableTabWidget* source, QWidget* page);
    void dragEnded(VDraggableTabWidget* source, QWidget* page, bool accepted);

private:
    explicit VDragManager(QObject* parent = nullptr);
    QList<VDraggableTabWidget*> m_widgets;
    QPointer<VDropIndicator> m_indicator;
    QPointer<VDraggableTabWidget> m_indicatorOwner;
    QPointer<VDraggableTabWidget> m_activeSource;
    QPointer<QWidget>            m_activePage;
    int m_dragThreshold = -1;  // -1 means "use QApplication default * 2"
};

} // namespace vistella::ui
