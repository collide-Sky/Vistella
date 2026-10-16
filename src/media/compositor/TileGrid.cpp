// SPDX-License-Identifier: MIT
//
// TileGrid impl - P8.1 (2026-10-16)

#include "TileGrid.h"

namespace compositor {

TileGrid::TileGrid(int imgW, int imgH, int tileSize)
    : m_imgW(imgW), m_imgH(imgH), m_tileSize(tileSize > 0 ? tileSize : kDefaultTileSize)
{
    m_cols = (m_imgW + m_tileSize - 1) / m_tileSize;   // ceil div
    m_rows = (m_imgH + m_tileSize - 1) / m_tileSize;
    if (m_cols < 1) m_cols = 1;
    if (m_rows < 1) m_rows = 1;
}

cv::Rect TileGrid::tileRect(int tx, int ty) const
{
    const int x = tx * m_tileSize;
    const int y = ty * m_tileSize;
    int w = m_tileSize;
    int h = m_tileSize;
    if (x + w > m_imgW) w = m_imgW - x;
    if (y + h > m_imgH) h = m_imgH - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return cv::Rect(x, y, w, h);
}

QList<QPair<int,int>> TileGrid::allTiles() const
{
    QList<QPair<int,int>> r;
    r.reserve(m_cols * m_rows);
    for (int ty = 0; ty < m_rows; ++ty) {
        for (int tx = 0; tx < m_cols; ++tx) {
            r.append({tx, ty});
        }
    }
    return r;
}

} // namespace compositor
