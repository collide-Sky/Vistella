// VDragManager.cpp
#include "VDragManager.h"

#include <QApplication>
#include <QWidget>

#include "VDraggableTabWidget.h"
#include "VDropIndicator.h"

namespace vistella::ui {

VDragManager& VDragManager::instance()
{
    static VDragManager m;
    return m;
}

VDragManager::VDragManager(QObject* parent)
    : QObject(parent)
{
}

void VDragManager::registerTabWidget(VDraggableTabWidget* w)
{
    if (w && !m_widgets.contains(w)) {
        m_widgets.append(w);
    }
}

void VDragManager::unregisterTabWidget(VDraggableTabWidget* w)
{
    m_widgets.removeAll(w);
    if (m_indicatorOwner == w) {
        releaseIndicator(w);
    }
}

VDraggableTabWidget* VDragManager::tabWidgetAt(const QPoint& globalPos) const
{
    // QWidget::childAt returns the topmost child; we want the *registered*
    // VDraggableTabWidget ancestor. Walk up the parent chain.
    QWidget* w = QApplication::widgetAt(globalPos);
    while (w) {
        // We can't use a qobject_cast here because VDraggableTabWidget is
        // forward-declared; instead search our registry.
        for (VDraggableTabWidget* tw : m_widgets) {
            if (tw == w) return tw;
        }
        w = w->parentWidget();
    }
    return nullptr;
}

VDropIndicator* VDragManager::acquireIndicator(VDraggableTabWidget* owner)
{
    if (!m_indicator) {
        m_indicator = new VDropIndicator();
        m_indicator->setObjectName(QStringLiteral("VDragIndicator"));
    }
    m_indicatorOwner = owner;
    return m_indicator;
}

void VDragManager::releaseIndicator(VDraggableTabWidget* owner)
{
    Q_UNUSED(owner);
    if (m_indicator) {
        m_indicator->detach();
    }
    m_indicatorOwner.clear();
}

VDropIndicator* VDragManager::activeIndicator() const
{
    return m_indicator.data();
}

VDraggableTabWidget* VDragManager::indicatorOwner() const
{
    return m_indicatorOwner.data();
}

int VDragManager::dragStartThreshold() const
{
    if (m_dragThreshold > 0) return m_dragThreshold;
    return QApplication::startDragDistance() * 2;
}

void VDragManager::beginDrag(VDraggableTabWidget* source, QWidget* page)
{
    m_activeSource = source;
    m_activePage   = page;
    emit dragStarted(source, page);
}

VDraggableTabWidget* VDragManager::activeDragSource() const
{
    return m_activeSource.data();
}

QWidget* VDragManager::activeDragPage() const
{
    return m_activePage.data();
}

void VDragManager::endDrag()
{
    VDraggableTabWidget* src = m_activeSource.data();
    QWidget* pg = m_activePage.data();
    m_activeSource.clear();
    m_activePage.clear();
    emit dragEnded(src, pg, src != nullptr);  // accepted iff source still alive
}

} // namespace vistella::ui
