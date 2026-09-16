#include "LiquifyDialog.h"

#include "FaceDetector.h"
#include "LiquifyBackingStore.h"
#include "LiquifyCanvas.h"
#include "LiquifyEngine.h"
#include "LiquifyFaceAware.h"
#include "LiquifyMesh.h"
#include "LiquifyOptionsPanel.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace filters::liquify {

LiquifyDialog::LiquifyDialog(const QImage& source, QWidget* parent)
    : QDialog(parent), m_source(source) {
    setWindowTitle(tr("Liquify"));
    setMinimumSize(640, 480);

    m_store = new LiquifyBackingStore;
    m_store->setSnapshot(source);
    m_engine = new LiquifyEngine;
    m_engine->setStore(m_store);

    // P1.2.7: face detector preloaded with OpenCV's bundled Haar cascades.
    //   v2: face + eye cascades for accurate eye anchor positions.
    m_faceDetector = new FaceDetector;
    m_faceDetector->loadDefaults();

    buildUi();
    wireSignals();

    m_canvas->rebuildCache();
}

LiquifyDialog::~LiquifyDialog() {
    delete m_engine;
    delete m_store;
    delete m_faceDetector;
}

QImage LiquifyDialog::resultImage() const {
    if (!m_engine || !m_engine->store()) return QImage();
    return m_engine->renderFull();
}

void LiquifyDialog::buildUi() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // Left: options panel
    m_panel = new LiquifyOptionsPanel(this);
    m_panel->setFixedWidth(260);
    root->addWidget(m_panel);

    // Right: canvas inside a scroll area
    auto* rightPane = new QVBoxLayout;
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignCenter);
    m_canvas = new LiquifyCanvas(m_engine, scroll);
    m_canvas->setFixedSize(m_source.size());
    scroll->setWidget(m_canvas);
    rightPane->addWidget(scroll, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                           | QDialogButtonBox::Cancel,
                                           this);
    rightPane->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* rightContainer = new QWidget(this);
    rightContainer->setLayout(rightPane);
    root->addWidget(rightContainer, 1);
}

void LiquifyDialog::wireSignals() {
    connect(m_panel, &LiquifyOptionsPanel::toolModeChanged,
            this, &LiquifyDialog::onToolModeChanged);
    connect(m_panel, &LiquifyOptionsPanel::brushSizeChanged,
            this, &LiquifyDialog::onBrushSizeChanged);
    connect(m_panel, &LiquifyOptionsPanel::brushPressureChanged,
            this, &LiquifyDialog::onBrushPressureChanged);
    connect(m_panel, &LiquifyOptionsPanel::meshSizeChanged,
            this, &LiquifyDialog::onMeshSizeChanged);
    connect(m_panel, &LiquifyOptionsPanel::showMeshChanged,
            this, &LiquifyDialog::onShowMeshChanged);
    connect(m_panel, &LiquifyOptionsPanel::showFrozenChanged,
            this, &LiquifyDialog::onShowFrozenChanged);
    connect(m_panel, &LiquifyOptionsPanel::resetRequested,
            this, &LiquifyDialog::onResetRequested);
    // P1.2.7: face-aware
    connect(m_panel, &LiquifyOptionsPanel::detectFacesRequested,
            this, &LiquifyDialog::onDetectFacesRequested);
    connect(m_panel, &LiquifyOptionsPanel::applyFaceAwareRequested,
            this, &LiquifyDialog::onApplyFaceAwareRequested);
}

void LiquifyDialog::onToolModeChanged(LiquifyToolMode mode) {
    Q_UNUSED(mode);
    // Tool mode currently only influences how the canvas / engine interpret
    // mouse strokes; for now the canvas stamps a default Forward Warp.
    // Real per-tool routing lives in a follow-up once a unified mouse
    // dispatcher is in place.
}

void LiquifyDialog::onBrushSizeChanged(int size) {
    Q_UNUSED(size);
    // Cached on the panel; consumed by the canvas the next time it stamps.
}

void LiquifyDialog::onBrushPressureChanged(qreal pressure) {
    Q_UNUSED(pressure);
}

void LiquifyDialog::onMeshSizeChanged(int size) {
    if (!m_store || !m_engine) return;
    m_store->rebuildMesh(size);
    m_canvas->rebuildCache();
}

void LiquifyDialog::onShowMeshChanged(bool show) {
    m_canvas->setShowMesh(show);
}

void LiquifyDialog::onShowFrozenChanged(bool show) {
    m_canvas->setShowFrozen(show);
}

void LiquifyDialog::onResetRequested() {
    if (!m_store || !m_engine) return;
    m_store->resetMesh();
    m_canvas->rebuildCache();
}

// P1.2.7: face-aware

void LiquifyDialog::onDetectFacesRequested() {
    if (!m_faceDetector || !m_faceDetector->isLoaded()) {
        // Detector unavailable; surface a status message via the dialog
        // title bar so the user knows the cascade is missing.
        setWindowTitle(tr("Liquify (face detector unavailable)"));
        return;
    }
    const QVector<Face> faces = m_faceDetector->detect(m_source);
    m_panel->setFaces(faces);
    setWindowTitle(tr("Liquify (%n face(s) detected)", "", faces.size()));
}

void LiquifyDialog::onApplyFaceAwareRequested(FaceSliders sliders,
                                              int faceIndex) {
    if (!m_store || faceIndex < 0) return;
    // Pull the selected face from the panel's stored list.
    const int idx = m_panel->selectedFaceIndex();
    if (idx < 0) return;
    // Re-detect so we have the live face data (the panel only stores the
    // bounding-box summary in its combobox labels).
    QVector<Face> faces;
    if (m_faceDetector && m_faceDetector->isLoaded()) {
        faces = m_faceDetector->detect(m_source);
    }
    if (idx >= faces.size()) return;
    const Face& face = faces[idx];

    LiquifyFaceAware::applyToMesh(m_store->mesh(), face, sliders);

    // Re-render the affected region.
    QRect dirty;
    const QImage piece = m_engine->renderDirty(dirty);
    if (!piece.isNull()) {
        // Force a full re-render to keep cache consistent (face-aware
        // touches many vertices across the image).
        m_canvas->rebuildCache();
    }
}

}  // namespace filters::liquify
