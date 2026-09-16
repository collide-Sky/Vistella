// SPDX-License-Identifier: MIT
//
// BrushPickerDialog - P1.1 (2026-09-15) Brush full implementation
//   PS-style preset browser. Modal dialog with grid of preset thumbnails.
//   Double-click selects a preset and emits presetSelected().
//   "Load ABR..." button imports .abr brushes (parsing in P1.1 follow-up).
//   Built-ins + user presets displayed together.
//
//   For first pass:
//     - IconMode QListView (60x60 icons + text below)
//     - Double-click closes dialog with chosen preset
//     - Load ABR button shows file dialog + log (parsing deferred to P1.1 follow-up)

#pragma once

#include <QDialog>

#include "BrushPreset.h"

class QListView;
class QStringListModel;
class QPushButton;
class QLineEdit;

namespace brushes {
class BrushPresetManager;
}

namespace brushes {

class BrushPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BrushPickerDialog(BrushPresetManager *mgr, QWidget *parent = nullptr);
    ~BrushPickerDialog() override;

    // Selected preset (valid after accept() returns).
    BrushPreset selectedPreset() const { return m_selected; }

signals:
    // Emitted right before accept(); outer code can listen and apply.
    void presetSelected(const brushes::BrushPreset &p);

private slots:
    void onItemDoubleClicked(const QModelIndex &idx);
    void onFilterChanged(const QString &text);
    void onLoadAbrClicked();

private:
    void rebuildModel();
    void selectByName(const QString &name);

    BrushPresetManager*   m_mgr;
    QListView*            m_list;
    QStringListModel*      m_model;
    QStringList            m_allNames;
    BrushPreset            m_selected;
};

} // namespace brushes