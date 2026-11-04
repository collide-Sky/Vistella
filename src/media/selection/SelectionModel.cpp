// SPDX-License-Identifier: MIT
//
// SelectionModel implementation
//
#include "SelectionModel.h"
#include "logger.h"

#include <QGuiApplication>
#include <QPainter>
#include <QImageReader>
#include <algorithm>

namespace selection {

SelectionModel::SelectionModel(QObject* parent) : QObject(parent) {}

SelectionModel::~SelectionModel() = default;

void SelectionModel::ensureMaskAllocated()
{
    if (m_maskSize.isEmpty()) {
        m_mask = QImage();
        m_bbox = QRect();
        return;
    }
    if (m_mask.isNull() || m_mask.size() != m_maskSize) {
        m_mask = QImage(m_maskSize, QImage::Format_Alpha8);
        m_mask.fill(0);
    }
}

void SelectionModel::setSize(const QSize& size)
{
    if (m_maskSize == size) return;
    LOG_INFO("[SelectionModel] setSize: {}x{} -> {}x{}",
             m_maskSize.width(), m_maskSize.height(), size.width(), size.height());
    m_maskSize = size;
    ensureMaskAllocated();
    recomputeBbox();
    emit sizeChanged(size);
    emit changed();
}

void SelectionModel::clear()
{
    if (m_mask.isNull()) return;
    m_mask.fill(0);
    m_bbox = QRect();
    LOG_DEBUG("[SelectionModel] clear");
    emit changed();
}

void SelectionModel::selectAll()
{
    ensureMaskAllocated();
    if (m_mask.isNull()) return;
    m_mask.fill(255);
    m_bbox = QRect(QPoint(0, 0), m_maskSize);
    LOG_INFO("[SelectionModel] selectAll: {}x{}", m_maskSize.width(), m_maskSize.height());
    emit changed();
}

void SelectionModel::invert()
{
    ensureMaskAllocated();
    if (m_mask.isNull()) return;
    // invert: 0 -> 255, 255 -> 0 (per pixel)
    const int total = m_maskSize.width() * m_maskSize.height();
    if (total == 0) return;
    uchar* data = m_mask.bits();
    const int stride = m_mask.bytesPerLine();
    for (int y = 0; y < m_maskSize.height(); ++y) {
        uchar* row = data + y * stride;
        for (int x = 0; x < m_maskSize.width(); ++x) {
            row[x] = (row[x] == 0) ? 255 : 0;
        }
    }
    recomputeBbox();
    LOG_INFO("[SelectionModel] invert: bbox={}x{}",
             m_bbox.width(), m_bbox.height());
    emit changed();
}

void SelectionModel::setMask(const QImage& newMask, Mode mode)
{
    if (newMask.isNull()) return;
    ensureMaskAllocated();
    if (m_mask.isNull()) {
        // size unknown — adopt newMask size
        m_maskSize = newMask.size();
        m_mask = QImage(m_maskSize, QImage::Format_Alpha8);
        m_mask.fill(0);
    }
    if (newMask.size() != m_maskSize) {
        LOG_WARN("[SelectionModel] setMask size mismatch: newMask={}x{} model={}x{}",
                 newMask.size().width(), newMask.size().height(),
                 m_maskSize.width(), m_maskSize.height());
        return;
    }
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    uchar* dst = m_mask.bits();
    const int dstStride = m_mask.bytesPerLine();
    const uchar* src = newMask.constBits();
    const int srcStride = newMask.bytesPerLine();

    switch (mode) {
    case Mode::Replace:
        for (int y = 0; y < H; ++y) {
            std::memcpy(dst + y * dstStride, src + y * srcStride, W);
        }
        break;
    case Mode::Add:
        for (int y = 0; y < H; ++y) {
            uchar* d = dst + y * dstStride;
            const uchar* s = src + y * srcStride;
            for (int x = 0; x < W; ++x) {
                d[x] = std::max<uchar>(d[x], s[x]);
            }
        }
        break;
    case Mode::Subtract:
        for (int y = 0; y < H; ++y) {
            uchar* d = dst + y * dstStride;
            const uchar* s = src + y * srcStride;
            for (int x = 0; x < W; ++x) {
                // 0 stays 0; >0 subtracts: 255 - (255 - s[x])*d[x]/255 simplifies to
                //   d[x] = (d[x] * (255 - s[x])) / 255  (preserve 0 on s, kill on full s)
                d[x] = static_cast<uchar>((int(d[x]) * (255 - int(s[x]))) / 255);
            }
        }
        break;
    case Mode::Intersect:
        for (int y = 0; y < H; ++y) {
            uchar* d = dst + y * dstStride;
            const uchar* s = src + y * srcStride;
            for (int x = 0; x < W; ++x) {
                d[x] = std::min<uchar>(d[x], s[x]);
            }
        }
        break;
    }
    recomputeBbox();
    LOG_INFO("[SelectionModel] setMask: mode={} bbox={}x{}",
             static_cast<int>(mode), m_bbox.width(), m_bbox.height());
    emit changed();
}

bool SelectionModel::contains(const QPoint& p) const
{
    if (m_mask.isNull()) return false;
    if (!QRect(QPoint(0, 0), m_maskSize).contains(p)) return false;
    const uchar* data = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    return data[p.y() * stride + p.x()] != 0;
}

// The mask alpha at a pixel (0..255). 0 when the pixel is outside the
//   mask or outside the image. The paint clip uses this instead of
//   contains() so a feathered selection clips proportionally instead of
//   acting as a hard on/off switch.
int SelectionModel::alphaAt(const QPoint& p) const
{
    if (m_mask.isNull()) return 0;
    if (p.x() < 0 || p.y() < 0) return 0;
    if (p.x() >= m_maskSize.width() || p.y() >= m_maskSize.height()) return 0;
    const uchar* data = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    return data[p.y() * stride + p.x()];
}

// The outline of the selection, as straight segments in mask pixel
//   coordinates.
//
//   The shape is found by scanning every row of the mask for runs of
//   selected pixels. For each run the four boundary edges are emitted
//   where the neighbouring pixel is NOT selected:
//
//        (x0,y) ---- top edge (when row y-1 is empty here)
//          |        |
//        left      right      (when the pixel beside the run is empty)
//          |        |
//        (x0,y+1) - bottom edge (when row y+1 is empty here)
//
//   The result traces the real contour: a round selection gives a round
//   outline, not the bounding box.
QList<SelectionModel::Segment> SelectionModel::contourSegments() const
{
    QList<Segment> segments;
    if (m_mask.isNull() || m_maskSize.isEmpty()) return segments;

    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    const uchar* data = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();

    // selected(x, y) -- outside the mask counts as "not selected".
    auto selected = [data, stride, W, H](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return data[y * stride + x] != 0;
    };

    for (int y = 0; y < H; ++y) {
        int x = 0;
        while (x < W) {
            // Skip the empty part of the row.
            while (x < W && !selected(x, y)) ++x;
            if (x >= W) break;
            const int runStart = x;
            // Find the end of the run.
            while (x < W && selected(x, y)) ++x;
            const int runEnd = x;   // exclusive

            // Top edge: the row above does not cover this run.
            if (!selected(runStart, y - 1)) {
                segments.append(Segment{ QPointF(runStart, y), QPointF(runEnd, y) });
            }
            // Bottom edge: the row below does not cover this run.
            if (!selected(runStart, y + 1)) {
                segments.append(Segment{ QPointF(runStart, y + 1), QPointF(runEnd, y + 1) });
            }
            // Left edge: the pixel beside the run start is empty.
            if (!selected(runStart - 1, y)) {
                segments.append(Segment{ QPointF(runStart, y), QPointF(runStart, y + 1) });
            }
            // Right edge: the pixel beside the run end is empty.
            if (!selected(runEnd, y)) {
                segments.append(Segment{ QPointF(runEnd, y), QPointF(runEnd, y + 1) });
            }
        }
    }
    return segments;
}

// The persistent selection mode. One owner per document (see the header
//   comment): the mode survives a tool switch because the tool instance
//   does not.
void SelectionModel::setSelectionMode(Mode m)
{
    if (m_selectionMode == m) return;
    m_selectionMode = m;
    LOG_INFO("[SelectionModel] selectionMode -> {}", static_cast<int>(m));
    emit selectionModeChanged(m_selectionMode);
}

int SelectionModel::pixelCount() const
{
    if (m_mask.isNull()) return 0;
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    const uchar* data = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    int count = 0;
    for (int y = 0; y < H; ++y) {
        const uchar* row = data + y * stride;
        for (int x = 0; x < W; ++x) {
            if (row[x]) ++count;
        }
    }
    return count;
}

SelectionModel::Mode SelectionModel::modifierKeyToMode(Qt::KeyboardModifiers mods)
{
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt   = mods & Qt::AltModifier;
    if (shift && alt)  return Mode::Intersect;
    if (shift)         return Mode::Add;
    if (alt)           return Mode::Subtract;
    return Mode::Replace;
}

void SelectionModel::recomputeBbox()
{
    if (m_mask.isNull() || m_maskSize.isEmpty()) {
        m_bbox = QRect();
        return;
    }
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    const uchar* data = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();

    int minX = W, minY = H, maxX = -1, maxY = -1;
    bool any = false;
    for (int y = 0; y < H; ++y) {
        const uchar* row = data + y * stride;
        for (int x = 0; x < W; ++x) {
            if (row[x]) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
                any = true;
            }
        }
    }
    if (!any) {
        m_bbox = QRect();
    } else {
        // QRect(P1, P2) ctor: P2 is bottomRight inclusive -> size = maxX-minX+1
        m_bbox = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    }
}

// ===== PS select > Modify 5 项 + Transform =====
//   所有方法生成新 mask 应用,触发 changed + bbox recompute.

// feather: separable box blur of the alpha mask, keeping the grayscale ramp.
//   radiusPx: blur radius (PS: 0..250 pixels)
//   The blurred alpha is written back verbatim (0..255). Thresholding it
//   would throw the soft edge away and turn the feather back into a hard
//   edge -- that is what this function used to do.
void SelectionModel::feather(int radiusPx)
{
    ensureMaskAllocated();
    if (m_mask.isNull() || radiusPx <= 0) return;
    // QImage has no native gaussianBlur. A separable box blur over the
    //   alpha8 mask approximates a Gaussian well enough and needs no
    //   OpenCV. The float buffer holds the horizontal pass; the vertical
    //   pass writes the final 0..255 alpha straight into the mask.
    if (radiusPx < 1) radiusPx = 1;
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    if (W == 0 || H == 0) return;

    // Horizontal pass into a float buffer.
    std::vector<float> tmp(static_cast<size_t>(W) * H);
    const uchar* src = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    auto idx = [W](int x, int y) { return static_cast<size_t>(y) * W + x; };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int sum = 0, count = 0;
            for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
                int sx = x + dx;
                if (sx >= 0 && sx < W) {
                    sum += src[y * stride + sx];
                    ++count;
                }
            }
            tmp[idx(x, y)] = static_cast<float>(sum) / count;
        }
    }
    // Vertical pass -> m_mask. The blurred alpha is kept as grayscale so
    //   the selection edge stays soft (a feathered edge is a ramp of
    //   intermediate alpha values, not a hard 0/255 step).
    uchar* dst = m_mask.bits();
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float sum = 0;
            int count = 0;
            for (int dy = -radiusPx; dy <= radiusPx; ++dy) {
                int sy = y + dy;
                if (sy >= 0 && sy < H) {
                    sum += tmp[idx(x, sy)];
                    ++count;
                }
            }
            const float v = sum / count;
            const int a = static_cast<int>(v + 0.5f);
            dst[y * stride + x] = static_cast<uchar>(a < 0 ? 0 : (a > 255 ? 255 : a));
        }
    }
    recomputeBbox();
    LOG_INFO("[SelectionModel] feather: radius={}", radiusPx);
    emit changed();
}

// grow: dilation 半径 radiusPx (圆形 8 邻域)
//   PS: 选区扩展 1..100 像素
void SelectionModel::grow(int radiusPx)
{
    ensureMaskAllocated();
    if (m_mask.isNull() || radiusPx <= 0) return;
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    if (W == 0 || H == 0) return;

    std::vector<uchar> newMask(static_cast<size_t>(W) * H, 0);
    const uchar* src = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    const int r2 = radiusPx * radiusPx;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // 如果半径内有任一像素 >= 128, newMask = 128 (进选区)
            bool inside = false;
            for (int dy = -radiusPx; dy <= radiusPx && !inside; ++dy) {
                int sy = y + dy;
                if (sy < 0 || sy >= H) continue;
                for (int dx = -radiusPx; dx <= radiusPx && !inside; ++dx) {
                    int sx = x + dx;
                    if (sx < 0 || sx >= W) continue;
                    if (dx * dx + dy * dy > r2) continue;
                    if (src[sy * stride + sx] >= 128) {
                        inside = true;
                    }
                }
            }
            newMask[static_cast<size_t>(y) * W + x] = inside ? 255 : 0;
        }
    }
    std::memcpy(m_mask.bits(), newMask.data(), static_cast<size_t>(W) * H);
    recomputeBbox();
    LOG_INFO("[SelectionModel] grow: radius={}", radiusPx);
    emit changed();
}

// shrink: erosion 半径 radiusPx (像素完全在半径外才保留)
//   PS: 选区收缩 1..100 像素
void SelectionModel::shrink(int radiusPx)
{
    ensureMaskAllocated();
    if (m_mask.isNull() || radiusPx <= 0) return;
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    if (W == 0 || H == 0) return;

    std::vector<uchar> newMask(static_cast<size_t>(W) * H, 0);
    const uchar* src = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    const int r2 = radiusPx * radiusPx;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // 当前像素 + 半径内所有像素都得是 255, newMask 才 = 255
            bool inside = true;
            for (int dy = -radiusPx; dy <= radiusPx && inside; ++dy) {
                int sy = y + dy;
                if (sy < 0 || sy >= H) { inside = false; break; }
                for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
                    int sx = x + dx;
                    if (sx < 0 || sx >= W) { inside = false; break; }
                    if (dx * dx + dy * dy > r2) continue;
                    if (src[sy * stride + sx] < 128) { inside = false; }
                }
            }
            newMask[static_cast<size_t>(y) * W + x] = inside ? 255 : 0;
        }
    }
    std::memcpy(m_mask.bits(), newMask.data(), static_cast<size_t>(W) * H);
    recomputeBbox();
    LOG_INFO("[SelectionModel] shrink: radius={}", radiusPx);
    emit changed();
}

// smooth: median blur 半径 radiusPx (3x3 / 5x5) — 移除锯齿选区边
void SelectionModel::smooth(int radiusPx)
{
    ensureMaskAllocated();
    if (m_mask.isNull() || radiusPx <= 0) return;
    // radiusPx 离散化为 1 (3x3), 2 (5x5)
    const int half = (radiusPx <= 1) ? 1 : 2;
    const int W = m_maskSize.width();
    const int H = m_maskSize.height();
    if (W == 0 || H == 0) return;

    std::vector<uchar> newMask(static_cast<size_t>(W) * H, 0);
    const uchar* src = m_mask.constBits();
    const int stride = m_mask.bytesPerLine();
    // median 5x5: 取 25 个样本的中位数 (用 std::sort)
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::vector<uchar> samples;
            samples.reserve((2 * half + 1) * (2 * half + 1));
            for (int dy = -half; dy <= half; ++dy) {
                int sy = y + dy;
                if (sy < 0 || sy >= H) continue;
                for (int dx = -half; dx <= half; ++dx) {
                    int sx = x + dx;
                    if (sx < 0 || sx >= W) continue;
                    samples.push_back(src[sy * stride + sx]);
                }
            }
            std::sort(samples.begin(), samples.end());
            newMask[static_cast<size_t>(y) * W + x] = samples[samples.size() / 2];
        }
    }
    std::memcpy(m_mask.bits(), newMask.data(), static_cast<size_t>(W) * H);
    recomputeBbox();
    LOG_INFO("[SelectionModel] smooth: radius={}", radiusPx);
    emit changed();
}

// transform: QTransform 应用到 mask 像素坐标 (translate/scale/rotate)
void SelectionModel::transform(const QTransform& t)
{
    ensureMaskAllocated();
    if (m_mask.isNull()) return;
    // 1) 建新 mask (maskSize 大小, 全 0)
    QImage newMask(m_maskSize, QImage::Format_Alpha8);
    newMask.fill(0);
    // 2) 用 QPainter 把 m_mask 按 t.mapRect 应用到 newMask
    //   QPainter::setTransform 应用变换, drawImage 把当前 mask 按 matrix 绘制.
    QPainter p(&newMask);
    p.setTransform(t);
    p.drawImage(QPointF(0, 0), m_mask);
    p.end();
    // 3) 替换
    m_mask = newMask;
    recomputeBbox();
    LOG_INFO("[SelectionModel] transform: m11={} m12={} dx={}",
             t.m11(), t.m12(), t.dx());
    emit changed();
}

} // namespace selection
