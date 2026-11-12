// SPDX-License-Identifier: MIT
//
// SelectionStrategy
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
        Ellipse     = 4,
        Polygon     = 5,
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

    // feathering radius (0 = none, max 50px)
    void setFeather(int r) { m_featherRadius = std::clamp(r, 0, 50); }
    int  feather() const { return m_featherRadius; }

    // anti-alias on path boundary
    void setAntiAlias(bool b) { m_antiAlias = b; }
    bool antiAlias() const { return m_antiAlias; }

    // For tests / live preview
    const QPolygonF& path() const { return m_path; }

private:
    QPolygonF m_path;
    bool      m_active = false;
    int       m_featherRadius = 0;
    bool      m_antiAlias = true;
};

// ===== MagicWand: flood fill =====
class MagicWandSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::MagicWand; }

    void setTolerance(int t) { m_tolerance = std::clamp(t, 0, 255); }
    int  tolerance() const { return m_tolerance; }

    // contiguous switch (PS standard)
    //   true  (default): BFS 4-neighborhood, only connected same-color region
    //   false:           full image scan, all pixels within tolerance
    void setContiguous(bool c) { m_contiguous = c; }
    bool contiguous() const { return m_contiguous; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

private:
    bool    m_active = false;
    QPointF m_seed;
    int     m_tolerance = 32;
    bool    m_contiguous = true;
};

// ===== ColorRange: PS-style sample-points + HSV distance =====
class ColorRangeSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::ColorRange; }

    // sample points (image-pixel coords), fuzziness (0..255)
    //   invert flag — all driven by optionPage UI.
    void setSamplePoints(const QVector<QPointF>& pts);
    void setFuzziness(int f) { m_fuzziness = std::clamp(f, 0, 255); }
    void setInvert(bool b)   { m_invert = b; }
    int  fuzziness() const  { return m_fuzziness; }
    bool invert() const     { return m_invert; }
    const QVector<QPointF>& samplePoints() const { return m_samplePoints; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

private:
    QVector<QPointF> m_samplePoints;
    int  m_fuzziness = 30;
    bool m_invert = false;
};

// ===== Ellipse: drag an ellipse =====
//
//   The Photoshop elliptical marquee: press, drag, release. The
//   ellipse is inscribed in the dragged rectangle (the corners of the
//   rectangle are OUTSIDE the ellipse -- that is what makes it an
//   ellipse marquee and not a rounded rectangle).
class EllipseSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::Ellipse; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

    // Feathering radius (0 = none, max 50px).
    void setFeather(int r) { m_featherRadius = std::clamp(r, 0, 50); }
    int  feather() const { return m_featherRadius; }

    // Anti-alias on the path boundary.
    void setAntiAlias(bool b) { m_antiAlias = b; }
    bool antiAlias() const { return m_antiAlias; }

    // For tests / live preview.
    QRectF bounds() const { return QRectF(m_p0, m_p1).normalized(); }

private:
    bool    m_active = false;
    QPointF m_p0;
    QPointF m_p1;
    int     m_featherRadius = 0;
    bool    m_antiAlias = true;
};

// ===== Polygon: click the vertices, double-click closes =====
//
//   The Photoshop polygonal marquee: every click adds a vertex, the
//   double click closes the polygon and commits the selection. Unlike
//   the lasso (which follows the mouse continuously) the polygon has
//   DISCRETE vertices, so the gesture is click-click-click-doubleclick.
class PolygonSelectionStrategy : public SelectionStrategy
{
public:
    Kind kind() const override { return Kind::Polygon; }

    void begin(const QPointF& p) override;
    void update(const QPointF& p) override;
    QImage end(const QImage& image) override;
    void cancel() override;

    // Add a vertex explicitly (the click gesture).
    void addVertex(const QPointF& p);

    // Close the polygon and commit it.
    //
    //   Returns false when there are fewer than 3 vertices -- a
    //   2-vertex "polygon" is a line and cannot be a selection.
    bool close();

    bool isClosed() const { return m_closed; }
    int  vertexCount() const { return static_cast<int>(m_path.size()); }
    const QPolygonF& path() const { return m_path; }

    // Feathering radius (0 = none, max 50px).
    void setFeather(int r) { m_featherRadius = std::clamp(r, 0, 50); }
    int  feather() const { return m_featherRadius; }

    // Anti-alias on the path boundary.
    void setAntiAlias(bool b) { m_antiAlias = b; }
    bool antiAlias() const { return m_antiAlias; }

private:
    QPolygonF m_path;
    bool      m_active = false;
    bool      m_closed = false;
    int       m_featherRadius = 0;
    bool      m_antiAlias = true;
};

} // namespace selection
