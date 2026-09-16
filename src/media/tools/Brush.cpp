// SPDX-License-Identifier: MIT
//
// Brush tool implementation - P1.1 (2026-09-15) Brush full implementation
//   Wires the BrushSettings/BrushEngine/StrokeSmoother into the tool state machine.
//   On mouse press: reset smoother, push undo baseline.
//   On mouse move:  smooth point, generate stamp, apply to m_current, render.
//   On mouse release: finalize.
//
//   Per-stroke pattern follows MosaicTool:
//     1. MousePress -> snapshot m_current as m_backup (undo source)
//     2. MouseMove (in stroke) -> paint stamps + track current
//     3. MouseRelease -> push ImageEditCommand(m_backup, current, "Brush stroke")
//
//   Spacing is applied as distance threshold between consecutive stamps
//   so the stroke appears continuous regardless of mouse speed.

#include "Brush.h"
#include "BrushOptionsPanel.h"
#include "logger.h"

#include "../imagewindow.h"          // ImageEditCommand + cv::Mat helpers
#include "../brushes/BrushEngine.h"
#include "../brushes/BrushPickerDialog.h"
#include "../brushes/BrushPreset.h"
#include "../brushes/StrokeSmoother.h"
#include "../brushes/PressureCurve.h"

#include <QColor>
#include <QCursor>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QVBoxLayout>
#include <QWidget>

namespace tools {

Brush::Brush(QWidget* /*parent*/)
    : ToolState()
    , m_preset(brushes::BrushPreset::makeBuiltInHardRound())
    , m_pressureCurve(new brushes::PressureCurve())
    , m_smoother(new brushes::StrokeSmoother())
    , m_paintColor(Qt::black)
{
    // Enable smoothing by default (PS default)
    m_smoother->setEnabled(true);
    m_smoother->setAmount(50);
    m_smoother->setRadius(5);
}

Brush::~Brush()
{
    delete m_pressureCurve;
    delete m_smoother;
}

void Brush::onEnter(ImageWindow* host)
{
    Q_UNUSED(host);
    m_active = false;
    m_lastStampDist = 0;
    LOG_DEBUG("[Brush] onEnter (P1.1 full impl)");
}

void Brush::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    if (m_active && host) {
        // Commit any pending stroke (e.g. when tool switched mid-stroke)
        finalizeStroke(host);
    }
    m_active = false;
    LOG_DEBUG("[Brush] onExit");
}

mediators::ToolId Brush::id() const { return mediators::ToolId::Brush; }

QString Brush::pageTitle() const { return QStringLiteral("Brush"); }

QCursor Brush::cursor() const
{
    return Qt::CrossCursor;
}

QWidget* Brush::optionPage(QWidget* parent)
{
    auto* panel = new BrushOptionsPanel(parent);
    panel->setPreset(m_preset);
    // Wire UI changes back to this Brush (setPreset updates m_preset + smoother).
    //   Brush does not inherit QObject (ToolState is non-QObject), so we use lambda
    //   with context object = panel (which is QObject). Qt routes via panel's lifetime.
    QObject::connect(panel, &BrushOptionsPanel::presetChanged, panel,
                     [this](const brushes::BrushPreset &p) { this->setPreset(p); });
    // Picker dialog: show modal BrushPickerDialog on "..." click
    QObject::connect(panel, &BrushOptionsPanel::pickerRequested, panel, [this, panel]() {
        brushes::BrushPickerDialog dlg(nullptr, panel);
        // Two-way: when user double-clicks a preset, the dialog emits presetSelected
        //   and accepts. We also accept() in the dialog; this lambda runs after exec()
        //   so selectedPreset() returns the chosen one.
        if (dlg.exec() == QDialog::Accepted) {
            const auto p = dlg.selectedPreset();
            if (!p.name.isEmpty()) {
                this->setPreset(p);
                panel->setPreset(p);
            }
        }
    });
    return panel;
}

// ===== Active brush config =====

void Brush::setPreset(const brushes::BrushPreset &p)
{
    m_preset = p;
    m_smoother->setAmount(p.dynamics.smoothing.amount);
    m_smoother->setRadius(p.dynamics.smoothing.radius);
    m_smoother->setEnabled(p.dynamics.smoothing.enabled);
}

void Brush::setPaintColor(const QColor &c) { m_paintColor = c; }
QColor Brush::paintColor() const { return m_paintColor; }

// ===== Mouse event handlers =====

void Brush::onMousePress(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    if (!host) return;
    // Snapshot current image for undo (will push on release)
    cv::Mat& cur = host->currentImage();
    if (cur.empty()) {
        LOG_WARN("[Brush] mouse press on empty image, ignore");
        return;
    }
    m_backup = cur.clone();
    m_active = true;

    m_smoother->reset();
    m_smoother->addPoint(scenePos, 1.0);
    m_lastStampScene = scenePos;
    m_lastStampDist  = 0.0;
    // First stamp placed immediately
    paintSingleStamp(host, scenePos, 1.0);
    host->renderToViewPublic();
}

void Brush::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_active || !host) return;

    m_smoother->addPoint(scenePos, 1.0);
    const QPointF smoothed = m_smoother->smoothed();

    // Spacing: stamp when cumulative distance > diameter * spacing / 100
    const double dx = smoothed.x() - m_lastStampScene.x();
    const double dy = smoothed.y() - m_lastStampScene.y();
    const double step = std::sqrt(dx*dx + dy*dy);
    m_lastStampDist += step;

    const int diameter = m_preset.settings.size;
    const double threshold = std::max(1.0, double(diameter) * m_preset.settings.spacing / 100.0);

    if (m_lastStampDist >= threshold) {
        // Interpolate from last stamp to current to fill the gap (multiple stamps)
        const double stamps = m_lastStampDist / threshold;
        const QPointF last  = m_lastStampScene;
        const QPointF cur   = smoothed;
        for (int i = 1; i <= int(stamps); ++i) {
            const double t = i / stamps;
            QPointF p(last.x() + (cur.x() - last.x()) * t,
                      last.y() + (cur.y() - last.y()) * t);
            paintSingleStamp(host, p, 1.0);
        }
        m_lastStampScene = cur;
        m_lastStampDist = 0;
        host->renderToViewPublic();
    }
}

void Brush::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& /*scenePos*/)
{
    if (!m_active || !host) return;
    finalizeStroke(host);
}

void Brush::paintSingleStamp(ImageWindow* host, const QPointF& scenePos, double pressure)
{
    if (!host) return;
    cv::Mat& target = host->currentImage();
    if (target.empty()) return;

    // Resolve final per-step parameters (size jitter + opacity/flow jitter + pressure)
    const brushes::BrushEngine::ResolvedParams rp =
        brushes::BrushEngine::resolve(m_preset.settings, m_preset.shape, m_preset.dynamics,
                                      pressure, 1.0, 0.0);

    // Use the diameter from ResolvedParams (already pressure-scaled)
    brushes::BrushSettings s = m_preset.settings;
    s.size = rp.diameter;
    // Apply resolved opacity/flow to the applyStamp call
    const double opacity = std::clamp(rp.opacity, 0.0, 1.0);
    const double flow    = std::clamp(rp.flow, 0.0, 1.0);

    // Generate stamp
    auto stamp = brushes::BrushEngine::generateStamp(s, m_preset.shape, pressure);

    // Apply texture (if configured)
    if (m_preset.dynamics.texture.texturePath.isEmpty()) {
        // No texture, but we still may want dual brush
    } else {
        // Future: load pattern from texturePath and apply
        // (texture path resolution deferred to P1.1 follow-up)
    }

    // Apply dual brush (if configured)
    if (m_preset.dynamics.dual.enabled) {
        brushes::BrushSettings dualS = m_preset.settings;
        dualS.size = std::max(1, int(rp.diameter * m_preset.dynamics.dual.size / 100.0));
        auto dual = brushes::BrushEngine::generateStamp(dualS, m_preset.shape, pressure);
        stamp.stamp = brushes::BrushEngine::applyDual(stamp.stamp, dual.stamp, m_preset.dynamics.dual);
    }

    // Paint stamp onto m_current
    const int ix = int(std::round(scenePos.x()));
    const int iy = int(std::round(scenePos.y()));
    brushes::BrushEngine::applyStamp(target, cv::Point(ix, iy), stamp, m_paintColor, opacity, flow);
}

void Brush::finalizeStroke(ImageWindow* host)
{
    if (!m_active || !host) return;
    m_active = false;

    // Push undo command: imageBefore=m_backup, imageAfter=m_current
    cv::Mat& cur = host->currentImage();
    if (!m_backup.empty() && cur.size() == m_backup.size() && cur.type() == m_backup.type()) {
        auto* cmd = new ImageEditCommand(host, m_backup, cur, QStringLiteral("Brush stroke"));
        if (host->undoStack()) {
            host->undoStack()->push(cmd);
        } else {
            delete cmd;
        }
    }
    m_backup.release();
    host->renderToViewPublic();
}

} // namespace tools