// SPDX-License-Identifier: MIT
//
// TransformCommand implementation - P0-6.4 / P0-6.6
//
#include "TransformCommand.h"
#include "../imagewindow.h"

namespace transform {

TransformCommand::TransformCommand(ImageWindow* host,
                                     const cv::Mat& beforeMat,
                                     const cv::Mat& afterMat,
                                     const TransformBox& before,
                                     const TransformBox& after,
                                     const QString& text)
    : QUndoCommand(text)
    , m_host(host)
    , m_beforeMat(beforeMat.clone())
    , m_afterMat(afterMat.clone())
    , m_before(before)
    , m_after(after)
    , m_useBox(true)
{
}

TransformCommand::TransformCommand(ImageWindow* host,
                                     const cv::Mat& beforeMat,
                                     const cv::Mat& afterMat,
                                     const QString& text)
    : QUndoCommand(text)
    , m_host(host)
    , m_beforeMat(beforeMat.clone())
    , m_afterMat(afterMat.clone())
    , m_useBox(false)
{
}

void TransformCommand::redo()
{
    if (!m_host) return;
    // P0-6.6: 应用 afterMat 跟 afterBox 状态
    //   applyTransformImage 是 ImageWindow 接口, 接受 cv::Mat + 可选 TransformBox
    m_host->applyTransformImage(m_afterMat, m_useBox ? &m_after : nullptr);
}

void TransformCommand::undo()
{
    if (!m_host) return;
    m_host->applyTransformImage(m_beforeMat, m_useBox ? &m_before : nullptr);
}

bool TransformCommand::mergeWith(const QUndoCommand* /*other*/)
{
    // 不合并 (每次 mouse release 单独 push command)
    return false;
}

}  // namespace transform
