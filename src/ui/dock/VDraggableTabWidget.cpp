// VDraggableTabWidget.cpp
//   P7.6.x v3 commit 2 (2026-10-14).
#include "VDraggableTabWidget.h"

#include <QApplication>
#include <QTabBar>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionTab>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <QDebug>

#include "VDragManager.h"
#include "VDropIndicator.h"

namespace vistella::ui {

// ---------------------------------------------------------------------------
// Mime type shared by all VDraggableTabWidget instances.
// ---------------------------------------------------------------------------
QString VDraggableTabWidget::mimeType()
{
    return QStringLiteral("application/x-vistella-vtabpage");
}

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
VDraggableTabWidget::VDraggableTabWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    VDragManager::instance().registerTabWidget(this);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
}

VDraggableTabWidget::~VDraggableTabWidget()
{
    // Detach floating wrappers so they don't keep dangling refs to us.
    for (PageState& s : m_pages) {
        if (s.isFloating && s.dockWrapper) {
            // The wrapper is a top-level QDockWidget; deleting it is safe
            // because we created it via new and it has no Qt parent.
            delete s.dockWrapper;
            s.dockWrapper.clear();
        }
    }
    VDragManager::instance().unregisterTabWidget(this);
}

void VDraggableTabWidget::buildUi()
{
    m_tabBar = new QTabBar(this);
    m_tabBar->setExpanding(false);
    m_tabBar->setMovable(false);   // we own the move semantics
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setDrawBase(true);
    m_tabBar->setMinimumHeight(24);

    m_stack = new QStackedWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabBar);
    layout->addWidget(m_stack, 1);

    m_tabBar->installEventFilter(this);

    connect(m_tabBar, &QTabBar::currentChanged, this,
            [this](int idx) {
                if (idx >= 0 && idx < m_pages.size()) {
                    QWidget* page = m_pages[idx].page;
                    if (page) m_stack->setCurrentWidget(page);
                    emit currentChanged(idx, page);
                }
            });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this,
            [this](int idx) {
                if (idx >= 0 && idx < m_pages.size()) {
                    QWidget* page = m_pages[idx].page;
                    if (page) emit pageCloseRequested(page);
                }
            });
}

// ---------------------------------------------------------------------------
// Preferred size
// ---------------------------------------------------------------------------

QSize VDraggableTabWidget::sizeHint() const
{
    // Tab bar + the page that is currently shown.
    QSize s;
    if (m_tabBar) s = m_tabBar->sizeHint();
    if (m_stack) {
        if (QWidget* page = m_stack->currentWidget())
            s = s.expandedTo(page->sizeHint());
    }
    return s;
}

QSize VDraggableTabWidget::minimumSizeHint() const
{
    QSize s;
    if (m_tabBar) s = m_tabBar->sizeHint();
    if (m_stack) {
        if (QWidget* page = m_stack->currentWidget())
            s = s.expandedTo(page->minimumSizeHint());
    }
    // The tab bar is the only way to reach the other pages of this group,
    // so it must never be squeezed out of existence.
    s.setHeight(qMax(s.height(), 24));
    s.setWidth(qMax(s.width(), 60));
    return s;
}

// ---------------------------------------------------------------------------
// Page list
// ---------------------------------------------------------------------------
void VDraggableTabWidget::addPage(QWidget* page)
{
    insertPage(m_pages.size(), page);
}

void VDraggableTabWidget::insertPage(int index, QWidget* page)
{
    if (!page) return;
    if (m_indexOf.contains(page)) return;     // already tracked
    if (index < 0) index = 0;
    if (index > m_pages.size()) index = m_pages.size();

    PageState s;
    s.page  = page;
    s.title = page->windowTitle();
    s.isFloating = false;
    m_pages.insert(index, s);
    rebuildIndexMap();

    // Wire the title so the tab bar reflects changes to the page.
    page->installEventFilter(this);

    m_tabBar->insertTab(index, s.title);
    m_stack->insertWidget(index, page);
    if (m_pages.size() == 1) m_tabBar->setCurrentIndex(0);

    emit pageAdded(page);
}

void VDraggableTabWidget::removePage(QWidget* page)
{
    int idx = indexOfPage(page);
    if (idx < 0) return;
    PageState& s = m_pages[idx];
    if (s.isFloating) destroyDockWrapper(s);
    m_tabBar->removeTab(idx);
    m_stack->removeWidget(page);
    page->removeEventFilter(this);
    m_pages.removeAt(idx);
    rebuildIndexMap();

    emit pageRemoved(page);
}

int VDraggableTabWidget::indexOf(QWidget* page) const
{
    return indexOfPage(page);
}

QWidget* VDraggableTabWidget::pageAt(int index) const
{
    if (index < 0 || index >= m_pages.size()) return nullptr;
    return m_pages[index].page;
}

int VDraggableTabWidget::currentIndex() const
{
    return m_tabBar ? m_tabBar->currentIndex() : -1;
}

void VDraggableTabWidget::setCurrentIndex(int idx)
{
    if (m_tabBar) m_tabBar->setCurrentIndex(idx);
}

QWidget* VDraggableTabWidget::currentPage() const
{
    int idx = currentIndex();
    return (idx >= 0) ? pageAt(idx) : nullptr;
}

void VDraggableTabWidget::rebuildIndexMap()
{
    m_indexOf.clear();
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i].page) m_indexOf.insert(m_pages[i].page, i);
    }
}

auto VDraggableTabWidget::findState(QWidget* p) -> PageState*
{
    int i = indexOfPage(p);
    return (i < 0) ? nullptr : &m_pages[i];
}

auto VDraggableTabWidget::findState(QWidget* p) const -> const PageState*
{
    int i = indexOfPage(p);
    return (i < 0) ? nullptr : &m_pages[i];
}

void VDraggableTabWidget::setPageTitle(QWidget* page, const QString& title)
{
    PageState* s = findState(page);
    if (!s) return;
    s->title = title;
    page->setWindowTitle(title);
    int i = indexOfPage(page);
    if (i >= 0 && m_tabBar) m_tabBar->setTabText(i, title);
}

QString VDraggableTabWidget::pageTitle(QWidget* page) const
{
    const PageState* s = findState(page);
    return s ? s->title : QString();
}

int VDraggableTabWidget::dockedCount() const
{
    int n = 0;
    for (const PageState& s : m_pages) if (!s.isFloating) ++n;
    return n;
}

int VDraggableTabWidget::floatingCount() const
{
    int n = 0;
    for (const PageState& s : m_pages) if (s.isFloating) ++n;
    return n;
}

bool VDraggableTabWidget::isPageFloating(QWidget* page) const
{
    const PageState* s = findState(page);
    return s ? s->isFloating : false;
}

void VDraggableTabWidget::setDraggable(bool on)
{
    if (m_draggable == on) return;
    m_draggable = on;
    // The event filter is always installed (cheap); the on/off branch
    // is checked inside eventFilter.
}

// ---------------------------------------------------------------------------
// Undock / redock
// ---------------------------------------------------------------------------
QDockWidget* VDraggableTabWidget::createDockWrapper(QWidget* page)
{
    auto* dock = new QDockWidget(this);  // parent = us, so the QObject tree
                                         // is clean even if we forget.
    dock->setObjectName(QStringLiteral("VDraggableTabWidget_floating_") + page->objectName());
    dock->setAttribute(Qt::WA_DeleteOnClose, false);
    dock->setFeatures(QDockWidget::DockWidgetClosable
                    | QDockWidget::DockWidgetMovable
                    | QDockWidget::DockWidgetFloatable);
    dock->setWindowTitle(page->windowTitle());
    dock->setWidget(page);    // reparent into the dock wrapper

    // When the user clicks the dock's close button, treat as pageCloseRequested.
    connect(dock, &QDockWidget::visibilityChanged, this,
            [this, dock, page](bool visible) {
                if (!visible) {
                    // Only fire if the user actually closed (not just hidden).
                    // QDockWidget::isFloating() == false AND visible == false
                    // means the user pressed X.
                    if (!dock->isFloating()) {
                        emit pageCloseRequested(page);
                    }
                }
            });
    return dock;
}

void VDraggableTabWidget::destroyDockWrapper(PageState& s)
{
    if (!s.dockWrapper) return;
    QDockWidget* dock = s.dockWrapper.data();
    s.dockWrapper.clear();
    s.isFloating = false;
    if (dock) {
        // Detach page back to us so it isn't destroyed with the dock.
        if (s.page) dock->setWidget(nullptr);
        dock->deleteLater();
    }
}

void VDraggableTabWidget::undockPageFromStack(QWidget* page)
{
    int idx = indexOfPage(page);
    if (idx < 0) return;
    PageState& s = m_pages[idx];
    if (s.isFloating) return;

    // Remove the page from the visible stack + tab bar, but KEEP it in
    // m_pages with isFloating=true so the public count APIs (dockedCount,
    // floatingCount, isPageFloating, indexOf) all still see it. Single
    // source of truth = m_pages.
    m_stack->removeWidget(page);
    m_tabBar->removeTab(idx);
    s.isFloating = true;
    s.dockWrapper = createDockWrapper(page);
    QDockWidget* dock = s.dockWrapper.data();

    QPoint gpos = QCursor::pos();
    dock->move(gpos - QPoint(40, 12));
    dock->show();
    dock->raise();

    emit pageUndocked(page);
}

void VDraggableTabWidget::dockPageToStack(QWidget* page, int insertIndex)
{
    int idx = indexOfPage(page);
    if (idx < 0) return;
    if (!m_pages[idx].isFloating) return;

    // Tear down the dock wrapper and reparent the page back into our stack.
    PageState s = m_pages.takeAt(idx);
    rebuildIndexMap();
    if (s.dockWrapper) {
        s.dockWrapper->setWidget(nullptr);
        destroyDockWrapper(s);
    }
    s.isFloating = false;
    s.dockWrapper.clear();

    if (insertIndex < 0) insertIndex = m_pages.size();
    if (insertIndex > m_pages.size()) insertIndex = m_pages.size();

    m_pages.insert(insertIndex, s);
    rebuildIndexMap();
    m_tabBar->insertTab(insertIndex, s.title);
    m_stack->insertWidget(insertIndex, page);
    if (m_pages.size() == 1) m_tabBar->setCurrentIndex(0);

    emit pageRedocked(page, insertIndex);
}

void VDraggableTabWidget::undockPage(QWidget* page)
{
    if (isPageFloating(page)) return;
    undockPageFromStack(page);
}

void VDraggableTabWidget::redockPage(QWidget* page, int insertIndex)
{
    if (!isPageFloating(page)) return;
    dockPageToStack(page, insertIndex);
}

void VDraggableTabWidget::movePageTo(QWidget* page, VDraggableTabWidget* target, int insertIndex)
{
    if (!target || target == this) {
        if (target == this) redockPage(page, insertIndex);
        return;
    }
    // Pull out of our state first (works whether the page is docked or
    // floating here).
    int srcIdx = indexOfPage(page);
    if (srcIdx < 0) return;
    PageState s = m_pages.takeAt(srcIdx);
    rebuildIndexMap();
    if (s.isFloating && s.dockWrapper) {
        s.dockWrapper->setWidget(nullptr);
        s.dockWrapper->deleteLater();
    } else {
        // Docked here: remove from our stack and tab bar.
        m_stack->removeWidget(page);
        m_tabBar->removeTab(srcIdx);
    }

    // Append into target.
    s.isFloating = false;
    s.dockWrapper.clear();
    if (insertIndex < 0) insertIndex = target->m_pages.size();
    if (insertIndex > target->m_pages.size()) insertIndex = target->m_pages.size();
    target->m_pages.insert(insertIndex, s);
    target->rebuildIndexMap();
    page->setParent(target);
    target->m_tabBar->insertTab(insertIndex, s.title);
    target->m_stack->insertWidget(insertIndex, page);
    page->installEventFilter(target);
    if (target->m_pages.size() == 1) target->m_tabBar->setCurrentIndex(0);

    emit pageMoved(page, target, insertIndex);
    target->pageAdded(page);  // also fire on target for symmetry
}

// ---------------------------------------------------------------------------
// eventFilter: tab bar mouse events + per-page title updates
// ---------------------------------------------------------------------------
bool VDraggableTabWidget::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_tabBar) {
        switch (e->type()) {
        case QEvent::MouseButtonPress: {
            if (!m_draggable) break;
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() != Qt::LeftButton) break;
            int tab = m_tabBar->tabAt(me->pos());
            if (tab < 0 || tab >= m_pages.size()) break;
            if (m_pages[tab].isFloating) break;   // floating pages aren't on our tab bar
            QWidget* page = m_pages[tab].page;
            m_pressedPage      = page;
            m_pressLocalPos    = me->pos();
            m_pressGlobalPos   = me->globalPos();
            m_dragArmed        = true;
            m_dragSourceIndex  = tab;
            return false;  // let QTabBar still process the click (changes current)
        }
        case QEvent::MouseMove: {
            if (!m_dragArmed || !m_pressedPage) break;
            auto* me = static_cast<QMouseEvent*>(e);
            if (!(me->buttons() & Qt::LeftButton)) break;
            const QPoint g = me->globalPos();
            int threshold = VDragManager::instance().dragStartThreshold();
            if ((g - m_pressGlobalPos).manhattanLength() < threshold) break;
            if (maybeStartDrag(m_pressedPage, g)) {
                // QDrag took over. Stop further mouse events.
                m_dragArmed = false;
                m_pressedPage.clear();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            m_dragArmed = false;
            m_pressedPage.clear();
            break;
        }
        default: break;
        }
    } else if (obj && m_indexOf.contains(static_cast<QWidget*>(obj))) {
        // A page we're tracking fired an event; watch for title changes.
        if (e->type() == QEvent::WindowTitleChange) {
            QWidget* p = static_cast<QWidget*>(obj);
            int i = indexOfPage(p);
            if (i >= 0) {
                m_pages[i].title = p->windowTitle();
                m_tabBar->setTabText(i, m_pages[i].title);
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

// ---------------------------------------------------------------------------
// Drag start
// ---------------------------------------------------------------------------
bool VDraggableTabWidget::maybeStartDrag(QWidget* page, const QPoint& globalPos)
{
    if (!page) return false;
    int idx = indexOfPage(page);
    if (idx < 0) return false;
    if (m_pages[idx].isFloating) return false;

    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    // Carrying the page pointer in the mime data is unsafe across process
    // boundaries, but we are intra-process only. The drop side resolves
    // it via VDragManager (activeDragSource / activeDragPage) for
    // additional safety, with the mime data as a sanity check.
    mime->setText(QStringLiteral("vistella://vtabpage/%1")
                      .arg(reinterpret_cast<quintptr>(page)));
    mime->setData(mimeType(), QByteArray());
    drag->setMimeData(mime);

    // Ghost image: a small snapshot of the tab.
    int tab = m_tabBar->tabAt(m_tabBar->mapFromGlobal(globalPos));
    if (tab >= 0) {
        // QTabBar::tabSizeHint + initStyleOption are protected; use the
        // tab rect on screen as a reasonable proxy and draw a simple
        // rounded rect for the ghost.
        QRect r = m_tabBar->tabRect(tab);
        QSize sz(qMax(80, r.width()), qMax(20, r.height()));
        QPixmap pix(sz);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setOpacity(0.7);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(QColor(0x1f, 0x6f, 0xeb, 80));
        p.setPen(QPen(QColor(0x1f, 0x6f, 0xeb), 1));
        p.drawRoundedRect(pix.rect().adjusted(1, 1, -1, -1), 4, 4);
        if (tab >= 0 && tab < m_pages.size()) {
            p.setPen(Qt::black);
            p.drawText(pix.rect().adjusted(6, 2, -6, -2),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       m_pages[tab].title);
        }
        drag->setPixmap(pix);
        drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
    }

    m_activeDrag = drag;
    VDragManager::instance().beginDrag(this, page);
    Qt::DropAction action = drag->exec(Qt::MoveAction | Qt::IgnoreAction, Qt::MoveAction);
    VDragManager::instance().endDrag();
    m_activeDrag.clear();

    // If the drop target successfully claimed the page (via movePageTo
    // callback), the page is no longer in our list. If the user dropped
    // on empty area, we already created a floating dock and the page is
    // also no longer in m_pages. Either way, we return true so the
    // eventFilter stops forwarding mouse events.
    return action == Qt::MoveAction;
}

// ---------------------------------------------------------------------------
// Drop protocol (the widget itself receives events for the empty area; the
// tab bar forwards them via the event filter).
// ---------------------------------------------------------------------------
void VDraggableTabWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasFormat(mimeType())) return;
    e->acceptProposedAction();
    updateDropHint(e->pos());
}

void VDraggableTabWidget::dragMoveEvent(QDragMoveEvent* e)
{
    if (!e->mimeData()->hasFormat(mimeType())) return;
    e->acceptProposedAction();
    updateDropHint(e->pos());
}

void VDraggableTabWidget::dragLeaveEvent(QDragLeaveEvent* e)
{
    Q_UNUSED(e);
    clearDropHint();
}

void VDraggableTabWidget::dropEvent(QDropEvent* e)
{
    if (!e->mimeData()->hasFormat(mimeType())) {
        e->ignore();
        return;
    }
    QPoint gpos = e->pos();
    int insertIdx = calcInsertIndexAt(gpos);
    VDraggableTabWidget* src = VDragManager::instance().activeDragSource();
    QWidget* pg             = VDragManager::instance().activeDragPage();
    if (!src || !pg) {
        e->ignore();
        clearDropHint();
        return;
    }
    if (insertIdx < 0) {
        // Dropped in the empty area of *this* widget (e.g. below the tab
        // bar) -> the source handles it as a "tear off to floating dock"
        // (no special action here; the source already turned it into a
        // floating dock when exec returned IgnoreAction). Accept anyway
        // so the cursor feedback is clean.
        e->acceptProposedAction();
        clearDropHint();
        return;
    }
    // Avoid no-op self-moves (drop on the same tab the user picked up).
    if (src == this) {
        int srcIdx = src->indexOf(pg);
        if (srcIdx == insertIdx || srcIdx + 1 == insertIdx) {
            e->acceptProposedAction();
            clearDropHint();
            return;
        }
    }
    e->acceptProposedAction();
    src->movePageTo(pg, this, insertIdx);
    clearDropHint();
}

void VDraggableTabWidget::updateDropHint(const QPoint& globalPos)
{
    if (!m_indicator) {
        m_indicator = VDragManager::instance().acquireIndicator(this);
    }
    int idx = calcInsertIndexAt(globalPos);
    if (idx >= 0) {
        // Position the indicator between tabs (InterTab shape).
        if (idx >= m_tabBar->count()) {
            // After last tab: snap to end of tab bar.
            QRect lastTab = m_tabBar->tabRect(m_tabBar->count() - 1);
            QPoint topLeft = m_tabBar->mapToGlobal(lastTab.topRight());
            QSize  size(2, m_tabBar->height());
            m_indicator->setParent(m_tabBar);
            m_indicator->setGeometry(QRect(m_tabBar->mapFromGlobal(topLeft), size));
        } else {
            QRect r = m_tabBar->tabRect(idx);
            QPoint topLeft = m_tabBar->mapToGlobal(r.topLeft());
            QSize  size(2, m_tabBar->height());
            m_indicator->setParent(m_tabBar);
            m_indicator->setGeometry(QRect(m_tabBar->mapFromGlobal(topLeft), size));
        }
        m_indicator->setShape(VDropIndicator::Shape::InterTab);
        m_indicator->show();
        m_indicator->raise();
    } else if (isOverEmptyArea(globalPos)) {
        m_indicator->setParent(this);
        QPoint p = mapFromGlobal(globalPos);
        QSize  s(80, 60);
        m_indicator->setGeometry(QRect(p - QPoint(40, 30), s));
        m_indicator->setShape(VDropIndicator::Shape::DockArea);
        m_indicator->show();
        m_indicator->raise();
    } else {
        m_indicator->hide();
    }
}

void VDraggableTabWidget::clearDropHint()
{
    if (m_indicator) m_indicator->hide();
    VDragManager::instance().releaseIndicator(this);
}

int VDraggableTabWidget::calcInsertIndexAt(const QPoint& globalPos) const
{
    if (!m_tabBar) return -1;
    QPoint local = m_tabBar->mapFromGlobal(globalPos);
    if (!QRect(0, 0, m_tabBar->width(), m_tabBar->height()).contains(local)) {
        return -1;
    }
    int tab = m_tabBar->tabAt(local);
    if (tab < 0) {
        return m_tabBar->count();
    }
    QRect r = m_tabBar->tabRect(tab);
    int midX = r.left() + r.width() / 2;
    return (local.x() < midX) ? tab : tab + 1;
}

bool VDraggableTabWidget::isOverEmptyArea(const QPoint& globalPos) const
{
    QPoint local = mapFromGlobal(globalPos);
    return rect().contains(local)
        && (m_tabBar ? !m_tabBar->rect().contains(m_tabBar->mapFromGlobal(globalPos)) : true);
}

void VDraggableTabWidget::onTabBarMousePress(QWidget* page, const QPoint& localPos, const QPoint& globalPos)
{
    Q_UNUSED(page); Q_UNUSED(localPos); Q_UNUSED(globalPos);
}

void VDraggableTabWidget::onTabBarMouseMove(const QPoint& localPos, const QPoint& globalPos)
{
    Q_UNUSED(localPos); Q_UNUSED(globalPos);
}

void VDraggableTabWidget::onTabBarMouseRelease()
{
}

} // namespace vistella::ui
