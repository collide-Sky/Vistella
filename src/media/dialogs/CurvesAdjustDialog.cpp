// SPDX-License-Identifier: MIT
//
// CurvesAdjustDialog implementation - P0-3.2 v2 (2026-09-18)
//
#include "CurvesAdjustDialog.h"

#include "imagewindow/AdjustmentPanel.h"   // CurveEditor (defined inline there)

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointF>
#include <QPushButton>
#include <QVariantList>
#include <QVBoxLayout>

#include <algorithm>

namespace dialogs {

namespace {

// Map curve channel name -> list of QPointF (control space, 0..255).
// Used by both default reset and args deserialization.
QPolygonF defaultCurvePoints()
{
    return CurveEditor::defaultPoints();
}

// Serialize a QPolygonF into a QVariantList so it can ride inside QVariantMap.
QVariantList pointsToVariantList(const QPolygonF& pts)
{
    QVariantList list;
    list.reserve(pts.size());
    for (const QPointF& p : pts) {
        QVariantMap pm;
        pm.insert(QString("x"), p.x());
        pm.insert(QString("y"), p.y());
        list.append(pm);
    }
    return list;
}

// Parse a QVariantList (of {x,y} maps) back into a QPolygonF.
// Returns an empty polygon if the list is malformed.
QPolygonF variantListToPoints(const QVariant& v)
{
    QPolygonF pts;
    const QVariantList list = v.toList();
    if (list.isEmpty()) return pts;
    pts.reserve(list.size());
    for (const QVariant& item : list) {
        const QVariantMap pm = item.toMap();
        if (pm.isEmpty()) return QPolygonF();
        const double x = pm.value(QString("x"), 0.0).toDouble();
        const double y = pm.value(QString("y"), 0.0).toDouble();
        pts.append(QPointF(x, y));
    }
    return pts;
}

} // namespace

CurvesAdjustDialog::CurvesAdjustDialog(const QVariantMap& args, QWidget* parent)
    : AdjustDialogBase(QString("Curves (P0-3.2 v2)"), parent)
{
    setInitialArgs(args);
    // F-K (2026-09-09): base ctor does not call setupUi (avoids pure-virtual link
    //   ordering), derived ctor triggers init() to populate the body layout.
    init();

    // Restore initial args into UI (after setupUi wired signals).
    const QString chIn = args.value(QString("channel")).toString();
    if (!chIn.isEmpty()) {
        m_channel = chIn;
        const int idx = m_channelBox->findText(m_channel);
        if (idx >= 0) m_channelBox->setCurrentIndex(idx);
    } else {
        m_channelBox->setCurrentText(QString("RGB"));
    }
    if (args.contains(QString("points"))) {
        const QPolygonF pts = variantListToPoints(args.value(QString("points")));
        if (!pts.isEmpty()) {
            m_editor->setControlPoints(pts);
            m_controlPoints = pts;
        }
    }
}

AdjustDialogBase* CurvesAdjustDialog::create(const QVariantMap& args, QWidget* parent)
{
    return new CurvesAdjustDialog(args, parent);
}

void CurvesAdjustDialog::setupUi(QVBoxLayout* body)
{
    // Top row: channel selector + reset button
    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(6);

    auto* channelLabel = new QLabel(QString("Channel:"), this);
    m_channelBox = new QComboBox(this);
    m_channelBox->addItem(QString("RGB"));
    m_channelBox->addItem(QString("Red"));
    m_channelBox->addItem(QString("Green"));
    m_channelBox->addItem(QString("Blue"));
    m_channelBox->setCurrentText(QString("RGB"));

    m_resetBtn = new QPushButton(QString("Reset"), this);

    topRow->addWidget(channelLabel);
    topRow->addWidget(m_channelBox);
    topRow->addStretch(1);
    topRow->addWidget(m_resetBtn);

    body->addLayout(topRow);

    // Curve editor (centerpiece)
    m_editor = new CurveEditor(this);
    m_editor->setMinimumSize(256, 256);
    body->addWidget(m_editor, /*stretch*/ 1);

    // Wire signals
    connect(m_editor, &CurveEditor::pointsChanged,
            this, &CurvesAdjustDialog::onCurveChanged);
    connect(m_channelBox,
            qOverload<const QString&>(&QComboBox::currentTextChanged),
            this, [this](const QString& text) {
                m_channel = text;
                updateParam(QString("channel"), m_channel);
                triggerPreview();
            });
    connect(m_resetBtn, &QPushButton::clicked,
            this, &CurvesAdjustDialog::onResetClicked);
}

void CurvesAdjustDialog::onCurveChanged(const QPolygonF& pts)
{
    m_controlPoints = pts;
    updateParam(QString("points"), pointsToVariantList(pts));
    triggerPreview();
}

void CurvesAdjustDialog::onResetClicked()
{
    const QPolygonF def = defaultCurvePoints();
    m_editor->setControlPoints(def);
    m_controlPoints = def;
    updateParam(QString("points"), pointsToVariantList(def));
    triggerPreview();
}

void CurvesAdjustDialog::applyAdjust()
{
    // Default base impl: emits applied(m_args).
    AdjustDialogBase::applyAdjust();
}

} // namespace dialogs
