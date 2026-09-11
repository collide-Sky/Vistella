// SPDX-License-Identifier: MIT
//
// ImageOptionBar - F-E (2026-09-09)
//
// PS-style secondary toolbar above the canvas. 8 tool pages + 1 "no tool" page.
//   Index = static_cast<int>(ToolId) (None=0, Move=1, ..., Eyedropper=8)
//   F-E stage: 9 static placeholder pages (8 tools + 1 "no tool")
//   F-L stage: each ToolState::optionPage() returns a real widget, swapped in at attach time
//
// Wiring:
//   attach(ctx):
//     ctx->toolChanged -> setCurrentIndex(static_cast<int>(id))
//   detach():
//     disconnect ctx
//
#pragma once

#include <QWidget>
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

    void attach(ToolContext* ctx);
    void detach();

private slots:
    void onToolChanged(mediators::ToolId id);

private:
    Ui::ImageOptionBar* ui = nullptr;
    ToolContext* m_ctx = nullptr;  // weak ref
};

} // namespace tools
