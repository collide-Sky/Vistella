// SPDX-License-Identifier: MIT
//
// TextOverlayController - QObject, 接管文字 item 的增/删/改/扁平化
// (P0-1.2 2026-09-07: 完整实现, 从 imagewindow.cpp 搬过来)
//
#pragma once

#include <QObject>
#include <QList>
#include <QColor>
#include <QPointF>
#include <QString>

class ImageWindow;
class GraphicsTextItem;

class TextOverlayController : public QObject {
    Q_OBJECT
public:
    explicit TextOverlayController(QObject* parent = nullptr);
    ~TextOverlayController() override;

    void setHost(ImageWindow* w) { m_host = w; }

    // Lifecycle of a text item. Commands call these to register / unregister
    // so we can iterate them on flatten / undo / redo.
    void registerTextItem(GraphicsTextItem* item);
    void unregisterTextItem(GraphicsTextItem* item);
    QList<GraphicsTextItem*> textItems() const { return m_items; }

    // P0-1.2 (2026-09-07): 一次性清空 (给 ImageWindow 析构前调, 释放 scene 上的 item)
    //   clearAll: 仅清 m_items + emit itemRemoved, 不动 scene
    //   removeAllFromSceneAndDelete: 从 scene removeItem + delete + 清 m_items
    void clearAll();
    void removeAllFromSceneAndDelete();

    // Currently focused text item (font/size/color changes apply to it).
    //   P0-7.3 (2026-09-14): emit currentChanged (PropertiesDock 联动)
    void               setCurrent(GraphicsTextItem* item);
    GraphicsTextItem*  current() const                     { return m_current; }

    // Item being dragged via the handle hit-test (set by eventFilter).
    void               setHandleDragItem(GraphicsTextItem* item) { m_handleDragItem = item; }
    GraphicsTextItem*  handleDragItem() const { return m_handleDragItem; }

    // Default style (drives newly created text items).
    QColor  textColor() const        { return m_textColor; }
    QString textFont()  const        { return m_textFont; }
    int     textSize()  const        { return m_textSize; }
    void    setTextColor(const QColor& c) { m_textColor = c; }
    void    setTextFont(const QString& f)  { m_textFont  = f; }
    void    setTextSize(int s)             { m_textSize  = s; }

    // P0-7.2 (2026-09-14): PS 完整文字工具栏 Bold/Italic state
    //   跟 m_textFont/m_textSize 一起存 default style
    //   每次 onFontChanged/onSizeChanged/onColorSelected/applyStyleToCurrent
    //   都会应用到 m_current
    bool    textBold()   const            { return m_textBold; }
    bool    textItalic() const            { return m_textItalic; }
    void    setTextBold(bool on)          { m_textBold = on; }
    void    setTextItalic(bool on)        { m_textItalic = on; }

    // P0-7.3 (2026-09-14): 给 PropertiesDock 读当前 item 完整 style
    //   m_current 为 null 时, 返回 default style
    struct CurrentStyle {
        QString  font;
        int      size = 24;
        QColor   color = QColor(Qt::white);
        bool     bold  = false;
        bool     italic = false;
        QPointF  pos;
        qreal    rotation = 0.0;
    };
    CurrentStyle getCurrentStyle() const;

    // P0-1.2 (2026-09-07): 给 ImageWindow 转发用
    //   进马赛克: 锁所有文字 item 的双击编辑 (setBlockDoubleClickEdit(true))
    //   退马赛克: 解锁
    void    setMosaicMode(bool on);

    // P0-1.2 (2026-09-07): 给 ImageWindow 转发用 (UI 控件变化)
    //   onFontChanged: 字体变化 -> 写 m_textFont, 应用到 m_current
    //   onSizeChanged: 字号变化 -> 写 m_textSize, 应用到 m_current
    //   onColorSelected: 颜色选择 (来自 QColorDialog) -> 写 m_textColor, 应用到 m_current
    void    onFontChanged(const QString& family);
    void    onSizeChanged(int size);
    void    onColorSelected(const QColor& color);

    // Create a new text item at scenePos. Host wires its editingFinished +
    // transformFinished signals. Reads m_textFont / m_textSize / m_textColor
    // from internal state (UI 控件变化时调 setTextFont / setTextSize / setTextColor
    // 同步过来).
    GraphicsTextItem* createTextItem(const QPointF& scenePos);

    // Bake all current text items into the host's current image.
    // After flatten the items list is cleared.
    void flattenText();

    // Apply the current style (font/size/color) to the focused item.
    void applyStyleToCurrent();

    // Connect signals on a freshly created (or redo-restored) text item.
    // Mirrors the public ImageWindow::connectTextItemSignals entry point.
    void connectItemSignals(GraphicsTextItem* item);

signals:
    void itemAdded(GraphicsTextItem* item);
    void itemRemoved(GraphicsTextItem* item);
    void currentChanged(GraphicsTextItem* item);

private:
    ImageWindow*           m_host = nullptr;
    QList<GraphicsTextItem*> m_items;
    GraphicsTextItem*      m_current = nullptr;
    GraphicsTextItem*      m_handleDragItem = nullptr;

    QColor  m_textColor = QColor(Qt::white);
    QString m_textFont  = QStringLiteral("Microsoft YaHei UI");
    int     m_textSize  = 24;
    // P0-7.2 (2026-09-14): PS 完整 Bold/Italic state
    bool    m_textBold   = false;
    bool    m_textItalic = false;
};
