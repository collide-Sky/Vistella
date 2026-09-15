// SPDX-License-Identifier: MIT
//
// Text implementation - P0-7.1 (2026-09-14)
//
// 详见 Text.h 头注释
//
#include "Text.h"
#include "../imagewindow.h"
#include "../imagewindow/TextOverlayController.h"
#include "logger.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsItem>

#include "../imagewindow/ImageCanvas.h"

namespace tools {

Text::Text(QWidget* /*parent*/) : ToolState() {}

void Text::onEnter(ImageWindow* host)
{
    m_host = host;
    m_state = Idle;
    m_dragItem = nullptr;
    LOG_DEBUG("[Text] onEnter host={}", host ? "yes" : "null");
}

void Text::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_state = Idle;
    m_dragItem = nullptr;
    m_host = nullptr;
    LOG_DEBUG("[Text] onExit");
}

GraphicsTextItem* Text::itemAtScenePos(const QPointF& scenePos) const
{
    if (!m_host) return nullptr;
    auto* canvas = m_host->imageCanvas();
    if (!canvas) return nullptr;
    auto* scene = canvas->scene();
    if (!scene) return nullptr;
    QGraphicsItem* hit = scene->itemAt(scenePos, canvas->transform());
    while (hit && qgraphicsitem_cast<GraphicsTextItem*>(hit) == nullptr) {
        hit = hit->parentItem();
    }
    return qgraphicsitem_cast<GraphicsTextItem*>(hit);
}

void Text::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(host);
    if (!m_host || e->button() != Qt::LeftButton) return;

    // Plan B 已经被 imagewindow eventFilter 拿走 (handle drag 优先级最高)
    // 我们拿到的要么是 "已有 text item 上 click" 要么是 "空白 click"
    GraphicsTextItem* item = itemAtScenePos(scenePos);
    if (item) {
        // 选中 + 准备拖动
        //   先 unselect 其他 (主流做法, PS 同款)
        if (auto* overlay = m_host->textOverlay()) {
            for (auto* other : overlay->textItems()) {
                if (other && other != item) other->setSelected2(false);
            }
            overlay->setCurrent(item);
        }
        item->setSelected2(true);

        m_state = DragMove;
        m_dragItem = item;
        m_pressScenePos = scenePos;
        m_pressItemPos = item->position();
    } else {
        // 空白 click: imagewindow eventFilter 已经 unselect 全部 + 退出编辑
        // 我们 create new item + 进入编辑模式
        if (auto* created = m_host->createTextItem(scenePos)) {
            if (auto* overlay = m_host->textOverlay()) {
                overlay->setCurrent(created);
            }
            created->startEditing();
            // 不进 DragMove (用户在编辑文本, 不是拖动 item)
            m_state = Idle;
            m_dragItem = nullptr;
        }
    }
}

void Text::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& scenePos)
{
    if (m_state != DragMove || !m_dragItem) return;

    // sceneDelta 算 setPosition
    const QPointF delta = scenePos - m_pressScenePos;
    m_dragItem->setPosition(m_pressItemPos + delta);
}

void Text::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(scenePos);
    if (m_state != DragMove || !m_dragItem || !host) {
        m_state = Idle;
        m_dragItem = nullptr;
        return;
    }

    // 推 TextItemCommand::Move 到 undoStack
    GraphicsTextItem* item = m_dragItem;
    const QPointF oldPos = m_pressItemPos;
    const QPointF newPos = item->position();

    // 没有真正移动就不 push (避免空 command)
    if (qFuzzyCompare(oldPos.x(), newPos.x()) && qFuzzyCompare(oldPos.y(), newPos.y())) {
        m_state = Idle;
        m_dragItem = nullptr;
        return;
    }

    QUndoStack* stack = host->undoStack();
    if (stack) {
        // 关键: push Move command, 走 imagewindow.cpp TextItemCommand ctor 签名
        //   TextItemCommand::Move 需要 m_item, oldPos, newPos
        //   imagewindow.cpp line 69 ctor 签名:
        //     TextItemCommand(ImageWindow*, GraphicsTextItem*, Op, oldText, newText,
        //                     oldPos, newPos, oldRot, newRot, parent)
        auto* cmd = new TextItemCommand(host, item, TextItemCommand::Move,
                                        QString(), QString(),
                                        oldPos, newPos,
                                        0.0, 0.0);
        stack->push(cmd);
    }

    m_state = Idle;
    m_dragItem = nullptr;
}

void Text::onKeyPress(QKeyEvent* e, ImageWindow* host)
{
    Q_UNUSED(host);
    // Esc 取消当前编辑 (退出正在编辑的 item, 主流做法)
    if (e->key() == Qt::Key_Escape) {
        if (auto* overlay = host ? host->textOverlay() : nullptr) {
            if (auto* cur = overlay->current()) {
                cur->endEditing();
                overlay->setCurrent(nullptr);
            }
        }
    }
}

QWidget* Text::optionPage(QWidget* parent)
{
    // P0-7.1 (2026-09-14): PS 完整文字工具栏
    //   字体 ComboBox / 字号 SpinBox / 颜色按钮 / Bold / Italic / 对齐
    //   + "提交所有编辑" / "取消所有编辑" 按钮
    //
    // 跟 groupText (imagewindow.ui) 的关系:
    //   groupText 是"创建前的默认设置" (字号/字体/颜色, 在 image 上点空白时用)
    //   pageText 是"工具激活时" 显示的工具栏 (跟工具语义绑定)
    //   两者数据都同步到 TextOverlayController (同一个 source of truth)
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    // 字体
    auto* fontCombo = new QFontComboBox(page);
    fontCombo->setMinimumWidth(140);
    layout->addWidget(fontCombo);

    // 字号
    auto* sizeSpin = new QSpinBox(page);
    sizeSpin->setRange(8, 200);
    sizeSpin->setValue(24);
    sizeSpin->setMinimumWidth(60);
    layout->addWidget(sizeSpin);

    // 颜色按钮
    auto* colorBtn = new QToolButton(page);
    colorBtn->setText(QStringLiteral("颜色"));
    colorBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    layout->addWidget(colorBtn);

    // Bold
    auto* boldCheck = new QCheckBox(QStringLiteral("B"), page);
    boldCheck->setToolTip(QStringLiteral("粗体 (Bold)"));
    boldCheck->setMaximumWidth(40);
    layout->addWidget(boldCheck);

    // Italic
    auto* italicCheck = new QCheckBox(QStringLiteral("I"), page);
    italicCheck->setToolTip(QStringLiteral("斜体 (Italic)"));
    italicCheck->setMaximumWidth(40);
    layout->addWidget(italicCheck);

    layout->addStretch(1);

    // "提交所有编辑" / "取消所有编辑"
    auto* commitBtn = new QPushButton(QStringLiteral("提交"), page);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), page);
    layout->addWidget(commitBtn);
    layout->addWidget(cancelBtn);

    // 初始化: 同步当前 TextOverlayController 的默认状态
    if (m_host) {
        if (auto* overlay = m_host->textOverlay()) {
            const QString defFont = overlay->textFont();
            const int     defSize = overlay->textSize();
            const QColor  defColor = overlay->textColor();
            // 找字体 index (QFontComboBox 用 family 找)
            const int idx = fontCombo->findText(defFont);
            if (idx >= 0) fontCombo->setCurrentIndex(idx);
            sizeSpin->setValue(defSize);
            // 颜色按钮背景色
            QString css = QString("QToolButton { background: %1; color: %2; }")
                              .arg(defColor.name())
                              .arg(defColor.lightness() > 128 ? QStringLiteral("black") : QStringLiteral("white"));
            colorBtn->setStyleSheet(css);
            Q_UNUSED(css);
        }
    }

    // 联动:
    //   fontCombo 变化 -> m_textCtrl.setTextFont + applyStyleToCurrent
    QObject::connect(fontCombo, &QFontComboBox::currentFontChanged,
                     page, [this](const QFont& f) {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        overlay->setTextFont(f.family());
        overlay->applyStyleToCurrent();
    });
    //   sizeSpin 变化 -> m_textCtrl.setTextSize + applyStyleToCurrent
    QObject::connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     page, [this](int v) {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        overlay->setTextSize(v);
        overlay->applyStyleToCurrent();
    });
    //   colorBtn click -> QColorDialog -> m_textCtrl.setTextColor + applyStyleToCurrent
    QObject::connect(colorBtn, &QToolButton::clicked,
                     page, [this, colorBtn]() {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        QColor c = QColorDialog::getColor(overlay->textColor(), nullptr,
                                          QStringLiteral("选择文字颜色"));
        if (!c.isValid()) return;
        overlay->setTextColor(c);
        // 更新按钮背景色
        QString css = QString("QToolButton { background: %1; color: %2; }")
                          .arg(c.name())
                          .arg(c.lightness() > 128 ? QStringLiteral("black") : QStringLiteral("white"));
        colorBtn->setStyleSheet(css);
        Q_UNUSED(css);
        overlay->applyStyleToCurrent();
    });
    //   boldCheck toggled -> m_textCtrl.setTextBold + applyStyleToCurrent
    QObject::connect(boldCheck, &QCheckBox::toggled,
                     page, [this](bool on) {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        overlay->setTextBold(on);
        overlay->applyStyleToCurrent();
    });
    //   italicCheck toggled -> m_textCtrl.setTextItalic + applyStyleToCurrent
    QObject::connect(italicCheck, &QCheckBox::toggled,
                     page, [this](bool on) {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        overlay->setTextItalic(on);
        overlay->applyStyleToCurrent();
    });
    //   commitBtn click -> 退出所有编辑 (主流做法)
    QObject::connect(commitBtn, &QPushButton::clicked,
                     page, [this]() {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        for (auto* it : overlay->textItems()) {
            if (it) it->endEditing();
        }
        overlay->setCurrent(nullptr);
    });
    //   cancelBtn click -> 撤销当前编辑的 item 内容 (主流做法)
    //      注意: 实现简化, 只 endEditing 不 revert text (因为 editingStartText 在 item 内部)
    //      完整 PS 做法要记 beforeText/afterText 推 undo, 后续 P0-7.x 完善
    QObject::connect(cancelBtn, &QPushButton::clicked,
                     page, [this]() {
        if (!m_host) return;
        auto* overlay = m_host->textOverlay();
        if (!overlay) return;
        for (auto* it : overlay->textItems()) {
            if (it) it->endEditing();
        }
        overlay->setCurrent(nullptr);
    });

    LOG_DEBUG("[Text] optionPage created (font/size/color/Bold/Italic + commit/cancel)");
    return page;
}

} // namespace tools
