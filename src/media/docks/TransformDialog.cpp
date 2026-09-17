// SPDX-License-Identifier: MIT
//
// TransformDialog implementation - P1.4.6 (2026-09-17)
//
// See TransformDialog.h for the design contract.

#include "TransformDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace docks {

TransformDialog::TransformDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QObject::tr("Transform"));
    setMinimumWidth(320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);

    // Scale X
    m_scaleX = new QDoubleSpinBox(this);
    m_scaleX->setObjectName(QStringLiteral("scaleX"));
    m_scaleX->setRange(0.1, 10.0);
    m_scaleX->setSingleStep(0.1);
    m_scaleX->setDecimals(2);
    m_scaleX->setValue(1.0);
    m_scaleX->setSuffix(QStringLiteral(" x"));
    connect(m_scaleX, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TransformDialog::onScaleXChanged);
    form->addRow(QObject::tr("Scale X"), m_scaleX);

    // Scale Y + Link
    auto *scaleYRow = new QHBoxLayout;
    m_scaleY = new QDoubleSpinBox(this);
    m_scaleY->setObjectName(QStringLiteral("scaleY"));
    m_scaleY->setRange(0.1, 10.0);
    m_scaleY->setSingleStep(0.1);
    m_scaleY->setDecimals(2);
    m_scaleY->setValue(1.0);
    m_scaleY->setSuffix(QStringLiteral(" x"));
    connect(m_scaleY, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TransformDialog::onScaleYChanged);
    m_linkBox = new QCheckBox(QObject::tr("Link"), this);
    m_linkBox->setObjectName(QStringLiteral("linkBox"));
    m_linkBox->setChecked(true);
    connect(m_linkBox, &QCheckBox::toggled,
            this, &TransformDialog::onLinkToggled);
    scaleYRow->addWidget(m_scaleY, /*stretch*/1);
    scaleYRow->addWidget(m_linkBox);
    form->addRow(QObject::tr("Scale Y"), scaleYRow);

    // Rotation
    m_rotate = new QDoubleSpinBox(this);
    m_rotate->setObjectName(QStringLiteral("rotate"));
    m_rotate->setRange(-360.0, 360.0);
    m_rotate->setSingleStep(1.0);
    m_rotate->setDecimals(1);
    m_rotate->setValue(0.0);
    m_rotate->setSuffix(QObject::tr(" \xc2\xb0"));
    form->addRow(QObject::tr("Rotation"), m_rotate);

    // Translate X
    m_translateX = new QDoubleSpinBox(this);
    m_translateX->setObjectName(QStringLiteral("translateX"));
    m_translateX->setRange(-10000.0, 10000.0);
    m_translateX->setSingleStep(1.0);
    m_translateX->setDecimals(1);
    m_translateX->setValue(0.0);
    m_translateX->setSuffix(QObject::tr(" px"));
    form->addRow(QObject::tr("Translate X"), m_translateX);

    // Translate Y
    m_translateY = new QDoubleSpinBox(this);
    m_translateY->setObjectName(QStringLiteral("translateY"));
    m_translateY->setRange(-10000.0, 10000.0);
    m_translateY->setSingleStep(1.0);
    m_translateY->setDecimals(1);
    m_translateY->setValue(0.0);
    m_translateY->setSuffix(QObject::tr(" px"));
    form->addRow(QObject::tr("Translate Y"), m_translateY);

    root->addLayout(form);

    // Reset + standard OK/Cancel row
    auto *btnRow = new QHBoxLayout;
    m_resetBtn = new QPushButton(QObject::tr("Reset"), this);
    connect(m_resetBtn, &QPushButton::clicked,
            this, &TransformDialog::onResetClicked);
    btnRow->addWidget(m_resetBtn);
    btnRow->addStretch(1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel,
                                         this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QObject::tr("OK"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancel"));
    connect(buttonBox, &QDialogButtonBox::accepted,
            this, &TransformDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    btnRow->addWidget(buttonBox);

    root->addLayout(btnRow);
}

TransformDialog::~TransformDialog() = default;

void TransformDialog::setInitial(const QTransform &t)
{
    if (!t.isIdentity()) {
        // Recover scale factors and rotation from the matrix produced by
        //   rebuildFromSpinboxes (rotate-then-scale). Qt's matrix layout:
        //     (x',y')^T = (m11*x + m21*y + dx, m12*x + m22*y + dy)
        //   For T = R*S with R = rotate(deg), S = scale(sx, sy):
        //     m11 = sx * cos(deg)
        //     m12 = sx * sin(deg)        (positive! Qt Y-down screen)
        //     m21 = -sy * sin(deg)
        //     m22 =  sy * cos(deg)
        //   Decoded:
        //     sx  = hypot(m11, m12)
        //     sy  = hypot(m21, m22)
        //     deg = atan2(m12, m11) * 180/M_PI
        const double sx  = std::hypot(t.m11(), t.m12());
        const double sy  = std::hypot(t.m21(), t.m22());
        const double deg = std::atan2(t.m12(), t.m11()) * 180.0 / M_PI;
        m_scaleX->blockSignals(true);
        m_scaleY->blockSignals(true);
        m_scaleX->setValue(sx);
        m_scaleY->setValue(sy);
        m_scaleX->blockSignals(false);
        m_scaleY->blockSignals(false);
        m_rotate->setValue(deg);
        m_translateX->setValue(t.dx());
        m_translateY->setValue(t.dy());
    } else {
        m_scaleX->setValue(1.0);
        m_scaleY->setValue(1.0);
        m_rotate->setValue(0.0);
        m_translateX->setValue(0.0);
        m_translateY->setValue(0.0);
    }
    rebuildFromSpinboxes();
}

void TransformDialog::onResetClicked()
{
    m_scaleX->setValue(1.0);
    m_scaleY->setValue(1.0);
    m_rotate->setValue(0.0);
    m_translateX->setValue(0.0);
    m_translateY->setValue(0.0);
}

void TransformDialog::onLinkToggled(bool linked)
{
    m_scaleLinked = linked;
    if (linked) {
        // One-way sync: snap scale Y to scale X on toggle on.
        m_scaleY->blockSignals(true);
        m_scaleY->setValue(m_scaleX->value());
        m_scaleY->blockSignals(false);
    }
}

void TransformDialog::onScaleXChanged(double v)
{
    if (m_scaleLinked) {
        m_scaleY->blockSignals(true);
        m_scaleY->setValue(v);
        m_scaleY->blockSignals(false);
    }
}

void TransformDialog::onScaleYChanged(double v)
{
    if (m_scaleLinked) {
        m_scaleX->blockSignals(true);
        m_scaleX->setValue(v);
        m_scaleX->blockSignals(false);
    }
}

void TransformDialog::onAccept()
{
    rebuildFromSpinboxes();
    accept();
}

void TransformDialog::rebuildFromSpinboxes()
{
    // Compose rotate-then-scale (PS-style: column-major order = T * R * S * P).
    //   ImageWindow::applySmartObjectTransform expects a full QTransform that
    //   it composes with any pre-existing layer transform.
    QTransform s;
    s.scale(m_scaleX->value(), m_scaleY->value());
    QTransform r;
    r.rotate(m_rotate->value());
    // QTransform::QTransform(qreal h11, qreal h12, qreal h21, qreal h22,
    //                        qreal dx, qreal dy) is the canonical 6-arg
    //                      affine constructor (matches Qt's m11/m12/m21/m22
    //                      field order). Use it instead of setMatrix
    //                      (which is the 9-arg 3x3 projective variant).
    const QTransform combined = r * s;
    m_result = QTransform(combined.m11(), combined.m12(),
                          combined.m21(), combined.m22(),
                          m_translateX->value(), m_translateY->value());
}

} // namespace docks
