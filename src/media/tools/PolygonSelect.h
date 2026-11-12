// SPDX-License-Identifier: MIT
//
// PolygonSelect
//
// The polygonal marquee (M2-5c).
//
//   Click to add a vertex, double-click to close the polygon and commit
//   the selection. Unlike the lasso, which follows the mouse
//   continuously, the polygon has DISCRETE vertices -- so the gesture is
//   click / click / click / double-click.
//
//   The strategy does the mask computation; the tool drives the gesture
//   and applies the result to the document's SelectionModel (so the
//   selection mode survives a tool switch).
#pragma once

#include "ToolState.h"

#include <memory>

#include "../selection/SelectionModel.h"

class QCursor;
class QWidget;
class QMouseEvent;

namespace selection { class PolygonSelectionStrategy; }

namespace tools {

class PolygonSelect : public ToolState
{
public:
    explicit PolygonSelect(QWidget* parent = nullptr);
    ~PolygonSelect() override;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    // The double click closes the polygon and commits the selection.
    void onMouseDoubleClick(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::PolygonSelect; }
    QString pageTitle() const override { return QStringLiteral("Polygon Select"); }
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // The selection mode (New / Add / Subtract / Intersect).
    void setSelectionMode(selection::SelectionModel::Mode m);
    selection::SelectionModel::Mode selectionMode() const;

    // ===== test accessors =====
    int  vertexCount() const;
    bool isClosed() const;

private:
    // Commit the polygon into the document's selection.
    bool commitPolygon(ImageWindow* host, Qt::KeyboardModifiers mods);

    std::unique_ptr<selection::PolygonSelectionStrategy> m_strategy;

    // Weak view onto the document's selection model (bound in onEnter).
    selection::SelectionModel* m_selectionModel = nullptr;

    // Fallback used only while no document is attached.
    selection::SelectionModel::Mode m_detachedMode = selection::SelectionModel::Mode::Replace;
};

} // namespace tools
