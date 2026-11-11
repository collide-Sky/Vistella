// SPDX-License-Identifier: MIT
//
// MoveTool implementation
//
// M2-4: the tool moves content for real. See MoveTool.h for the
//   priority order (text item -> selection -> layer pixels).
//
// The undo contract is the important part:
//
//   * The text item drag pushes a TextItemCommand::Move, exactly like
//     the text tool does.
//   * The selection drag is NOT undoable through the selection model
//     (the model has no undo command), so the move is applied live
//     and the pixels it carries are moved with it.
//   * The layer pixel drag snapshots the layer before the drag and
//     pushes the before/after pair on release, exactly like the
//     brush stroke does. This is the contract from
//     docs/CONTRACTS.md: a pixel edit goes through
//     activeLayerImage() + pushLayerImageEdit() + commitLayerEdit().
#include "MoveTool.h"
#include "ToolGeometry.h"

#include "../imagewindow.h"
#include "../graphicstextitem.h"
#include "../imagewindow/TextOverlayController.h"
#include "../selection/SelectionModel.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTransform>

#include <algorithm>

namespace tools {

// ===== lifecycle =====

void MoveTool::onEnter(ImageWindow* host)
{
    m_target = DragTarget::None;
    m_dragItem = nullptr;
    m_layerIndex = -1;
    m_lastDelta = QPointF();
    m_backup.release();
    if (host && host->statusBar()) {
        host->statusBar()->showMessage(
            QStringLiteral("Move tool (V) - drag a layer, a text item or the selection"), 3000);
    }
}

void MoveTool::onExit(ImageWindow* host)
{
    // A drag that is still open when the tool is switched away must
    // not leave the layer half-written: commit it so the pixels and
    // the undo history stay consistent.
    if (isDragging()) finishDrag(host);
    m_target = DragTarget::None;
    m_dragItem = nullptr;
    m_backup.release();
    if (host && host->statusBar()) {
        host->statusBar()->clearMessage();
    }
}

// ===== helpers =====

GraphicsTextItem* MoveTool::textItemAt(ImageWindow* host, const QPointF& scenePos) const
{
    if (!host) return nullptr;
    auto* overlay = host->textOverlay();
    if (!overlay) return nullptr;
    const auto items = overlay->textItems();
    // Iterate BACKWARDS: the list is in creation order, so the last
    // item is the topmost one -- that is the one the user sees and
    // expects to grab.
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        GraphicsTextItem* item = *it;
        if (!item) continue;
        if (item->boundingRect().contains(item->mapFromScene(scenePos))) {
            return item;
        }
    }
    return nullptr;
}

// ===== mouse =====

void MoveTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!host) return;

    // A press while another drag is open cancels the previous one
    // first (the release never arrived).
    if (isDragging()) cancelDrag(host);

    m_pressScenePos = scenePos;
    m_lastDelta = QPointF();

    // 1. A text item under the cursor wins (Photoshop behaviour: the
    //    topmost object is grabbed).
    if (GraphicsTextItem* item = textItemAt(host, scenePos)) {
        beginTextDrag(host, item, scenePos);
        return;
    }

    // 2. A selection: dragging inside the selected area moves the
    //    selection. Outside it, the drag would be ambiguous -- PS
    //    deselects the selection and starts a new one, we leave the
    //    selection alone and move the pixels.
    if (auto* sel = host->selectionModel()) {
        if (!sel->isEmpty() && sel->contains(scenePos.toPoint())) {
            beginSelectionDrag(host, scenePos);
            return;
        }
    }

    // 3. Otherwise: drag the active layer's pixels.
    beginLayerDrag(host, scenePos);
}

void MoveTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!isDragging() || !host) return;

    switch (m_target) {
    case DragTarget::TextItem:   applyTextDrag(scenePos); break;
    case DragTarget::Selection:  applySelectionDrag(host, scenePos); break;
    case DragTarget::LayerPixels:applyLayerDrag(host, scenePos); break;
    case DragTarget::None:       break;
    }
}

void MoveTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!isDragging()) return;
    finishDrag(host);
}

// ===== text item drag =====

void MoveTool::beginTextDrag(ImageWindow* host, GraphicsTextItem* item, const QPointF& scenePos)
{
    Q_UNUSED(host);
    m_target = DragTarget::TextItem;
    m_dragItem = item;
    m_pressItemPos = item->position();
    m_pressScenePos = scenePos;
}

void MoveTool::applyTextDrag(const QPointF& scenePos)
{
    if (!m_dragItem) return;
    const QPointF delta = scenePos - m_pressScenePos;
    m_lastDelta = delta;
    m_dragItem->setPosition(m_pressItemPos + delta);
}

// ===== selection drag =====

void MoveTool::beginSelectionDrag(ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(scenePos);
    m_target = DragTarget::Selection;
    if (auto* sel = host->selectionModel()) {
        m_selBBoxAtPress = sel->boundingRect();
        // Keep the mask as it was at press time.
        //
        //   The drag applies the translation RELATIVE to the press
        //   position, not to the current mask. Without this copy the
        //   translation would compound on every mouse move and the
        //   selection would run away from the cursor.
        m_selMaskAtPress = sel->mask();
    } else {
        m_selBBoxAtPress = QRect();
        m_selMaskAtPress = QImage();
    }
}

void MoveTool::applySelectionDrag(ImageWindow* host, const QPointF& scenePos)
{
    auto* sel = host->selectionModel();
    if (!sel || sel->isEmpty() || m_selMaskAtPress.isNull()) return;

    const QPointF delta = scenePos - m_pressScenePos;
    m_lastDelta = delta;

    // Rebuild the mask from the press-time copy, translated by the
    // total delta. The mask keeps the model's own size.
    QTransform t;
    t.translate(delta.x(), delta.y());
    QImage moved = m_selMaskAtPress.transformed(t);
    sel->setMask(moved, selection::SelectionModel::Mode::Replace);
}

// ===== layer pixel drag =====

void MoveTool::beginLayerDrag(ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(scenePos);
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        // Nothing to drag (no editable bitmap layer) -- the drag is
        // simply not started. The tool stays in the None state so the
        // release does not try to commit anything.
        m_target = DragTarget::None;
        return;
    }
    m_layerIndex = host->activeLayerIndex();
    // Snapshot the layer pixels for undo (pushed on release), the
    // same contract the brush stroke uses.
    m_backup = target->clone();
    m_target = DragTarget::LayerPixels;
}

void MoveTool::applyLayerDrag(ImageWindow* host, const QPointF& scenePos)
{
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty() || m_backup.empty()) return;

    const QPointF raw = scenePos - m_pressScenePos;

    // The moved content must stay inside the canvas: a layer that is
    // dragged off the edge would lose pixels forever.
    const QPointF delta = ToolGeometry::clampDeltaToCanvas(
        host, raw, QPointF(m_backup.cols, m_backup.rows));

    if (qFuzzyCompare(delta.x(), m_lastDelta.x())
        && qFuzzyCompare(delta.y(), m_lastDelta.y())) {
        return;   // nothing changed yet
    }
    m_lastDelta = delta;

    const int dx = int(std::lround(delta.x()));
    const int dy = int(std::lround(delta.y()));
    const int w = m_backup.cols;
    const int h = m_backup.rows;

    // The destination of the backup is the backup shifted by (dx, dy).
    // The part that lands outside the canvas is dropped.
    //
    //   source rect = the backup region that ends up on the canvas
    //   dest rect   = where that region lands
    //
    // Both are clamped to the canvas, and the source rect is derived
    // from the dest rect by subtracting the shift -- that keeps the
    // two in sync by construction (a separate computation of the two
    // is exactly how the off-by-one creeps in).
    const int destX0 = std::max(0, dx);
    const int destY0 = std::max(0, dy);
    const int destX1 = std::min(w, dx + w);
    const int destY1 = std::min(h, dy + h);
    if (destX1 <= destX0 || destY1 <= destY0) return;

    const int srcX0 = destX0 - dx;
    const int srcY0 = destY0 - dy;
    const int rw = destX1 - destX0;
    const int rh = destY1 - destY0;

    // Start from an empty layer and copy the shifted region in.
    //
    //   The copy goes through a temporary buffer: the source and the
    //   destination overlap, so a direct copy would smear the pixels
    //   that have not been read yet.
    cv::Mat shifted = cv::Mat::zeros(m_backup.size(), m_backup.type());
    cv::Mat srcROI = m_backup(cv::Rect(srcX0, srcY0, rw, rh));
    srcROI.copyTo(shifted(cv::Rect(destX0, destY0, rw, rh)));

    // Write the result back into the live layer buffer.
    shifted.copyTo(*target);
    host->refreshCanvasNow();
}

// ===== finish / cancel =====

void MoveTool::finishDrag(ImageWindow* host)
{
    if (!host) {
        m_target = DragTarget::None;
        return;
    }

    if (m_target == DragTarget::TextItem && m_dragItem) {
        const QPointF oldPos = m_pressItemPos;
        const QPointF newPos = m_dragItem->position();
        // No real movement -> no empty command in the undo stack.
        if (!qFuzzyCompare(oldPos.x(), newPos.x())
            || !qFuzzyCompare(oldPos.y(), newPos.y())) {
            if (auto* stack = host->undoStack()) {
                stack->push(new TextItemCommand(
                    host, m_dragItem, TextItemCommand::Move,
                    QString(), QString(), oldPos, newPos, 0.0, 0.0));
            }
        }
        m_dragItem = nullptr;
    } else if (m_target == DragTarget::LayerPixels) {
        // Push the before/after pair as one undo command -- the same
        // contract the brush stroke uses.
        cv::Mat* cur = host->activeLayerImage();
        if (cur && !m_backup.empty() && cur->size() == m_backup.size()
            && cur->type() == m_backup.type()) {
            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Move layer"));
        }
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);
    }

    m_target = DragTarget::None;
}

void MoveTool::cancelDrag(ImageWindow* host)
{
    if (m_target == DragTarget::LayerPixels && host) {
        // Restore the pixels the drag wrote.
        cv::Mat* target = host->activeLayerImage();
        if (target && !m_backup.empty() && target->size() == m_backup.size()
            && target->type() == m_backup.type()) {
            m_backup.copyTo(*target);
            host->refreshCanvasNow();
        }
        m_backup.release();
    }
    m_target = DragTarget::None;
    m_dragItem = nullptr;
    m_lastDelta = QPointF();
}

} // namespace tools
