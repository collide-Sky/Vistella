// SPDX-License-Identifier: MIT
//
// MosaicTool - QObject, 接管涂抹 4 种类型 + 笔刷光标 + applyMosaic
// (P0-1.2 2026-09-07: 完整实现, 从 imagewindow.cpp 搬过来)
//
#pragma once

#include <QObject>
#include <QPointer>
#include <QPointF>

#include <opencv2/core.hpp>

class ImageWindow;
class ImageCanvas;
class QGraphicsEllipseItem;
class QGraphicsScene;

class MosaicTool : public QObject {
    Q_OBJECT
public:
    enum Type { Pixelate = 0, Blur = 1, Blackout = 2, Tiles = 3 };

    explicit MosaicTool(QObject* parent = nullptr);
    ~MosaicTool() override;

    // P0-1.2 (2026-09-08) fix: 切到 QPointer<ImageWindow>
    //   之前 raw pointer: m_host 在 ImageWindow 析构后野指针, 任何对 m_host->xxx() 的访问
    //   都会触发 SIGSEGV (用户报告 "程序启动时死机中断" 的根因之一)
    //   QPointer 在 ImageWindow 析构时自动置 null, 后续访问安全 early return
    //   之前 tst_ImageEditCommand QPointer 失败是因为 ImageEditCommand m_w 在 ctor 期
    //   设 (m_w 还没 ref count 初始化), 这里 setHost 都在 MosaicTool ctor 之后调,
    //   QPointer 安全. ImageWindow dtor 里 setHost(nullptr) 显式置 null 保证析构期安全.
    void setHost(ImageWindow* w)  { m_host = w; }
    ImageWindow* host() const     { return m_host.data(); }
    void setCanvas(ImageCanvas* c) { m_canvas = c; }

    // Mode toggle from the toolbar.
    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    // Current type / size, also from the toolbar.
    void setType(Type t)         { m_type = t; emit typeChanged(t); }
    Type type() const            { return m_type; }
    // P0-1.2 fix (2026-09-08): 三重防御
    //   1) m_size 永远先 set 成功 (不依赖任何外部状态)
    //   2) m_host 用 QPointer (上面), 析构后自动 null
    //   3) brush cursor item 加 scene() 野指针 guard (m_canvas 销毁后 item 野指针但非 null)
    void setSize(int radius);
    int  size() const            { return m_size; }

    // Push the brush cursor onto the canvas scene (called by the host when
    // entering mosaic mode, and on every size change).
    // P0-1.2 (2026-09-07): brush cursor 控件还在 ImageWindow 里 (这一轮**不动**)
    //   installBrushCursor / uninstallBrushCursor 只调 visibility, 不创建/销毁
    void installBrushCursor();
    void uninstallBrushCursor();

    // P0-1.2 (2026-09-07): 涂抹 stroke 接口, 给 ImageWindow eventFilter 转发
    //   onSceneClicked: stroke 起点 (snapshot + 立即应用一次)
    //   onSceneDragMove: stroke 持续 (左键按下时每次移动都调)
    //   onSceneDragEnd: stroke 结束 (push ImageEditCommand 撤销)
    void onSceneClicked(const QPointF& scenePos);
    void onSceneDragMove(const QPointF& scenePos);
    void onSceneDragEnd();

    // Returns whether the undo stack should see this as a single command
    // (set true on first click, set false on release).
    void beginStroke() { m_strokeOpen = true; ensureBackup(); }
    void endStroke()   { m_strokeOpen = false; }

    // Snapshot used for ImageEditCommand undo. Set by beginStroke().
    const cv::Mat& backupImage() const { return m_backup; }

signals:
    void modeChanged(bool on);
    void typeChanged(Type t);
    void sizeChanged(int radius);

private:
    // P0-1.2 (2026-09-07): 涂抹核心
    //   applyMosaicAt: 完整版 (MouseMove 路径, 4 种 type 完整 switch)
    //   applyMosaicAtInitial: 简化版 (MouseButtonPress 路径, 只用 ImageProcessor::mosaic)
    //   两个版本都保留原代码语义, 不修"原代码不一致"这个潜在 bug
    void applyMosaicAt(const QPointF& scenePos);
    void applyMosaicAtInitial(const QPointF& scenePos);
    void ensureBackup();

    QPointer<ImageWindow>    m_host;             // P0-1.2 fix: QPointer, ImageWindow 析构自动置 null
    ImageCanvas*             m_canvas = nullptr;
    QGraphicsEllipseItem*    m_brushCursor = nullptr;  // 占位, 实际 brush cursor 在 host
    QGraphicsScene*          m_scene = nullptr;
    cv::Mat                  m_backup;
    bool                     m_enabled    = false;
    bool                     m_strokeOpen = false;
    Type                     m_type       = Pixelate;
    int                      m_size       = 20;
};
