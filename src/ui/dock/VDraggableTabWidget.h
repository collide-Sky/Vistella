// VDraggableTabWidget.h
//
// PS-style draggable tab container.
//
// Inherits QWidget (not QTabWidget) because we need full control over the
// tab bar's mouse handling, the drop target routing, and the cross-widget
// drag protocol. Using QTabWidget as base would inherit its setMovable()
// and internal swap semantics that fight the cross-widget move we need.
//
// What this class does:
//   - Holds an ordered list of pages (QWidget*). Each page is either
//     "docked" (parented to the internal QStackedWidget) or "floating"
//     (parented to an internal QDockWidget wrapper that the user can move
//     anywhere on the desktop).
//   - Mouse press on a tab + drag past threshold starts a QDrag with a
//     mime payload pointing at the source page. Cross-widget drops are
//     resolved via VDragManager (registry of live VDraggableTabWidget).
//   - Drop on another VDraggableTabWidget's tab bar inserts the page at
//     the cursor position. Drop on empty area creates a floating dock
//     owned by the source VDraggableTabWidget.
//
// What this class does NOT do (out of scope for commit 2, deferred to 3/4):
//   - Dock area edge embedding (sub-dock inside a QMainWindow area).
//   - Layout persistence (workspace save/load).
//   - Animation / rubber-band feedback.
//   - GPU/CPU awareness (P0 placeholders live in VRenderPath.h; the
//     connection from VTabPageBase.preferredRenderPath() to actual
//     behaviour is wired in commit 3).
//
//   P7.6.x v3 commit 2 (2026-10-14).
#pragma once

#include <QWidget>
#include <QList>
#include <QString>
#include <QPoint>
#include <QPointer>
#include <QHash>
#include <QTabBar>
#include <QStackedWidget>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QDockWidget>
#include <QDrag>

#include "VCompositeStrategy.h"

namespace vistella::ui {

class VDropIndicator;
class VDragManager;

class VDraggableTabWidget : public QWidget {
    Q_OBJECT
public:
    explicit VDraggableTabWidget(QWidget* parent = nullptr);
    ~VDraggableTabWidget();

    // P8.3.2 (2026-10-26): the group's preferred size follows the CURRENT
    //   page, not the tallest of all pages.
    //
    //   QStackedWidget::sizeHint() returns the maximum over every page, so a
    //   group whose tallest tab is a big list was as tall as that list even
    //   while showing a small form. The right panel therefore had hundreds of
    //   empty pixels between a group header and its content, and the
    //   property text floated in the middle of a blank void.
    //
    //   minimumSizeHint() follows the current page as well, so the splitter
    //   can give the space back when the user switches to a smaller tab.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Page list API. Page identity is by QWidget* (the page must have
    // setObjectName() for mime data round-trip; we use it for diagnostics).
    // Page title is read from QWidget::windowTitle; set that on the page
    // before calling addPage().
    void   addPage(QWidget* page);                            // append
    void   insertPage(int index, QWidget* page);
    void   removePage(QWidget* page);
    int    pageCount() const { return m_pages.size(); }
    QWidget* pageAt(int index) const;
    int     indexOf(QWidget* page) const;

    // Active page (mirrors QTabBar::currentIndex).
    int     currentIndex() const;
    void    setCurrentIndex(int idx);
    QWidget* currentPage() const;

    // Composite strategy (P0 placeholder; wired in P8.4).
    CompositeStrategy compositeStrategy() const          { return m_compositeStrategy; }
    void              setCompositeStrategy(CompositeStrategy s) { m_compositeStrategy = s; }

    // Drag enable (default true). Disabling skips the event filter install.
    void   setDraggable(bool on);
    bool   isDraggable() const { return m_draggable; }

    // Manual float / re-dock. Both work even when setDraggable(false);
    // they are part of the programmatic API (tests, scripted moves).
    void   undockPage(QWidget* page);
    void   redockPage(QWidget* page, int insertIndex = -1);
    bool   isPageFloating(QWidget* page) const;
    int    dockedCount() const;
    int    floatingCount() const;

    // Cross-widget move: detach from this widget, re-insert into target.
    void   movePageTo(QWidget* page, VDraggableTabWidget* target, int insertIndex);

    // Set / get a page's title (also writes windowTitle so QTabBar shows it).
    void     setPageTitle(QWidget* page, const QString& title);
    QString  pageTitle(QWidget* page) const;

    // The QTabBar (exposed for tests + external lookups). Lifetime owned
    // by this widget.
    QTabBar*       tabBar()      const { return m_tabBar; }
    QStackedWidget* stack()      const { return m_stack; }

    // Custom mime type used in the QDrag. Other VDraggableTabWidget instances
    // listen for this type to recognise our drags.
    static QString mimeType();

signals:
    void pageAdded(QWidget* page);
    void pageRemoved(QWidget* page);
    void currentChanged(int index, QWidget* page);

    // Drag transitions (also fired on programmatic undock/redock for symmetry).
    void pageUndocked(QWidget* page);
    void pageRedocked(QWidget* page, int newIndex);
    void pageMoved(QWidget* page, VDraggableTabWidget* newHost, int newIndex);

    // User closed a floating dock (via the QDockWidget close button).
    void pageCloseRequested(QWidget* page);

protected:
    // Tab bar event filter (intercepts press/move for drag start).
    bool eventFilter(QObject* obj, QEvent* e);

    // Drop protocol on the widget itself (the VDraggableTabWidget acts as
    // both the empty-area drop target and the proxy for the tab bar).
    void dragEnterEvent(QDragEnterEvent* e);
    void dragMoveEvent(QDragMoveEvent* e);
    void dropEvent(QDropEvent* e);
    void dragLeaveEvent(QDragLeaveEvent* e);

private:
    struct PageState {
        QWidget* page = nullptr;            // owned by us; destroyed with us
        QString  title;                     // cached windowTitle
        bool     isFloating = false;
        QPointer<QDockWidget> dockWrapper;  // only valid when isFloating
    };
    QList<PageState> m_pages;
    QHash<QWidget*, int> m_indexOf;  // page* -> index in m_pages

    // Drag state (only valid mid-drag).
    QPointer<QWidget>  m_pressedPage;
    QPoint             m_pressLocalPos;
    QPoint             m_pressGlobalPos;
    bool               m_dragArmed   = false;
    QPointer<QDrag>    m_activeDrag;
    int                m_dragSourceIndex = -1;

    void buildUi();
    void rebuildIndexMap();
    int  indexOfPage(QWidget* p) const { return m_indexOf.value(p, -1); }
    PageState* findState(QWidget* p);
    const PageState* findState(QWidget* p) const;

    // Drag flow.
    void onTabBarMousePress(QWidget* page, const QPoint& localPos, const QPoint& globalPos);
    void onTabBarMouseMove(const QPoint& localPos, const QPoint& globalPos);
    void onTabBarMouseRelease();
    bool maybeStartDrag(QWidget* page, const QPoint& globalPos);

    // Drop flow.
    void updateDropHint(const QPoint& globalPos);
    void clearDropHint();
    int  calcInsertIndexAt(const QPoint& globalPos) const;  // -1 if not over tab bar
    bool isOverEmptyArea(const QPoint& globalPos) const;   // for "create floating"

    // Undock / redock helpers.
    QDockWidget* createDockWrapper(QWidget* page);
    void         destroyDockWrapper(PageState& s);
    void         dockPageToStack(QWidget* page, int insertIndex);
    void         undockPageFromStack(QWidget* page);

    // UI.
    QTabBar*        m_tabBar = nullptr;
    QStackedWidget* m_stack  = nullptr;
    QPointer<VDropIndicator> m_indicator;

    // Behaviour.
    bool m_draggable = true;
    CompositeStrategy m_compositeStrategy = CompositeStrategy::Sequential;

    // Friend so the VDragManager can call our internal helpers without
    // exposing the full PageState layout.
    friend class VDragManager;
};

} // namespace vistella::ui
