// SPDX-License-Identifier: MIT
//
// TextOptionPanel implementation - Q4.2.2 (2026-09-24)
//
#include "TextOptionPanel.h"
#include "TextOverlayController.h"
#include "graphicstextitem.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <QFontDatabase>

TextOptionPanel::TextOptionPanel(TextOverlayController* ctrl, QWidget* parent)
    : QWidget(parent), m_ctrl(ctrl)
{
    setupUi();
    if (m_ctrl) {
        // Listen for current item changes (PropertiesDock setCurrent
        // triggers a sync).
        connect(m_ctrl, &TextOverlayController::currentChanged,
                this, &TextOptionPanel::onCurrentChanged);
    }
    refreshFromController();
}

TextOptionPanel::~TextOptionPanel() = default;

void TextOptionPanel::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Font combo row (P0-7.2 PS-style full font dropdown).
    auto* fontRow = new QHBoxLayout();
    fontRow->addWidget(new QLabel(tr("字体:"), this));
    m_fontCombo = new QComboBox(this);
    m_fontCombo->setEditable(true);
    const QStringList families = QFontDatabase::families();
    m_fontCombo->addItems(families);
    fontRow->addWidget(m_fontCombo, 1);
    root->addLayout(fontRow);

    // Font size spinbox + Bold/Italic toggles + color picker.
    auto* sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(tr("字号:"), this));
    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setRange(8, 200);
    m_sizeSpin->setValue(24);
    m_sizeSpin->setSuffix(QStringLiteral(" pt"));
    sizeRow->addWidget(m_sizeSpin);
    m_boldBtn = new QToolButton(this);
    m_boldBtn->setText(QStringLiteral("B"));
    m_boldBtn->setCheckable(true);
    m_boldBtn->setToolTip(tr("加粗 (Ctrl+B)"));
    m_boldBtn->setFont(QFont(QString(), -1, QFont::Bold));
    sizeRow->addWidget(m_boldBtn);
    m_italicBtn = new QToolButton(this);
    m_italicBtn->setText(QStringLiteral("I"));
    m_italicBtn->setCheckable(true);
    m_italicBtn->setToolTip(tr("斜体 (Ctrl+I)"));
    QFont italicFont;
    italicFont.setItalic(true);
    m_italicBtn->setFont(italicFont);
    sizeRow->addWidget(m_italicBtn);
    m_colorBtn = new QPushButton(this);
    m_colorBtn->setText(tr("颜色..."));
    m_colorBtn->setToolTip(tr("选择文字颜色"));
    sizeRow->addWidget(m_colorBtn);
    root->addLayout(sizeRow);

    // Text content input row.
    auto* contentRow = new QHBoxLayout();
    contentRow->addWidget(new QLabel(tr("内容:"), this));
    m_contentEdit = new QLineEdit(this);
    m_contentEdit->setPlaceholderText(tr("(在画布上点位置后输入文字)"));
    contentRow->addWidget(m_contentEdit, 1);
    root->addLayout(contentRow);

    // Status hint (migrated from original "lblTextHint").
    m_hintLabel = new QLabel(tr("(在画布上点位置后, 内容生效)"), this);
    m_hintLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    m_hintLabel->setWordWrap(true);
    root->addWidget(m_hintLabel);

    root->addStretch(1);

    // Bidirectional binding.
    connect(m_fontCombo, &QComboBox::currentTextChanged,
            this, &TextOptionPanel::onFontComboChanged);
    // Q4.2.2: Qt 6 qOverload<int>::of fails to compile
    //   (error C3861: 'of' not found), use static_cast to disambiguate.
    using IntSpinSignal = void (QSpinBox::*)(int);
    connect(m_sizeSpin, static_cast<IntSpinSignal>(&QSpinBox::valueChanged),
            this, &TextOptionPanel::onSizeSpinChanged);
    connect(m_colorBtn, &QPushButton::clicked,
            this, &TextOptionPanel::onColorClicked);
    connect(m_boldBtn, &QToolButton::toggled,
            this, &TextOptionPanel::onBoldToggled);
    connect(m_italicBtn, &QToolButton::toggled,
            this, &TextOptionPanel::onItalicToggled);
    connect(m_contentEdit, &QLineEdit::editingFinished,
            this, &TextOptionPanel::onContentEditingFinished);
}

void TextOptionPanel::refreshFromController()
{
    if (!m_ctrl) return;
    QSignalBlocker blockA(m_fontCombo);
    QSignalBlocker blockB(m_sizeSpin);
    QSignalBlocker blockC(m_colorBtn);
    QSignalBlocker blockD(m_boldBtn);
    QSignalBlocker blockE(m_italicBtn);
    QSignalBlocker blockF(m_contentEdit);
    m_suspend = true;

    const TextOverlayController::CurrentStyle st = m_ctrl->getCurrentStyle();
    // Font combo: if current font not in system list, still setCurrentText.
    const QString fam = st.font.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : st.font;
    int idx = m_fontCombo->findText(fam);
    if (idx < 0) {
        m_fontCombo->addItem(fam);
        idx = m_fontCombo->count() - 1;
    }
    m_fontCombo->setCurrentIndex(idx);
    m_sizeSpin->setValue(st.size);
    // Color button: stylesheet shows the current color swatch.
    QString qss = QStringLiteral("background-color: %1; color: %2;")
        .arg(st.color.name(), st.color.lightness() > 128 ? QStringLiteral("black")
                                                         : QStringLiteral("white"));
    m_colorBtn->setStyleSheet(qss);
    m_colorBtn->setText(QStringLiteral("色 ") + st.color.name());
    m_boldBtn->setChecked(st.bold);
    m_italicBtn->setChecked(st.italic);
    // Text content: take current item text; empty if no item.
    if (auto* cur = m_ctrl->current()) {
        m_contentEdit->setText(cur->toPlainText());
    } else {
        m_contentEdit->clear();
    }
    m_suspend = false;
}

void TextOptionPanel::onFontComboChanged(const QString& family)
{
    if (m_suspend || !m_ctrl) return;
    m_ctrl->setTextFont(family);
    m_ctrl->applyStyleToCurrent();
}

void TextOptionPanel::onSizeSpinChanged(int v)
{
    if (m_suspend || !m_ctrl) return;
    m_ctrl->setTextSize(v);
    m_ctrl->applyStyleToCurrent();
}

void TextOptionPanel::onColorClicked()
{
    if (!m_ctrl) return;
    QColor c = QColorDialog::getColor(m_ctrl->textColor(), this, tr("选择文字颜色"));
    if (!c.isValid()) return;
    m_ctrl->setTextColor(c);
    m_ctrl->applyStyleToCurrent();
    refreshFromController();
}

void TextOptionPanel::onBoldToggled(bool on)
{
    if (m_suspend || !m_ctrl) return;
    m_ctrl->setTextBold(on);
    m_ctrl->applyStyleToCurrent();
}

void TextOptionPanel::onItalicToggled(bool on)
{
    if (m_suspend || !m_ctrl) return;
    m_ctrl->setTextItalic(on);
    m_ctrl->applyStyleToCurrent();
}

void TextOptionPanel::onContentEditingFinished()
{
    if (m_suspend || !m_ctrl) return;
    if (auto* cur = m_ctrl->current()) {
        cur->setPlainText(m_contentEdit->text());
    }
}

void TextOptionPanel::onCurrentChanged()
{
    refreshFromController();
}
