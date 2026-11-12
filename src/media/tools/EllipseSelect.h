// SPDX-License-Identifier: MIT
//
// EllipseSelect
//
// The elliptical marquee (M2-5c).
//
//   Drag an ellipse, release, and the selection is the ellipse. It is
//   the same shape as the rectangle marquee but with the ellipse
//   inscribed in the dragged rectangle.
//
//   The strategy does the mask computation; the tool drives the
//   gesture and applies the result to the document's SelectionModel
//   (so the selection mode survives a tool switch).
#pragma once

#include "ToolState.h"

#include <memory>

#include "../selection/SelectionModel.h"

class QCursor;
class QWidget;
class QMouseEvent;

namespace selection { class EllipseSelectionStrategy; }

namespace tools {

class EllipseSelect : public ToolState
{
public:
    explicit EllipseSelect(QWidget* parent = nullptr);
    ~EllipseSelect() override;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::EllipseSelect; }
    QString pageTitle() const override { return QStringLiteral("Ellipse Select"); }
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // The selection mode (New / Add / Subtract / Intersect).
    void setSelectionMode(selection::SelectionModel::Mode m);
    selection::SelectionModel::Mode selectionMode() const;

private:
    std::unique_ptr<selection::EllipseSelectionStrategy> m_strategy;

    // Weak view onto the document's selection model (bound in onEnter).
    selection::SelectionModel* m_selectionModel = nullptr;

    // Fallback used only while no document is attached.
    selection::SelectionModel::Mode m_detachedMode = selection::SelectionModel::Mode::Replace;
};

} // namespace tools
