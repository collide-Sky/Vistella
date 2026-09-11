// SPDX-License-Identifier: MIT
//
// TextOverlayController - P0-1.2 (2026-09-07) full impl
//   接管 imagewindow.cpp 中所有"文字"逻辑:
//     - m_textItems 列表 (搬过来 = m_items)
//     - m_currentTextItem (搬过来 = m_current)
//     - m_handleDragItem (搬过来)
//     - m_textColor / m_textFont / m_textSize (搬过来)
//     - createTextItem / connectItemSignals / flattenText / applyStyleToCurrent
//     - onTextFontChanged / onTextSizeChanged / onTextColorClicked
//     - onMosaicModeToggled 的"锁/解锁文字 item 双击编辑" 副作用
//   保持原代码语义, 仅**位置**移动, 不**逻辑**删除/简化.
#include "TextOverlayController.h"
#include "../imagewindow.h"
#include "../graphicstextitem.h"
#include "../imageprocessor.h"

#include <QFont>
#include <QPainter>
#include <QFontComboBox>
#include <QSpinBox>
#include <QStatusBar>
#include <QGraphicsScene>
#include <QTextDocument>

TextOverlayController::TextOverlayController(QObject* parent) : QObject(parent) {}
TextOverlayController::~TextOverlayController() = default;

void TextOverlayController::registerTextItem(GraphicsTextItem* item) {
    if (!item) return;
    if (m_items.contains(item)) return;
    m_items.append(item);
    emit itemAdded(item);
}

void TextOverlayController::unregisterTextItem(GraphicsTextItem* item) {
    if (!item) return;
    if (m_items.removeAll(item) <= 0) return;
    if (m_current == item) setCurrent(nullptr);
    if (m_handleDragItem == item) setHandleDragItem(nullptr);
    emit itemRemoved(item);
}

// P0-1.2 (2026-09-07): clearAll - 给 ImageWindow 析构前调 (替代原 m_textItems 遍历清理)
void TextOverlayController::clearAll() {
    // 注意: 不在这里 removeItem + delete (scene 由 host 管, scene 析构时会自动处理)
    // 只清 list + 通知, scene 析构自动 delete
    const auto copy = m_items;
    m_items.clear();
    if (m_current) setCurrent(nullptr);
    if (m_handleDragItem) setHandleDragItem(nullptr);
    for (auto* item : copy) emit itemRemoved(item);
}

// P0-1.2 (2026-09-07): 给 ImageWindow 析构时调 (替代原来的 m_textItems 遍历)
void TextOverlayController::removeAllFromSceneAndDelete() {
    if (!m_host) {
        m_items.clear();
        m_current = nullptr;
        m_handleDragItem = nullptr;
        return;
    }
    for (auto* item : m_items) {
        if (item) {
            m_host->sceneRef().removeItem(item);
            delete item;
        }
    }
    m_items.clear();
    m_current = nullptr;
    m_handleDragItem = nullptr;
}

void TextOverlayController::setMosaicMode(bool on) {
    // 主流做法 (Photoshop/Word): 进马赛克时锁文字双击编辑, 退时解锁
    //   避免涂抹和文字输入冲突, 但 transform (拖动/resize/rotate) 仍可用
    for (auto* ti : m_items) {
        if (ti) ti->setBlockDoubleClickEdit(on);
    }
}

GraphicsTextItem* TextOverlayController::createTextItem(const QPointF& scenePos) {
    if (!m_host) return nullptr;
    // 主流做法: 创建新 item 之前, 先让当前正在编辑的 item 退出编辑
    if (m_current) {
        m_current->endEditing();
        setCurrent(nullptr);
    }
    auto* item = new GraphicsTextItem();
    // 同步左面板的字体/字号/颜色 (从组件自己 m_textFont / m_textSize / m_textColor 读)
    QFont f(m_textFont);
    f.setPointSize(m_textSize);
    item->setFont(f);
    item->setDefaultTextColor(m_textColor);
    item->setPosition(scenePos);
    item->setZValue(100);
    m_host->sceneRef().addItem(item);
    registerTextItem(item);
    // 监听 editingFinished + transformFinished (统一走 helper)
    connectItemSignals(item);
    // 进入编辑模式
    item->startEditing();
    setCurrent(item);
    // 入撤销栈: Add
    if (auto* stack = m_host->undoStack()) {
        stack->push(new TextItemCommand(m_host, item, TextItemCommand::Add,
                                        item->serializedText()));
    }
    return item;
}

void TextOverlayController::connectItemSignals(GraphicsTextItem* item) {
    if (!item || !m_host) return;
    // editingFinished -> 推 Change command
    QPointer<GraphicsTextItem> weakItem(item);
    QPointer<TextOverlayController> weakSelf(this);
    connect(item, &GraphicsTextItem::editingFinished, this,
            [this, weakItem, weakSelf](const QString& oldText, const QString& newText) {
        if (oldText == newText) return;
        if (weakItem.isNull()) return;
        if (weakSelf.isNull()) return;
        if (auto* stack = m_host->undoStack()) {
            stack->push(new TextItemCommand(m_host, weakItem.data(), TextItemCommand::Change,
                                            oldText, newText));
        }
    });
    // 调试用: GraphicsTextItem 的鼠标事件实时打到 statusBar
    connect(item, &GraphicsTextItem::debugMsg, this, [this](const QString& text) {
        if (m_host && m_host->statusBarRef()) {
            m_host->statusBarRef()->showMessage(text, 3000);
        }
    });
    // transformFinished -> 推 Move / Rotate command
    connect(item, &GraphicsTextItem::transformFinished, this,
            [this, weakItem, weakSelf](const QPointF& oldPos, const QPointF& newPos,
                                       qreal oldRot, qreal newRot) {
        if (weakItem.isNull()) return;
        if (weakSelf.isNull()) return;
        const bool posChanged = !qFuzzyCompare(oldPos.x(), newPos.x())
                             || !qFuzzyCompare(oldPos.y(), newPos.y());
        const bool rotChanged = !qFuzzyCompare(1.0 + oldRot, 1.0 + newRot);
        auto* stack = m_host->undoStack();
        if (!stack) return;
        if (posChanged) {
            stack->push(new TextItemCommand(m_host, weakItem.data(), TextItemCommand::Move,
                                            QString(), QString(),
                                            oldPos, newPos,
                                            oldRot, newRot));
        } else if (rotChanged) {
            stack->push(new TextItemCommand(m_host, weakItem.data(), TextItemCommand::Rotate,
                                            QString(), QString(),
                                            oldPos, newPos,
                                            oldRot, newRot));
        }
    });
}

// 扁平化: 把所有文字 item 用 QPainter 渲染到 QImage, 转 cv::Mat, 烧到 m_current
// 烧图后 m_items 清空 (字变成图片像素, 不可再编辑/撤销)
void TextOverlayController::flattenText() {
    if (!m_host) return;
    auto& current = m_host->currentImage();
    if (m_items.isEmpty() || current.empty()) return;
    // 先让所有 item 退出编辑
    for (auto* item : m_items) {
        if (item) item->endEditing();
    }
    // m_current -> QImage
    QImage img = ImageProcessor::matToQImage(current);
    if (img.isNull()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    // 渲染每个文字 item 到对应 scene 位置
    for (auto* item : m_items) {
        if (!item) continue;
        const QPointF sp = item->scenePos();
        p.save();
        p.translate(sp);
        p.rotate(item->rotation());
        p.scale(item->scale(), item->scale());
        item->document()->drawContents(&p);
        p.restore();
    }
    p.end();
    // QImage -> cv::Mat (通过 host 的 setCurrentImage 触发完整 refreshAll)
    cv::Mat newMat = ImageProcessor::qImageToMat(img);
    m_host->setCurrentImage(newMat);
    // 删除所有 item (从 scene 移除 + delete)
    for (auto* item : m_items) {
        if (item) {
            m_host->sceneRef().removeItem(item);
            delete item;
        }
    }
    m_items.clear();
    setCurrent(nullptr);
}

// 字体变化 -> 应用到 m_current
void TextOverlayController::onFontChanged(const QString& family) {
    m_textFont = family;
    if (m_current) {
        QFont f = m_current->font();
        f.setFamily(m_textFont);
        f.setPointSize(m_textSize);
        m_current->setFont(f);
    }
}

// 字号变化 -> 应用到 m_current
void TextOverlayController::onSizeChanged(int size) {
    m_textSize = size;
    if (m_current) {
        QFont f = m_current->font();
        f.setPointSize(size);
        m_current->setFont(f);
    }
}

// 颜色变化 -> 应用到 m_current (来自 QColorDialog)
void TextOverlayController::onColorSelected(const QColor& color) {
    if (!color.isValid()) return;
    m_textColor = color;
    if (m_current) {
        m_current->setDefaultTextColor(color);
    }
}

// 占位: 之前 imagewindow.h 有这个 stub, 现在保留接口
void TextOverlayController::applyStyleToCurrent() {
    if (!m_current) return;
    QFont f(m_textFont);
    f.setPointSize(m_textSize);
    m_current->setFont(f);
    m_current->setDefaultTextColor(m_textColor);
}
