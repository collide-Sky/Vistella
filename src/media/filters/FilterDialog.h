// SPDX-License-Identifier: MIT
//
// FilterDialog - P0-5.9 (2026-09-10), P3.1.1 (2026-09-22)
//
// Non-modal filter dialog (PS-style, "Filter Gallery" 风格).
//   - Filter name (big font) + param widgets (slider/spinbox/picker)
//   - Param widgets: int slider, double slider, color picker (per strategy)
//   - Apply: realtime preview (host stores a temporary m_previewImage layer)
//   - OK: clear preview + push FilterCommand (uses dialog strategy's params)
//   - Cancel: clear preview
//
// P3.1.1 (2026-09-22):
//   - strategy() accessor exposes the dialog-owned FilterStrategy so the
//     caller (P3.1.3 host preview, OK push command) can read its params
//   - Owns m_strategy (created via FilterFactory in ctor); same instance
//     is used for Apply preview, OK command, paramText live updates
//
#pragma once

#include <QDialog>
#include <memory>
#include "FilterStrategy.h"

class QLabel;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class ImageWindow;

namespace filter {

class FilterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilterDialog(ImageWindow *host, FilterKind kind, QWidget *parent = nullptr);
    ~FilterDialog() override;

    FilterKind kind() const { return m_kind; }
    // P3.1.1: 暴露 strategy 引用 (供 Apply 预览 + OK push command 用)
    FilterStrategy* strategy() { return m_strategy.get(); }
    const FilterStrategy* strategy() const { return m_strategy.get(); }

signals:
    // PS 风格: Apply 实时预览, OK push Command
    void applyRequested();
    void okRequested();

private slots:
    void onApplyClicked();
    void onOkClicked();
    void onCancelClicked();

private:
    // P3.1.1: 用 m_kind 建对应 strategy, slider/picker 改它的公有字段
    //   strategy 生命周期 = dialog 生命周期 (unique_ptr)
    void buildParamWidgets();

    // P3.1.1: 参数改动 -> m_paramLabel 刷新
    void refreshParamText();

    ImageWindow                 *m_host = nullptr;
    FilterKind                   m_kind;
    std::unique_ptr<FilterStrategy> m_strategy;     // P3.1.1: dialog-owned
    QLabel                      *m_nameLabel    = nullptr;
    QLabel                      *m_paramLabel   = nullptr;
    QPushButton                 *m_applyBtn     = nullptr;
    QPushButton                 *m_okBtn        = nullptr;
    QPushButton                 *m_cancelBtn    = nullptr;
};

} // namespace filter