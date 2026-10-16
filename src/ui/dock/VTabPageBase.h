// VTabPageBase.h
//
// Base class for VDraggableTabWidget pages.
//
// Why a thin base (instead of adapting the existing widgets via multiple
// inheritance): the existing tab widgets in the project inherit from
// wildly different Qt parents (QFrame / QTreeView / QListWidget / QLabel
// / QComboBox / QWidget). Multi-inheriting from QWidget for each of those
// is fragile, and adapting them to VDraggableTabWidget's contract is
// cleaner as a wrapper layer. See src/ui/dock/pages/*Page.h for the
// concrete wrappers.
//
// What this base gives each page:
//   - Identity:    pageTag() / pageTitle() (required).
//   - Render path: preferredRenderPath() — P0 placeholder for P8.2 GPU
//                  backend abstraction. Default Auto = caller decides.
//   - Strategy:    preferredCompositeStrategy() — P0 placeholder for P8.4
//                  multi-threaded compositing. Default Sequential = current
//                  behaviour.
//   - Content:     contentWidget() — the inner widget VDraggableTabWidget
//                  shows. Default returns `this`.
//
//   P7.6.x v3 commit 3 (2026-10-14).
#pragma once

#include <QWidget>
#include <QString>

#include "VRenderPath.h"
#include "VCompositeStrategy.h"

namespace vistella::ui {

class VTabPageBase : public QWidget {
    Q_OBJECT
public:
    explicit VTabPageBase(QWidget* parent = nullptr);

    // Identity. pageTag is a stable short id (e.g. "color", "swatch") for
    // layout persistence / diagnostics; pageTitle is what the tab shows.
    virtual QString pageTag()   const = 0;
    virtual QString pageTitle() const = 0;

    // PS research 第十点 (P0 placeholder for P8.2 GPU backend).
    virtual RenderPath preferredRenderPath() const
    {
        return RenderPath::Auto;
    }

    // PS research 第十点 (P0 placeholder for P8.4 multi-threaded
    // compositing). 1-line flip in P8.4 to Parallel for pages that are
    // safe to render concurrently.
    virtual CompositeStrategy preferredCompositeStrategy() const
    {
        return CompositeStrategy::Sequential;
    }

    // The inner widget shown inside VDraggableTabWidget's QStackedWidget.
    // Most pages can use the default (`this`).
    virtual QWidget* contentWidget() { return this; }

    // Convenience accessor: tag for diagnostics.
    QString titleForTab() const { return pageTitle(); }

    // P7.10 (2026-10-15): incremental cache invalidation hook.
    //   Called when something outside the page's reactive update path
    //   changes (e.g. active layer change, active channel change, undo/
    //   redo stack mutation). Default is no-op; subclasses override to
    //   drop their derived state and re-fetch from the document model.
    //   This is the entry point for P8.1 incremental compositor hooks:
    //   pages that participate in incremental compositing override this
    //   and emit whatever signals the compositor subscribes to.
    //
    //   Distinction from raw `update()` / `repaint()`:
    //     - update() is the Qt paint scheduler; it doesn't drop derived
    //       caches (e.g. PropertiesDock's parsed file path, or
    //       ChannelPathPanel's last rendered mask thumbnail).
    //     - invalidateCache() is the *model* side: the page's internal
    //       cache of document state is stale and must be re-derived.
    //       Pages may or may not trigger a repaint as part of doing so.
    virtual void invalidateCache() {}

signals:
    // Emitted when pageTitle() should change (e.g. user renames a layer).
    // VDraggableTabWidget listens and updates the tab label.
    void pageTitleChanged(const QString& newTitle);
};

} // namespace vistella::ui
