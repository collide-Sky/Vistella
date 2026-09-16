#pragma once

#include <QDialog>
#include <QImage>

#include "LiquifyFaceAware.h"
#include "LiquifyMesh.h"

namespace filters::liquify {

class FaceDetector;
class LiquifyBackingStore;
class LiquifyEngine;
class LiquifyOptionsPanel;
class LiquifyCanvas;

// Modal dialog that hosts the Liquify preview + options.
//
// Usage:
//   LiquifyDialog dlg(sourceImage, parent);
//   if (dlg.exec() == QDialog::Accepted) {
//       const QImage warped = dlg.resultImage();
//       // apply warped to current layer / undo stack
//   }
//
// On entry the dialog clones the source into a backing store and renders an
// initial preview. As the user paints, the engine incrementally re-renders
// the dirty region. On OK the final warped image is captured; on Cancel the
// source is dropped.
class LiquifyDialog : public QDialog {
    Q_OBJECT
public:
    LiquifyDialog(const QImage& source, QWidget* parent = nullptr);
    ~LiquifyDialog() override;

    QImage resultImage() const;

private slots:
    void onToolModeChanged(LiquifyToolMode mode);
    void onBrushSizeChanged(int size);
    void onBrushPressureChanged(qreal pressure);
    void onMeshSizeChanged(int size);
    void onShowMeshChanged(bool show);
    void onShowFrozenChanged(bool show);
    void onResetRequested();
    // P1.2.7: face-aware
    void onDetectFacesRequested();
    void onApplyFaceAwareRequested(FaceSliders sliders, int faceIndex);

private:
    void buildUi();
    void wireSignals();

    QImage m_source;
    QImage m_result;
    LiquifyBackingStore* m_store = nullptr;
    LiquifyEngine* m_engine = nullptr;
    LiquifyOptionsPanel* m_panel = nullptr;
    LiquifyCanvas* m_canvas = nullptr;
    FaceDetector* m_faceDetector = nullptr;
};

}  // namespace filters::liquify
