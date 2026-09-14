// SPDX-License-Identifier: MIT
//
// TransformCommand - P0-6.4 (2026-09-14)
//                       P0-6.6 扩展: 存 cv::Mat before/after 支持真 undo/redo
//
// QUndoCommand 子类 — 撤销/重做 box 变换
//   存 m_before / m_after 完整 box 状态 (rect, corners, rotation, mode)
//   存 m_beforeMat / m_afterMat cv::Mat — image 实际像素 (PS undo 行为)
//   redo: m_host->setCurrentImage(m_afterMat), 重设 box
//   undo: m_host->setCurrentImage(m_beforeMat), 重设 box
//
// 集成:
//   - TransformTool::commitTransform() 推入 m_w->undoStack()
//   - 6 menu action (P0-6.5) 翻转 / 旋转 90 也用这个 command
//
#pragma once

#include "TransformBox.h"

#include <opencv2/core.hpp>
#include <QUndoCommand>
#include <QPointer>

class ImageWindow;

namespace transform {

class TransformCommand : public QUndoCommand
{
public:
    // 简单构造: before / after box 状态 + cv::Mat 像素
    TransformCommand(ImageWindow* host,
                     const cv::Mat& beforeMat,
                     const cv::Mat& afterMat,
                     const TransformBox& before,
                     const TransformBox& after,
                     const QString& text = QStringLiteral("变换"));

    // 翻转 / 旋转 90 专用 (矩阵直传, box 状态前后相同)
    TransformCommand(ImageWindow* host,
                     const cv::Mat& beforeMat,
                     const cv::Mat& afterMat,
                     const QString& text = QStringLiteral("变换"));

    void redo() override;
    void undo() override;

    // 合并多个微调 (拖动过程中合并, 不每像素都 push command)
    int id() const override { return 9001; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QPointer<ImageWindow> m_host;
    cv::Mat               m_beforeMat;
    cv::Mat               m_afterMat;
    TransformBox          m_before;
    TransformBox          m_after;
    bool                  m_useBox = true;  // false 表示用 m_beforeMat / m_afterMat 即可
};

}  // namespace transform
