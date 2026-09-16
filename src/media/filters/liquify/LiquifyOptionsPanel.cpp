#include "LiquifyOptionsPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace filters::liquify {

LiquifyOptionsPanel::LiquifyOptionsPanel(QWidget* parent)
    : QWidget(parent) {
    buildUi();
    wireSignals();
}

void LiquifyOptionsPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ---- Tool row ----
    auto* toolRow = new QHBoxLayout;
    toolRow->setSpacing(4);
    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    struct ToolEntry { LiquifyToolMode mode; QString label; QString text; };
    const ToolEntry tools[] = {
        {LiquifyToolMode::ForwardWarp, tr("Forward Warp"), tr("W")},
        {LiquifyToolMode::Reconstruct, tr("Reconstruct"),   tr("R")},
        {LiquifyToolMode::Smooth,      tr("Smooth"),       tr("S")},
        {LiquifyToolMode::Twirl,       tr("Twirl"),        tr("C")},
        {LiquifyToolMode::Pucker,      tr("Pucker"),       tr("B")},
        {LiquifyToolMode::Bloat,       tr("Bloat"),        tr("V")},
    };
    auto* btn = makeToolButton(tools[0].mode, tools[0].text);
    btn->setChecked(true);
    m_toolGroup->addButton(btn, int(tools[0].mode));
    toolRow->addWidget(btn);
    for (size_t i = 1; i < sizeof(tools) / sizeof(tools[0]); ++i) {
        auto* b = makeToolButton(tools[i].mode, tools[i].text);
        m_toolGroup->addButton(b, int(tools[i].mode));
        toolRow->addWidget(b);
    }
    toolRow->addStretch(1);
    root->addLayout(toolRow);

    // ---- Brush Size ----
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("Brush Size:")));
    m_brushSize = new QSlider(Qt::Horizontal, this);
    m_brushSize->setRange(1, 500);
    m_brushSize->setValue(50);
    sizeRow->addWidget(m_brushSize, 1);
    m_brushSizeSpin = new QSpinBox(this);
    m_brushSizeSpin->setRange(1, 500);
    m_brushSizeSpin->setValue(50);
    sizeRow->addWidget(m_brushSizeSpin);
    root->addLayout(sizeRow);

    // ---- Brush Pressure ----
    auto* pressureRow = new QHBoxLayout;
    pressureRow->addWidget(new QLabel(tr("Pressure:")));
    m_brushPressure = new QSlider(Qt::Horizontal, this);
    m_brushPressure->setRange(0, 100);
    m_brushPressure->setValue(50);
    pressureRow->addWidget(m_brushPressure, 1);
    m_brushPressureSpin = new QSpinBox(this);
    m_brushPressureSpin->setRange(0, 100);
    m_brushPressureSpin->setValue(50);
    pressureRow->addWidget(m_brushPressureSpin);
    root->addLayout(pressureRow);

    // ---- Mesh Size ----
    auto* meshRow = new QHBoxLayout;
    meshRow->addWidget(new QLabel(tr("Mesh Size:")));
    m_meshSize = new QSlider(Qt::Horizontal, this);
    m_meshSize->setRange(2, 32);
    m_meshSize->setValue(5);
    meshRow->addWidget(m_meshSize, 1);
    m_meshSizeSpin = new QSpinBox(this);
    m_meshSizeSpin->setRange(2, 32);
    m_meshSizeSpin->setValue(5);
    meshRow->addWidget(m_meshSizeSpin);
    root->addLayout(meshRow);

    // ---- Overlays ----
    auto* overlayRow = new QHBoxLayout;
    m_showMesh = new QCheckBox(tr("Show Mesh"), this);
    m_showFrozen = new QCheckBox(tr("Show Frozen"), this);
    overlayRow->addWidget(m_showMesh);
    overlayRow->addWidget(m_showFrozen);
    overlayRow->addStretch(1);
    m_resetBtn = new QPushButton(tr("Reset"), this);
    overlayRow->addWidget(m_resetBtn);
    root->addLayout(overlayRow);

    // ---- Face-Aware Liquify (P1.2.7) ----
    auto* faceBox = new QGroupBox(tr("Face-Aware Liquify"), this);
    auto* faceLay = new QVBoxLayout(faceBox);
    auto* detectRow = new QHBoxLayout;
    m_detectFacesBtn = new QPushButton(tr("Detect Faces"), faceBox);
    m_faceSelector = new QComboBox(faceBox);
    m_faceSelector->setEnabled(false);
    detectRow->addWidget(m_detectFacesBtn);
    detectRow->addWidget(m_faceSelector, 1);
    faceLay->addLayout(detectRow);

    struct SliderEntry { QSlider** slider; const QString label; };
    auto addFaceSlider = [&](QVBoxLayout* lay, QSlider** s, const QString& label) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label, faceBox));
        *s = new QSlider(Qt::Horizontal, faceBox);
        (*s)->setRange(-100, 100);
        (*s)->setValue(0);
        row->addWidget(*s, 1);
        lay->addLayout(row);
    };
    addFaceSlider(faceLay, &m_eyeSize,    tr("Eye Size:"));
    addFaceSlider(faceLay, &m_noseSize,   tr("Nose Size:"));
    addFaceSlider(faceLay, &m_noseWidth,  tr("Nose Width:"));
    addFaceSlider(faceLay, &m_mouthSize,  tr("Mouth Size:"));
    addFaceSlider(faceLay, &m_mouthWidth, tr("Mouth Width:"));
    addFaceSlider(faceLay, &m_faceWidth,  tr("Face Width:"));

    m_applyFaceBtn = new QPushButton(tr("Apply"), faceBox);
    m_applyFaceBtn->setEnabled(false);
    faceLay->addWidget(m_applyFaceBtn);

    root->addWidget(faceBox);

    root->addStretch(1);
}

QPushButton* LiquifyOptionsPanel::makeToolButton(LiquifyToolMode mode,
                                                  const QString& label) {
    auto* b = new QPushButton(label, this);
    b->setCheckable(true);
    b->setFixedSize(32, 32);
    b->setToolTip(toolTipFor(mode));
    return b;
}

QString LiquifyOptionsPanel::toolTipFor(LiquifyToolMode mode) {
    switch (mode) {
        case LiquifyToolMode::ForwardWarp: return tr("Forward Warp (W)");
        case LiquifyToolMode::Reconstruct: return tr("Reconstruct (R)");
        case LiquifyToolMode::Smooth:      return tr("Smooth (S)");
        case LiquifyToolMode::Twirl:       return tr("Twirl (C)");
        case LiquifyToolMode::Pucker:      return tr("Pucker (B)");
        case LiquifyToolMode::Bloat:       return tr("Bloat (V)");
    }
    return QString();
}

void LiquifyOptionsPanel::wireSignals() {
    connect(m_toolGroup, &QButtonGroup::idClicked, this,
            [this](int id) { emit toolModeChanged(LiquifyToolMode(id)); });
    connect(m_brushSize, &QSlider::valueChanged, this, [this](int v) {
        m_brushSizeSpin->blockSignals(true);
        m_brushSizeSpin->setValue(v);
        m_brushSizeSpin->blockSignals(false);
        emit brushSizeChanged(v);
    });
    connect(m_brushSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) {
                m_brushSize->blockSignals(true);
                m_brushSize->setValue(v);
                m_brushSize->blockSignals(false);
                emit brushSizeChanged(v);
            });
    connect(m_brushPressure, &QSlider::valueChanged, this, [this](int v) {
        m_brushPressureSpin->blockSignals(true);
        m_brushPressureSpin->setValue(v);
        m_brushPressureSpin->blockSignals(false);
        emit brushPressureChanged(v / qreal(100));
    });
    connect(m_brushPressureSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) {
                m_brushPressure->blockSignals(true);
                m_brushPressure->setValue(v);
                m_brushPressure->blockSignals(false);
                emit brushPressureChanged(v / qreal(100));
            });
    connect(m_meshSize, &QSlider::valueChanged, this, [this](int v) {
        m_meshSizeSpin->blockSignals(true);
        m_meshSizeSpin->setValue(v);
        m_meshSizeSpin->blockSignals(false);
        emit meshSizeChanged(v);
    });
    connect(m_meshSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) {
                m_meshSize->blockSignals(true);
                m_meshSize->setValue(v);
                m_meshSize->blockSignals(false);
                emit meshSizeChanged(v);
            });
    connect(m_showMesh, &QCheckBox::toggled, this, &LiquifyOptionsPanel::showMeshChanged);
    connect(m_showFrozen, &QCheckBox::toggled, this, &LiquifyOptionsPanel::showFrozenChanged);
    connect(m_resetBtn, &QPushButton::clicked, this, &LiquifyOptionsPanel::resetRequested);

    // P1.2.7: face-aware
    connect(m_detectFacesBtn, &QPushButton::clicked, this,
            &LiquifyOptionsPanel::detectFacesRequested);
    connect(m_applyFaceBtn, &QPushButton::clicked, this, [this]() {
        emit applyFaceAwareRequested(faceSliders(), selectedFaceIndex());
    });
}

LiquifyToolMode LiquifyOptionsPanel::toolMode() const {
    return LiquifyToolMode(m_toolGroup->checkedId());
}

int LiquifyOptionsPanel::brushSize() const { return m_brushSize->value(); }
qreal LiquifyOptionsPanel::brushPressure() const {
    return m_brushPressure->value() / qreal(100);
}
int LiquifyOptionsPanel::meshSize() const { return m_meshSize->value(); }
bool LiquifyOptionsPanel::showMesh() const { return m_showMesh->isChecked(); }
bool LiquifyOptionsPanel::showFrozen() const { return m_showFrozen->isChecked(); }

// P1.2.7: face-aware accessors

FaceSliders LiquifyOptionsPanel::faceSliders() const {
    FaceSliders s;
    const auto toUnit = [](int v) { return qreal(v) / qreal(100); };
    s.eyeSize    = toUnit(m_eyeSize->value());
    s.noseSize   = toUnit(m_noseSize->value());
    s.noseWidth  = toUnit(m_noseWidth->value());
    s.mouthSize  = toUnit(m_mouthSize->value());
    s.mouthWidth = toUnit(m_mouthWidth->value());
    s.faceWidth  = toUnit(m_faceWidth->value());
    return s;
}

int LiquifyOptionsPanel::selectedFaceIndex() const {
    return m_faceSelector->currentIndex();
}

void LiquifyOptionsPanel::setFaces(const QVector<Face>& faces) {
    m_faces = faces;
    m_faceSelector->clear();
    if (faces.isEmpty()) {
        m_faceSelector->addItem(tr("(no faces)"), -1);
        m_faceSelector->setEnabled(false);
        m_applyFaceBtn->setEnabled(false);
        return;
    }
    for (int i = 0; i < faces.size(); ++i) {
        m_faceSelector->addItem(
            tr("Face %1  (%2, %3, %4 x %5)")
                .arg(i + 1)
                .arg(int(faces[i].bbox.x()))
                .arg(int(faces[i].bbox.y()))
                .arg(int(faces[i].bbox.width()))
                .arg(int(faces[i].bbox.height())),
            i);
    }
    m_faceSelector->setEnabled(true);
    m_applyFaceBtn->setEnabled(true);
}

}  // namespace filters::liquify
