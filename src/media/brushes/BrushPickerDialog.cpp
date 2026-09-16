// SPDX-License-Identifier: MIT
//
// BrushPickerDialog impl - P1.1 (2026-09-15)

#include "BrushPickerDialog.h"
#include "BrushEngine.h"
#include "BrushPreset.h"
#include "logger.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QStringListModel>
#include <QVBoxLayout>

namespace brushes {

BrushPickerDialog::BrushPickerDialog(BrushPresetManager* mgr, QWidget* parent)
    : QDialog(parent)
    , m_mgr(mgr)
{
    setWindowTitle(tr("Brush Preset Picker"));
    resize(640, 480);

    auto* layout = new QVBoxLayout(this);

    // Filter line
    auto* filterRow = new QHBoxLayout;
    auto* filterText = new QLabel(tr("Filter:"));
    auto* filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText(tr("type preset name..."));
    filterRow->addWidget(filterText);
    filterRow->addWidget(filterEdit, 1);
    layout->addLayout(filterRow);

    // List
    m_list = new QListView;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(60, 60));
    m_list->setGridSize(QSize(80, 90));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(8);
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list, 1);

    m_model = new QStringListModel(this);
    m_list->setModel(m_model);

    connect(m_list, &QListView::doubleClicked, this, &BrushPickerDialog::onItemDoubleClicked);
    connect(filterEdit, &QLineEdit::textChanged, this, &BrushPickerDialog::onFilterChanged);

    // Button row
    auto* btnRow = new QHBoxLayout;
    auto* loadAbr = new QPushButton(tr("Load ABR..."));
    auto* okBtn = new QPushButton(tr("Cancel"));
    btnRow->addWidget(loadAbr);
    btnRow->addStretch(1);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    connect(loadAbr, &QPushButton::clicked, this, &BrushPickerDialog::onLoadAbrClicked);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::reject);

    rebuildModel();
}

BrushPickerDialog::~BrushPickerDialog() = default;

void BrushPickerDialog::rebuildModel()
{
    m_allNames.clear();
    if (!m_mgr) return;
    for (const auto &p : m_mgr->all()) {
        // Use preset name + size in label, like PS
        QString label = QStringLiteral("%1 (%2px)").arg(p.name).arg(p.settings.size);
        m_allNames.append(label);
    }
    onFilterChanged({});
}

void BrushPickerDialog::onFilterChanged(const QString &text)
{
    if (text.isEmpty()) {
        m_model->setStringList(m_allNames);
        return;
    }
    QStringList filtered;
    for (const auto &name : m_allNames) {
        if (name.contains(text, Qt::CaseInsensitive)) {
            filtered.append(name);
        }
    }
    m_model->setStringList(filtered);
}

void BrushPickerDialog::onItemDoubleClicked(const QModelIndex& idx)
{
    if (!idx.isValid() || !m_mgr) return;
    // Extract preset name (text before " (")
    const QString label = idx.data(Qt::DisplayRole).toString();
    const int paren = label.indexOf(QStringLiteral(" ("));
    const QString name = (paren > 0) ? label.left(paren) : label;
    selectByName(name);
}

void BrushPickerDialog::selectByName(const QString &name)
{
    for (const auto &p : m_mgr->all()) {
        if (p.name == name) {
            m_selected = p;
            LOG_INFO("[Picker] selected preset '{}'", name.toLocal8Bit().constData());
            emit presetSelected(p);
            accept();
            return;
        }
    }
    LOG_WARN("[Picker] preset '{}' not found", name.toLocal8Bit().constData());
}

void BrushPickerDialog::onLoadAbrClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Adobe Brush (.abr)"), {},
        tr("Adobe Brush (*.abr);;All Files (*)"));
    if (path.isEmpty()) return;
    QMessageBox::information(this, tr("Load ABR - P1.1 follow-up"),
                             tr("ABR parser will be implemented in P1.1 follow-up.\nSelected: %1").arg(path));
    LOG_INFO("[Picker] ABR load requested (P1.1 follow-up): {}", path.toStdString());
}

} // namespace brushes