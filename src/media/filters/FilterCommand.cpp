// SPDX-License-Identifier: MIT
//
// FilterCommand implementation
//
#include "FilterCommand.h"
#include "../imagewindow.h"
#include "FilterFactory.h"
#include "logger.h"

namespace filter {

namespace {

//  取活动图层的像素 + 图层索引.
//  没有可写的 Bitmap 图层时返回 false —— 滤镜没有落点, 命令不做事.
bool grabActiveLayer(ImageWindow* host, int& layerIndex, cv::Mat& pixels)
{
    layerIndex = -1;
    if (!host) return false;
    layerIndex = host->activeLayerIndex();
    cv::Mat* layer = host->activeLayerImage();
    if (!layer || layer->empty()) return false;
    pixels = layer->clone();
    return true;
}

} // namespace

FilterCommand::FilterCommand(ImageWindow* host, FilterKind kind,
                             const QString& text, QUndoCommand* parent)
    : QUndoCommand(parent), m_host(host), m_kind(kind)
{
    setText(text);
    cv::Mat before;
    if (!grabActiveLayer(host, m_layerIndex, before)) return;
    m_before = before;
    auto strategy = FilterFactory::createFilter(kind);
    if (strategy && !m_before.empty()) {
        strategy->apply(m_before, m_after);
    } else {
        m_after = m_before.clone();
    }
}

FilterCommand::FilterCommand(ImageWindow* host, FilterStrategy* strategy,
                             const QString& text, QUndoCommand* parent)
    : QUndoCommand(parent), m_host(host),
      m_kind(strategy ? strategy->kind() : FilterKind::FilterGallery)
{
    setText(text);
    cv::Mat before;
    if (!grabActiveLayer(host, m_layerIndex, before)) return;
    m_before = before;
    if (strategy && !m_before.empty()) {
        strategy->apply(m_before, m_after);
    } else {
        m_after = m_before.clone();
    }
}

void FilterCommand::applyToLayer(const cv::Mat& pixels)
{
    if (!m_host || pixels.empty()) return;
    if (m_layerIndex < 0) return;
    m_host->writeLayerPixels(m_layerIndex, pixels);
}

void FilterCommand::undo()
{
    applyToLayer(m_before);
    LOG_INFO("[FilterCommand] undo: kind={}", static_cast<int>(m_kind));
}

void FilterCommand::redo()
{
    applyToLayer(m_after);
    LOG_INFO("[FilterCommand] redo: kind={}", static_cast<int>(m_kind));
}

} // namespace filter
