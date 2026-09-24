// SPDX-License-Identifier: MIT
//
// TextOptionPanel - Q4.2.2 (2026-09-24)
//
// PS-style options panel for TextOverlayController. Migrated from
// imagewindow.ui hardcoded groupText (font combo + size spin + color
// button + Bold + Italic + text content input). ImageWindow hosts
// this widget inside the left dock stack (below MosaicOptionPanel),
// replacing the placeholder group.
//
// Two-way binding:
//   font combo     <-> TextOverlayController::setTextFont / textFont()
//   size spin      <-> TextOverlayController::setTextSize / textSize()
//   color picker   <-> TextOverlayController::setTextColor / textColor()
//   bold toggle    <-> TextOverlayController::setTextBold / textBold()
//   italic toggle  <-> TextOverlayController::setTextItalic / textItalic()
//   content input  -> TextOverlayController::setCurrent() + apply text
//
// All edits route through TextOverlayController so changes flow back
// to the currently focused text item via applyStyleToCurrent().
//
#pragma once

#include <QWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>

class TextOverlayController;

class TextOptionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TextOptionPanel(TextOverlayController* ctrl, QWidget* parent = nullptr);
    ~TextOptionPanel();

    // Re-sync UI from controller state (e.g. after PropertiesDock
    // changes current item).
    void refreshFromController();

private slots:
    void onFontComboChanged(const QString& family);
    void onSizeSpinChanged(int v);
    void onColorClicked();
    void onBoldToggled(bool on);
    void onItalicToggled(bool on);
    void onContentEditingFinished();
    // TextOverlayController signal - re-sync when current item changes.
    void onCurrentChanged();

private:
    void setupUi();

    TextOverlayController* m_ctrl = nullptr;
    QComboBox*    m_fontCombo = nullptr;
    QSpinBox*     m_sizeSpin   = nullptr;
    QPushButton*  m_colorBtn   = nullptr;
    QToolButton*  m_boldBtn    = nullptr;
    QToolButton*  m_italicBtn  = nullptr;
    QLineEdit*    m_contentEdit = nullptr;
    QLabel*       m_hintLabel  = nullptr;
    bool          m_suspend    = false;
};
