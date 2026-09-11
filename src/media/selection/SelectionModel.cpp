// SPDX-License-Identifier: MIT
//
// SelectionModel implementation - P0-4.1 (2026-09-10)
//
#include "SelectionModel.h"
#include "logger.h"

#include <QGuiApplication>
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

} // namespace selection
