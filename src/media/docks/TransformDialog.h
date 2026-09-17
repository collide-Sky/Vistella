#ifndef TRANSFORMDIALOG_H
#define TRANSFORMDIALOG_H

// SPDX-License-Identifier: MIT
//
// TransformDialog - P1.4.6 (2026-09-17)
//
// Unified one-shot transform dialog for SmartObject / Bitmap layers.
//   Replaces the chained QInputDialog (scale X / scale Y / rotation) used by
//   P1.4.5 with a single QDialog that holds all 5 fields (scale X, scale Y,
//   rotation degrees, translate X, translate Y) + a Link toggle for the
//   scale pair + a Reset-to-identity button.
//
// Usage (caller pattern, matches P1.4.5's applySmartObjectTransform path):
//     docks::TransformDialog dlg(this);
//     auto l = m_layerStack->at(smartIdx);
//     if (l) dlg.setInitial(l->transform);
//     if (dlg.exec() != QDialog::Accepted) return;
//     QTransform t = dlg.result();
//     applySmartObjectTransform(smartIdx, t);
//
// QTransform parse / compose conventions:
//   setInitial: read m11()/m12()/m21()/m22()/dx()/dy() into spinbox values
//               (scale is recovered as the geometric mean of |m11|/sqrt and
//               |m22|/sqrt when transforms are pure scale; rotation is the
//               atan2 of m21/m11).
//   result:     composed via QTransform().rotate(deg).scale(sx, sy) and then
//               written back through setMatrix() to keep dx/dy intact. This
//               keeps the matrix layout consistent with what P1.4.5 produced.

#include <QDialog>
#include <QTransform>

QT_BEGIN_NAMESPACE
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
QT_END_NAMESPACE

namespace docks {

class TransformDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TransformDialog(QWidget *parent = nullptr);
    ~TransformDialog() override;

    // Populate spinboxes from an existing transform. Accepts any QTransform
    // (identity is the most common starting point).
    void setInitial(const QTransform &t);

    // Computed result (only meaningful after exec() returns Accepted).
    QTransform result() const { return m_result; }

private slots:
    void onResetClicked();
    void onLinkToggled(bool linked);
    void onScaleXChanged(double v);
    void onScaleYChanged(double v);
    void onAccept();

private:
    void rebuildFromSpinboxes();

    QDoubleSpinBox *m_scaleX    = nullptr;
    QDoubleSpinBox *m_scaleY    = nullptr;
    QDoubleSpinBox *m_rotate    = nullptr;
    QDoubleSpinBox *m_translateX = nullptr;
    QDoubleSpinBox *m_translateY = nullptr;
    QCheckBox      *m_linkBox   = nullptr;   // link scaleX/Y toggle
    QPushButton    *m_resetBtn  = nullptr;

    bool m_scaleLinked = true;             // default: PS-style linked scale
    QTransform m_result;
};

} // namespace docks

#endif // TRANSFORMDIALOG_H
