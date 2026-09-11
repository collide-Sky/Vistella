// SPDX-License-Identifier: MIT
//
// SelectionStrategy - P0-4.2 (2026-09-10)
//
// Strategy pattern: each selection tool delegates the actual mask computation
// to a concrete SelectionStrategy. The tool drives begin/update/end; the
// strategy returns a 1-channel QImage (Format_Alpha8, 0=out, 255=in).
//
// Concrete strategies:
//   RectSelectionStrategy       — drag rectangle
//   LassoSelectionStrategy      — free-form polygon (close on release)
//   MagicWandSelectionStrategy  — flood fill from seed pixel with tolerance
//   ColorRangeSelectionStrategy — P1 stub (returns empty mask)
//
// Used by tools (RectSelect / Lasso / MagicWand) and SelectionCommand (for
// Select All / Deselect / Inverse built-in modes).
//
#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>

namespace selection {

class SelectionStrategy
{
public:
    enum class Kind {
        Rect        = 0,
        Lasso       = 1,
        MagicWand   = 2,
        ColorRange  = 3,
    };

    virtual ~SelectionStrategy() = default;
    virtual Kind kind() const = 0;

    // User gesture lifecycle (called by ToolState onMouse* overrides)
    virtual void begin(const QPointF& p) = 0;        // MouseButtonPress
    virtual void update(const QPointF& p) = 0;       // MouseMove
    // MouseButtonRelease — returns the new 1-channel mask (Format_Alpha8)
    //   image param: current image (BGRA), used by MagicWand flood fill
    //   image.size() == output mask size
    virtual QImage end(const QImage& image) = 0;

    virtual void cancel() = 0;
};

// ===== Rect: drag rectangle =====
class RectSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::Rect; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

private:
    bool    m_active = false;
    QPointF m_p0;
    QPointF m_p1;
};

// ===== Lasso: free polygon =====
class LassoSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::Lasso; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

    // For tests / live preview
    const QPolygonF& path() const { return m_path; }

private:
    QPolygonF m_path;
    bool      m_active = false;
};

// ===== MagicWand: flood fill =====
class MagicWandSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::MagicWand; }

    void setTolerance(int t) { m_tolerance = std::clamp(t, 0, 255); }
    int  tolerance() const { return m_tolerance; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

private:
    bool    m_active = false;
    QPointF m_seed;
    int     m_tolerance = 32;
};

// ===== ColorRange: P1 placeholder (returns empty mask) =====
class ColorRangeSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::ColorRange; }
    void begin(const QPointF&) override {}
    void update(const QPointF&) override {}
    QImage end(const QImage& image) override;
    void cancel() override {}
};

} // namespace selection
