// SPDX-License-Identifier: MIT
//
// TileGrid + TileKey - P8.1 (2026-10-16) incremental compositor
//   TileGrid: image-space decomposition into (tx, ty) tiles, 256x256 default.
//   TileKey:  composite identity (nodeId, tx, ty, version) for cache lookup.
//
//   P8.1 commit (2026-10-16). P8.2 GPU backend reads TileKey the same way
//   (P8.2 just changes what the cache contains - a GPU texture or a
//   command-buffer), and P8.4 parallel executor fans tiles across threads
//   using the same grid.

#pragma once

#include <QList>
#include <QPair>
#include <opencv2/core.hpp>

#include "CompositorNode.h"   // CompositorNode::NodeId

namespace compositor {

// 256x256 default. PS / GIMP / Krita common choice. P8.2 / P8.4 read this
// constant; tests can override via TileGrid(imgW, imgH, customTileSize).
inline constexpr int kDefaultTileSize = 256;

// Identifies one (node, tile, version) cache slot. The version is the
// node's "fingerprint at the time the slot was written"; lookup requires
// the same fingerprint. markDirty() bumps the version which causes all
// downstream tile slots to miss on next lookup.
struct TileKey {
    CompositorNode::NodeId nodeId = 0;
    int                    tileX  = 0;
    int                    tileY  = 0;
    quint64                version = 0;   // 0 = invalid (cache miss sentinel)

    bool isValid() const { return version != 0; }
    bool operator==(const TileKey& o) const {
        return nodeId == o.nodeId && tileX == o.tileX
            && tileY == o.tileY && version == o.version;
    }
};

inline uint qHash(const TileKey& k, uint seed = 0) {
    // Mix all 4 fields; xorshift is enough for cache lookup (no security need).
    quint64 h = seed;
    h ^= static_cast<quint64>(k.nodeId) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h ^= static_cast<quint64>(k.tileX)  + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h ^= static_cast<quint64>(k.tileY)  + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h ^= k.version                     + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return static_cast<uint>(h ^ (h >> 32));
}

class TileGrid {
public:
    TileGrid() = default;
    TileGrid(int imgW, int imgH, int tileSize = kDefaultTileSize);

    int  cols()        const { return m_cols; }
    int  rows()        const { return m_rows; }
    int  totalTiles()  const { return m_cols * m_rows; }
    int  tileSize()    const { return m_tileSize; }
    int  imageWidth()  const { return m_imgW; }
    int  imageHeight() const { return m_imgH; }

    // Image-space rect (x, y, w, h) for tile (tx, ty). Right/bottom edges
    // are clamped to the image boundary; partial tiles are normal.
    cv::Rect tileRect(int tx, int ty) const;

    // All (tx, ty) in row-major order: ty * cols + tx.
    QList<QPair<int,int>> allTiles() const;

    bool isValidTile(int tx, int ty) const {
        return tx >= 0 && tx < m_cols && ty >= 0 && ty < m_rows;
    }

private:
    int m_imgW     = 0;
    int m_imgH     = 0;
    int m_tileSize = kDefaultTileSize;
    int m_cols     = 0;
    int m_rows     = 0;
};

} // namespace compositor
