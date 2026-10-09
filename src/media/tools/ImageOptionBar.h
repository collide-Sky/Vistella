// SPDX-License-Identifier: MIT
//
// ImageOptionBar - F-E (2026-09-09) + P5.1 (2026-10-08)
//
// PS-style secondary toolbar above the canvas. Pages indexed by
// mediators::ToolId (None=0..MaskBrush=16) plus two P5.1 dynamic pages:
//   Page index 17 (kMosaicPageIdx): MosaicOptionPanel — shown when active
//                                    tool is Brush (ToolId::Brush = 7) and
//                                    a MosaicTool instance is registered via
//                                    attach(). Replaces Brush's default
//                                    BrushOptionsPanel so the user can toggle
//                                    mosaic mode + size + type from one place.
//   Page index 18 (kTextPageIdx):   TextOptionPanel — shown when active tool
//                                    is Text (ToolId::Text = 6) and a
//                                    TextOverlayController is registered.
//
// Architecture note:
//   F-E stage: 9 static placeholder pages (None + 8 tools)
//   F-L stage: ToolState::optionPage() swaps in real widget per tool
//   Q4.2.1:    MaskBrush (idx=16) joins Transform (idx=9) on dynamic addPage
//   P5.1:      Mosaic (idx=17) and Text (idx=18) join on dynamic addPage —
//              promoted out of the Q4.2.2.1 leftPanel gridLayout_4
//              hard-coding so the option panel follows active tool like
//              every other tool (no more "always-on" Mosaic+Text block).
//
// Wiring:
//   attach(ctx, mosaic, textCtrl):
//     ctx->toolChanged  -> onToolChanged (routes Brush->Mosaic, Text->Text)
//     mosaic->modeChanged -> onMosaicModeChanged (re-route when mosaic toggles
//                            while user is on Brush tool)
//     textCtrl  held as weak ref for page ownership only
//   detach():
//     disconnect everything
//
#pragma once

#include <QWidget>

#include "../imagewindow/MosaicTool.h"
#include "../imagewindow/TextOverlayController.h"
#include "../mediators/ToolMediator.h"

namespace Ui { class ImageOptionBar; }

namespace tools {

class ToolContext;

class ImageOptionBar : public QWidget
{
    Q_OBJECT
public:
    explicit ImageOptionBar(QWidget* parent = nullptr);
    ~ImageOptionBar() override;

    // P5.1 (2026-10-08): attach with Mosaic + TextOverlay deps.
    //   Both are optional; nullptr disables the corresponding route.
    //   Without deps, Mosaic/Text pages never show (graceful no-op).
    void attach(ToolContext* ctx,
                MosaicTool* mosaic = nullptr,
                TextOverlayController* textCtrl = nullptr);
    void detach();

    // P5.1: actual idx of the dynamic Mosaic / Text page once added
    //   (>= 0 means added). -1 means not yet added.
    int mosaicPageIdx() const { return m_mosaicPageIdx; }
    int textPageIdx()   const { return m_textPageIdx;   }

private slots:
    void onToolChanged(mediators::ToolId id);
    void onMosaicModeChanged(bool on);

private:
    // P5.1 helper: lazily register MosaicOptionPanel / TextOptionPanel as
    //   stackedWidget children and remember the slot they own.
    void ensureMosaicPage();
    void ensureTextPage();

    Ui::ImageOptionBar* ui = nullptr;
    ToolContext* m_ctx = nullptr;       // weak ref
    MosaicTool* m_mosaic = nullptr;     // weak ref, optional
    TextOverlayController* m_textCtrl = nullptr;  // weak ref, optional
    int m_mosaicPageIdx = -1;  // P5.1: actual idx after addWidget, -1 = not added
    int m_textPageIdx   = -1;  // P5.1: actual idx after addWidget, -1 = not added
};

} // namespace tools
