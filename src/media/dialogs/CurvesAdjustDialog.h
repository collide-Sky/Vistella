// SPDX-License-Identifier: MIT
//
// CurvesAdjustDialog - P0-3.2 v2 (2026-09-18)
//
// PS-style Curves dialog: reuses the existing CurveEditor widget
// (declared in imagewindow/AdjustmentPanel.h) together with a
// channel QComboBox (RGB/Red/Green/Blue) and a Reset button.
//
// Args format on applied():
//   {
//     "channel": QString ("RGB" | "Red" | "Green" | "Blue"),
//     "points":  QVariantList of QPointF (control points)
//   }
//
#pragma once

#include "AdjustDialogBase.h"

#include <QPolygonF>

class CurveEditor;
class QComboBox;
class QPushButton;
class QVBoxLayout;

namespace dialogs {

class CurvesAdjustDialog : public AdjustDialogBase
{
    Q_OBJECT
public:
    explicit CurvesAdjustDialog(const QVariantMap& args, QWidget* parent = nullptr);
    ~CurvesAdjustDialog() = default;

    // DialogFactory entry point
    static AdjustDialogBase* create(const QVariantMap& args, QWidget* parent);

protected:
    void setupUi(QVBoxLayout* body);
    void applyAdjust();

private slots:
    void onCurveChanged(const QPolygonF& pts);
    void onResetClicked();

private:
    CurveEditor*  m_editor      = nullptr;
    QComboBox*    m_channelBox  = nullptr;
    QPushButton*  m_resetBtn    = nullptr;
    QString       m_channel     = QString("RGB");
    QPolygonF     m_controlPoints;
};

} // namespace dialogs
