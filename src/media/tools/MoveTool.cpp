// SPDX-License-Identifier: MIT
//
// MoveTool implementation - F-C (2026-09-09)
//
#include "MoveTool.h"

#include "../imagewindow.h"

#include <QStatusBar>

namespace tools {

void MoveTool::onEnter(ImageWindow* host)
{
    if (host && host->statusBar()) {
        host->statusBar()->showMessage(
            QStringLiteral("Move tool (V) - F-N will wire drag behavior"), 3000);
    }
}

void MoveTool::onExit(ImageWindow* host)
{
    if (host && host->statusBar()) {
        host->statusBar()->clearMessage();
    }
}

} // namespace tools
