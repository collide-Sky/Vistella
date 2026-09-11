// SPDX-License-Identifier: MIT
//
// FilterDialog - P0-5.9 (2026-09-10)
//
// Non-modal filter dialog (PS-style, "Filter Gallery" 风格).
//   - 显示滤镜名 + Apply/OK/Cancel 3 按钮
//   - 简化: 参数用 strategy 默认值 (P0 阶段不在 dialog 里调参数, P1 接)
//   - Apply: 实时预览 (发 applyRequested signal, host 调 strategy->apply 到临时图层预览)
//   - OK: 关闭 + push FilterCommand
//   - Cancel: 关闭
//
// P0 简化版: 没有 preview thumbnail, Apply 只 log + 显示状态栏
//   (完整 preview 留 ImageWindow::previewFilter 实装)
//
#pragma once

#include <QDialog>
#include "FilterStrategy.h"

class QLabel;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class ImageWindow;

namespace filter {

class FilterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilterDialog(ImageWindow *host, FilterKind kind, QWidget *parent = nullptr);
    ~FilterDialog() override;

    FilterKind kind() const { return m_kind; }

signals:
    // PS 风格: Apply 实时预览, OK push Command
    void applyRequested();
    void okRequested();

private slots:
    void onApplyClicked();
    void onOkClicked();
    void onCancelClicked();

private:
    ImageWindow    *m_host = nullptr;
    FilterKind      m_kind;
    QLabel         *m_nameLabel    = nullptr;
    QLabel         *m_paramLabel   = nullptr;
    QPushButton    *m_applyBtn     = nullptr;
    QPushButton    *m_okBtn        = nullptr;
    QPushButton    *m_cancelBtn    = nullptr;
};

} // namespace filter
