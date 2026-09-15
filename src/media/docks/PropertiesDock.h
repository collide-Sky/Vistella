// SPDX-License-Identifier: MIT
//
// PropertiesDock - F-I (2026-09-09)
//
// 右侧 panel dock 2: 属性 (PS 风格, 显示当前图片信息)
//   F-I 阶段:
//     - 路径/尺寸/格式/DPI 4 个字段
//     - QScrollArea 装 form layout, **maxHeight 150px** 防止 1316 bug 复发
//   F-L 阶段: 加更多属性 (位深/通道数/历史步数/缩放等)
//
// 1316 bug 真因 (2026-09-09 调查):
//   imageproperty 面板内容全展示, widget 自动 grow 高, 触发 window resize
//   m_normalSize 被设成 (1280, 1316) (屏幕高度), 写到 QSettings
//   修法: PropertiesDock 内部用 QScrollArea + maxHeight 限制, 永远不拉高主窗口
//
#pragma once

#include <QWidget>
#include <QString>

#include "Throttle.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QFormLayout;

namespace docks {

class PropertiesDock : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesDock(QWidget* parent = nullptr);
    ~PropertiesDock() override = default;

    // imagewindow loadFile 后调, 注入图片信息
    struct ImageInfo {
        QString filePath;
        int     width   = 0;
        int     height  = 0;
        QString format;     // "PNG" / "JPEG" / "..."
        int     dpi      = 72;
    };
    void setImageInfo(const ImageInfo& info);
    void clear();

    // P0-4.9 (2026-09-10): selection bounding box (QRect() = clear)
    void setSelectionBbox(const QRect& bbox);
    void clearSelectionBbox();

    // P0-6.12 (2026-09-14): 自由变换 box 旋转角度 (TransformTool 在 drag 时调)
    //   qreal deg 任意值 (负数也支持); qQNaN() 表示"无 transform"
    void setRotation(qreal deg);
    void clearRotation();

    // P0-7.4 (2026-09-14): 文字图层属性 (跟当前 GraphicsTextItem 联动)
    //   ImageWindow::ctor 里 connect m_textCtrl->currentChanged -> setTextProperties(item)
    //   text item 没选中或销毁时调 clearTextProperties()
    //   text = font family (e.g. "Microsoft YaHei UI")
    //   pos = item->position() (scene 坐标)
    struct TextProperties {
        QString font;
        int     size  = 0;
        QColor  color;
        bool    bold  = false;
        bool    italic = false;
        QPointF pos;
    };
    void setTextProperties(const TextProperties& props);
    void clearTextProperties();

    // F-O (2026-09-10) test accessors (read-only snapshot for QSignalSpy-style
    // verification in tst_F_O_Throttle). Cheap, no side effects. Defined in
    // .cpp so we don't drag <QLabel> into every PropertiesDock.h consumer.
    int     appliedCount() const;
    QString pathLabelText() const;
    QString sizeLabelText() const;
    QString formatLabelText() const;
    QString dpiLabelText() const;

    // P0-7.5 (2026-09-14) text properties accessors (跟 F-O 同模式, 给 tst_P0_7_TextSystem 用)
    QString textFontLabel() const;
    QString textSizeLabel() const;
    QString textColorLabel() const;
    QString textBoldLabel() const;
    QString textItalicLabel() const;
    QString textPosLabel() const;

private slots:
    // F-O (2026-09-10): throttled apply slot, fired by m_setInfoThrottle.
    void applyPendingInfo();

private:
    void setupFormLayout();

    QScrollArea* m_scroll      = nullptr;
    QWidget*     m_formWidget  = nullptr;
    QFormLayout* m_formLayout  = nullptr;
    QLabel*      m_pathLabel   = nullptr;
    QLabel*      m_sizeLabel   = nullptr;
    QLabel*      m_formatLabel = nullptr;
    QLabel*      m_dpiLabel    = nullptr;

    // P0-4.9 (2026-09-10): selection bbox labels (x/y/w/h)
    QLabel*      m_selXLabel   = nullptr;
    QLabel*      m_selYLabel   = nullptr;
    QLabel*      m_selWLabel   = nullptr;
    QLabel*      m_selHLabel   = nullptr;
    // P0-6.12 (2026-09-14): transform rotation label (1 行, 在 X/Y/W/H 之后)
    QLabel*      m_rotLabel    = nullptr;
    // P0-7.4 (2026-09-14): text properties 6 行 (字体/字号/颜色/Bold/Italic/位置)
    //   在 rotation 之后追加, 跟 P0-6 一样模式
    QLabel*      m_textFontLabel  = nullptr;
    QLabel*      m_textSizeLabel  = nullptr;
    QLabel*      m_textColorLabel = nullptr;
    QLabel*      m_textBoldLabel  = nullptr;
    QLabel*      m_textItalicLabel = nullptr;
    QLabel*      m_textPosLabel   = nullptr;

    // F-O (2026-09-10): throttle state
    core::Throttle*        m_setInfoThrottle = nullptr;   // owned, parent=this
    ImageInfo              m_pendingInfo;                // last info passed to setImageInfo
    int                    m_appliedCount   = 0;         // how many times applyPendingInfo ran
};

} // namespace docks
