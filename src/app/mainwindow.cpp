#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "docwindow.h"
#include "homepage.h"
#include "infotreedock.h"
#include "imagewindow.h"
#include "../media/selection/SelectionCommand.h"
#include "../media/filters/FilterStrategy.h"
#include "../media/filters/FilterFactory.h"
#include "../media/filters/FilterCommand.h"
#include "../media/filters/FilterDialog.h"
#include "../media/filters/liquify/LiquifyDialog.h"
#include "../media/filters/liquify/LiquifyCommand.h"
#include "../media/masks/RefineEdge.h"
#include "../media/masks/ColorRange.h"
#include "../media/mediators/ToolMediator.h"
#include "../media/imageworker/layers/Layer.h"
#include "../media/imageworker/layers/LayerMaskCommand.h"
#include <opencv2/imgproc.hpp>
#include <QInputDialog>
#include "logger.h"
#include "recentmanager.h"
#include "settingsdialog.h"
#include "themegallerydialog.h"
#include "logindialog.h"
#include "../core/MediaDispatcher.h"
#include "../core/FileExtensionRegistry.h"
#include "../core/ThemeManager.h"
#include "../core/SessionManager.h"

#include <QTimer>
#include "../media/mediators/WorkspaceMediator.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QShowEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QWidget>
#include <QWindow>

namespace {
inline constexpr const char *kAppVersion = "1.0.0";

// tab 关闭期间: 阻止 reorder / tabMoved 重排
bool s_closing = false;

// 主题感知地给 pin 按钮上色 (不走全局 QSS, 直接 setStyleSheet)
void applyPinButtonStyle(QToolButton *pin, bool checked)
{
    const auto &p = ThemeManager::instance().palette();
    const QColor col = checked ? p.accent : p.textSubtle;
    pin->setStyleSheet(QString(
        "QToolButton { background: transparent; border: 0; padding: 0; margin: 0;"
        "  color: %1; font-size: 14px; font-weight: 600; }"
        "QToolButton:hover { color: %2; }"
    ).arg(col.name()).arg(p.accent.name()));
}

// 自己画的 close 按钮 (跟 pin 放在同一个 RightSide 容器里)
void applyCloseButtonStyle(QToolButton *btn)
{
    const auto &p = ThemeManager::instance().palette();
    btn->setStyleSheet(QString(
        "QToolButton { background: transparent; border: 0; padding: 0; margin: 0;"
        "  color: %1; font-size: 14px; font-weight: 600; }"
        "QToolButton:hover { color: %2; background: %3; border-radius: 3px; }"
    )
    .arg(p.textSubtle.name())
    .arg(p.tabBorderSelected.name())
    .arg(p.tabBgHover.name()));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{

    setWindowFlags(Qt::FramelessWindowHint);

    ui->setupUi(this);

    // (3D 模块移除, 2026-09-02: 模式读取 + 模式按钮创建已删除)
    //   后续如果需要多模态切分 (图片/视频/音频), 在这里加回

    //m_btnTheme = new QToolButton(this);
    //m_btnTheme->setText(tr("🎨"));
    //m_btnTheme->setToolTip(tr("切换亮色/暗色主题 (Ctrl+T)"));
    //m_btnTheme->setCursor(Qt::PointingHandCursor);
    //m_btnTheme->setObjectName(QStringLiteral("themeBtn"));

    buildActions();
    buildTitleBar();        // 替代 buildMenuCorner: 自绘标题栏 (3 段 + 窗控)
    buildInfoTree();
    // PS/WPS/VS 风格主页切换 (2026-09-10): HomePage 装到 mainStack page 0 (homePageContainer)
    installHomePage();
    buildThemeConnections();

    // F-F (2026-09-09): 工作区 Mediator (临时 mainwindow 持有, F-G 改 imagewindow 共享)
    m_workspaceMed = std::make_unique<mediators::WorkspaceMediator>(this);
    LOG_INFO("[F-F] WorkspaceMediator created, current={}", static_cast<int>(m_workspaceMed->currentWorkspace()));

    // 应用主题
    applyTheme();

    // 语言
    QSettings s;
    const QString lang = s.value(QStringLiteral("App/language"), QStringLiteral("zh_CN")).toString();
    if (lang != QLatin1String("zh_CN")) {
        auto *tr = new QTranslator(this);
        if (tr->load(QStringLiteral(":/translatefile/vistella_%1.qm").arg(lang))) {
            QApplication::installTranslator(tr);
        }
    }

    updateWindowTitle();
    statusBar()->showMessage(tr("就绪"));

    // 加载窗口模式 + normal 尺寸 (QSettings -> m_mode + m_normalSize, 然后 show)
    loadWindowState();

    // 注: 不在这里自动恢复 session, 由 main.cpp splash 关闭后
    //     弹窗问用户"是否恢复", 然后调 restoreSessionFiles()
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange) {
        // 关键: 同步窗控按钮文字 + m_isMaximized
        //   外部触发 (双击标题栏, 系统快捷键, showMaximized() 等) 都会触发
        m_isMaximized = isMaximized();
        if (m_btnMax) {
            m_btnMax->setText(m_isMaximized ? QStringLiteral("\u2750")  // ❐
                                             : QStringLiteral("\u25A1")); // ▢
        }
    } else if (e->type() == QEvent::LanguageChange) {
        retranslateUiTexts();
        updateWindowTitle();
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    // F-G.4 (2026-09-09): 主页 tab 移除, 不再需要刷 home tab 文字
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // 询问所有 dirty 文档
    QList<QWidget *> dirtyDocs;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget *w = ui->tabWidget->widget(i);
        bool dirty = false;
        if (auto *d = qobject_cast<DocWindow *>(w))   dirty = d->isDirty();
        else if (auto *img = qobject_cast<ImageWindow *>(w)) dirty = img->isDirty();
        if (dirty) dirtyDocs << w;
    }

    for (auto *w : dirtyDocs) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("保存确认"));
        box.setText(tr("文档 \"%1\" 有未保存的修改。\n是否保存?")
                    .arg(ui->tabWidget->tabText(ui->tabWidget->indexOf(w))));
        QPushButton *btnSave    = box.addButton(tr("保存"), QMessageBox::AcceptRole);
        QPushButton *btnDiscard = box.addButton(tr("不保存"), QMessageBox::DestructiveRole);
        QPushButton *btnCancel  = box.addButton(tr("取消"), QMessageBox::RejectRole);
        box.setDefaultButton(btnSave);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        if (qobject_cast<QPushButton *>(clicked) == btnCancel) { e->ignore(); return; }
        if (qobject_cast<QPushButton *>(clicked) == btnSave) {
            if (auto *d = qobject_cast<DocWindow *>(w)) {
                QString err;
                if (d->filePath().isEmpty()) {
                    const QString p = QFileDialog::getSaveFileName(
                        this, tr("另存为"),
                        QDir::homePath() + QStringLiteral("/untitled.txt"),
                        defaultFileDialogFilter());
                    if (p.isEmpty()) { e->ignore(); return; }
                    if (!d->saveAsFile(p, &err)) { e->ignore(); return; }
                } else if (!d->saveFile(&err)) { e->ignore(); return; }
            }
        }
        if(qobject_cast<QPushButton*>(clicked)==btnDiscard){

        }
    }
    // 标记为"正常退出", 下次启动不会触发恢复弹窗
    // (异常退出: 程序崩溃 / kill -9 / 断电, closeEvent 不会触发, flag 保持 false)
    SessionManager::instance().setLastExitClean(true);
    saveSession();
    // 只保存 mode (2026-09-09 user 拍板: m_normalSize 写死 1280x800, 不持久化)
    {
        QSettings s;
        s.setValue(QStringLiteral("window/mode"), int(m_mode));
        LOG_INFO("[State] closeEvent save m_mode={} (m_normalSize not persisted)", static_cast<int>(m_mode));
    }
    QMainWindow::closeEvent(e);
}

// ----------------- 主题 -----------------

void MainWindow::buildThemeConnections()
{
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
}

void MainWindow::onThemeChanged()
{
    applyTheme();
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(ThemeManager::instance().globalStyleSheet());
}

void MainWindow::onToggleTheme()
{
    ThemeManager::instance().toggleTheme();
}

// ----------------- 构建 -----------------

void MainWindow::buildActions()
{
    // ----- 文件 -----
    m_actNew      = new QAction(tr("新建"),       this);
    m_actOpen     = new QAction(tr("打开..."),    this);
    m_actSave     = new QAction(tr("保存"),       this);
    m_actSaveAs   = new QAction(tr("另存为..."), this);
    // P0-8.2 (2026-09-15): 多格式导出 (PS 同款 Save For Web)
    m_actExport   = new QAction(tr("导出..."),     this);
    m_actCloseCur = new QAction(tr("关闭当前"), this);
    m_actCloseOthers   = new QAction(tr("关闭其他"),   this);
    m_actCloseSameType = new QAction(tr("关闭同类型"), this);
    m_actCloseAll = new QAction(tr("关闭全部"), this);

    // ----- 编辑 -----
    m_actUndo     = new QAction(tr("撤消"), this);
    m_actRedo     = new QAction(tr("重做"), this);
    m_actCut      = new QAction(tr("剪切"), this);
    m_actCopy     = new QAction(tr("复制"), this);
    m_actPaste    = new QAction(tr("粘贴"), this);
    m_actSelAll   = new QAction(tr("全选"), this);
    m_actFind     = new QAction(tr("查找"), this);
    m_actReplace  = new QAction(tr("替换"), this);

    // ----- 视图 -----
    m_actZoomIn   = new QAction(tr("放大"), this);
    m_actZoomOut  = new QAction(tr("缩小"), this);
    m_actResetLayout = new QAction(tr("重置布局"), this);
    m_actToggleTheme = new QAction(tr("切换主题"), this);

    // ----- 模式 (3D 模块已移除, 2026-09-02: 模式选择删除) -----
    //   后续如果需要多模态切分 (图片/视频/音频), 在这里加回 QActionGroup

    // ----- 快捷键 -----
    m_actNew->setShortcut(QKeySequence::New);
    m_actOpen->setShortcut(QKeySequence::Open);
    m_actSave->setShortcut(QKeySequence::Save);
    m_actSaveAs->setShortcut(QKeySequence::SaveAs);
    m_actExport->setShortcut(QKeySequence("Ctrl+Shift+E"));   // PS 同款 Save For Web 快捷键 (实际 PS 是 Ctrl+Alt+Shift+S, 但 Ctrl+Shift+E 跟浏览器一致)
    m_actCloseCur->setShortcut(QKeySequence::Close);                  // Ctrl+W
    m_actCloseAll->setShortcut(QKeySequence("Ctrl+Shift+W"));
    m_actUndo->setShortcut(QKeySequence::Undo);                        // Ctrl+Z
    m_actRedo->setShortcut(QKeySequence::Redo);                        // Ctrl+Y
    m_actCut->setShortcut(QKeySequence::Cut);
    m_actCopy->setShortcut(QKeySequence::Copy);
    m_actPaste->setShortcut(QKeySequence::Paste);
    m_actSelAll->setShortcut(QKeySequence::SelectAll);
    m_actFind->setShortcut(QKeySequence::Find);
    m_actReplace->setShortcut(QKeySequence::Replace);
    m_actZoomIn->setShortcut(QKeySequence::ZoomIn);
    m_actZoomOut->setShortcut(QKeySequence::ZoomOut);
    m_actToggleTheme->setShortcut(QKeySequence("Ctrl+Shift+T"));  // P0-6.5: 释放 Ctrl+T 给 Free Transform (PS 风格)
    m_actFreeTransform = new QAction(tr("自由变换"), this);
    m_actFreeTransform->setShortcut(QKeySequence("Ctrl+T"));
    m_actImageFlipH = new QAction(tr("水平翻转"), this);
    m_actImageFlipV = new QAction(tr("垂直翻转"), this);
    m_actImageRotate90CW = new QAction(tr("旋转 90° 顺时针"), this);
    m_actImageRotate90CW->setShortcut(QKeySequence("Ctrl+]"));
    m_actImageRotate90CCW = new QAction(tr("旋转 90° 逆时针"), this);
    m_actImageRotate90CCW->setShortcut(QKeySequence("Ctrl+["));
    m_actImageRotate180 = new QAction(tr("旋转 180°"), this);

    // ----- connect -----
    connect(m_actNew,      &QAction::triggered, this, &MainWindow::onNewFile);
    connect(m_actOpen,     &QAction::triggered, this, &MainWindow::onOpenFile);
    connect(m_actSave,     &QAction::triggered, this, &MainWindow::onSaveFile);
    connect(m_actSaveAs,   &QAction::triggered, this, &MainWindow::onSaveAsFile);
    connect(m_actExport,   &QAction::triggered, this, &MainWindow::onExportFile);   // P0-8.2
    connect(m_actCloseCur, &QAction::triggered, this, &MainWindow::onCloseCurrent);
    connect(m_actCloseOthers, &QAction::triggered, this, &MainWindow::onCloseOthers);
    connect(m_actCloseSameType, &QAction::triggered, this, &MainWindow::onCloseSameType);
    connect(m_actCloseAll, &QAction::triggered, this, &MainWindow::onCloseAll);
    connect(m_actUndo,     &QAction::triggered, this, &MainWindow::onUndo);
    connect(m_actRedo,     &QAction::triggered, this, &MainWindow::onRedo);
    connect(m_actCut,      &QAction::triggered, this, &MainWindow::onCut);
    connect(m_actCopy,     &QAction::triggered, this, &MainWindow::onCopy);
    connect(m_actPaste,    &QAction::triggered, this, &MainWindow::onPaste);
    connect(m_actSelAll,   &QAction::triggered, this, &MainWindow::onSelectAll);
    connect(m_actFind,     &QAction::triggered, this, &MainWindow::onFind);
    connect(m_actReplace,  &QAction::triggered, this, &MainWindow::onReplace);
    connect(m_actZoomIn,   &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(m_actZoomOut,  &QAction::triggered, this, &MainWindow::onZoomOut);
    connect(m_actResetLayout, &QAction::triggered, this, [this](){ /* TBD */ });
    connect(m_actToggleTheme, &QAction::triggered, this, &MainWindow::onToggleTheme);
    // P0-6.5: 6 图像变换 action
    connect(m_actFreeTransform,    &QAction::triggered, this, &MainWindow::onFreeTransform);
    connect(m_actImageFlipH,       &QAction::triggered, this, &MainWindow::onImageFlipH);
    connect(m_actImageFlipV,       &QAction::triggered, this, &MainWindow::onImageFlipV);
    connect(m_actImageRotate90CW,  &QAction::triggered, this, &MainWindow::onImageRotate90CW);
    connect(m_actImageRotate90CCW, &QAction::triggered, this, &MainWindow::onImageRotate90CCW);
    connect(m_actImageRotate180,   &QAction::triggered, this, &MainWindow::onImageRotate180);
    // (3D 模块已移除, 模式 action connect 删除)

    // ----- 菜单栏 (第二行) -----
    auto *mb = menuBar();
    mb->setNativeMenuBar(false);   // 在 Windows 强制内嵌, 视觉一致

    QMenu *mFile = mb->addMenu(tr("文件"));
    mFile->addAction(m_actNew);
    mFile->addAction(m_actOpen);
    mFile->addSeparator();
    mFile->addAction(m_actSave);
    mFile->addAction(m_actSaveAs);
    mFile->addAction(m_actExport);   // P0-8.2: 多格式导出
    mFile->addSeparator();
    mFile->addAction(m_actCloseCur);
    mFile->addAction(m_actCloseOthers);
    mFile->addAction(m_actCloseSameType);
    mFile->addAction(m_actCloseAll);

    QMenu *mEdit = mb->addMenu(tr("编辑"));
    mEdit->addAction(m_actUndo);
    mEdit->addAction(m_actRedo);
    mEdit->addSeparator();
    // P0-6.5: 自由变换 (Ctrl+T, PS 风格)
    mEdit->addAction(m_actFreeTransform);
    mEdit->addSeparator();
    mEdit->addAction(m_actCut);
    mEdit->addAction(m_actCopy);
    mEdit->addAction(m_actPaste);
    mEdit->addAction(m_actSelAll);
    mEdit->addSeparator();
    mEdit->addAction(m_actFind);
    mEdit->addAction(m_actReplace);

    QMenu *mView = mb->addMenu(tr("视图"));
    QAction *actShowHome = mView->addAction(tr("主页"));
    actShowHome->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    connect(actShowHome, &QAction::triggered, this, &MainWindow::switchToHomePage);
    mView->addSeparator();
    // 信息树: checkable action, 同步显示/隐藏 + 勾选状态
    QAction *actInfoTree = mView->addAction(tr("信息树"));
    actInfoTree->setCheckable(true);
    actInfoTree->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    if (m_infoDock) actInfoTree->setChecked(m_infoDock->isVisible());
    connect(actInfoTree, &QAction::toggled, this, [this](bool on){
        if (m_infoDock) m_infoDock->setVisible(on);
    });
    // Stage D (2026-09-15): 右侧 panel toggle (ImageWindow::setRightPanelVisible)
    //   解决原 bug: setGeometry 浮动覆盖 + 没 toggle 入口
    //   现在 QDockWidget 容器 + 菜单 toggle, dock 关闭后菜单项仍在
    QAction *actRightPanel = mView->addAction(tr("右侧面板"));
    actRightPanel->setCheckable(true);
    actRightPanel->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    actRightPanel->setChecked(true);  // 默认显示
    connect(actRightPanel, &QAction::toggled, this, [this](bool on){
        if (auto* img = qobject_cast<ImageWindow*>(widgetAt(ui->tabWidget->currentIndex()))) {
            img->setRightPanelVisible(on);
        }
    });
    // 切到 imagewindow 时同步菜单勾选状态 (dock 状态可能因其他操作变化)
    //   Stage D 后续优化: emit visibilityChanged 信号接 menu state 同步
    mView->addAction(m_actZoomIn);
    mView->addAction(m_actZoomOut);
    mView->addAction(m_actResetLayout);

    // F-F (2026-09-09): 工作区 submenu (4 个 checkable action + QActionGroup 互斥)
    //   触发 action -> m_workspaceMed->switchWorkspace(id)
    //   m_workspaceMed 临时 mainwindow 持有, F-G 改 imagewindow 共享
    QMenu *mWorkspace = mView->addMenu(tr("工作区"));
    QActionGroup *wsGroup = new QActionGroup(this);
    wsGroup->setExclusive(true);
    auto addWsAction = [this, mWorkspace, wsGroup](mediators::WorkspaceId id, const QString& label) {
        QAction *a = mWorkspace->addAction(label);
        a->setCheckable(true);
        if (m_workspaceMed && m_workspaceMed->currentWorkspace() == id) {
            a->setChecked(true);
        }
        wsGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, id]() {
            if (m_workspaceMed) m_workspaceMed->switchWorkspace(id);
        });
    };
    addWsAction(mediators::WorkspaceId::Basic, tr("基本功能"));
    addWsAction(mediators::WorkspaceId::Photo, tr("摄影"));
    addWsAction(mediators::WorkspaceId::Paint, tr("绘画"));
    addWsAction(mediators::WorkspaceId::Web,   tr("Web"));
    // (主题切换已统一到标题栏 user btn 旁的 ☀ 按钮, 不再放进菜单避免重复)
    //   m_actToggleTheme 字段保留 + 快捷键 Ctrl+T, 但不 addAction 到任何菜单

    // (3D 模块已移除, 模式菜单删除, 2026-09-02)

    QMenu *mTools = mb->addMenu(tr("工具"));
    QAction *aSettings = mTools->addAction(tr("首选项..."));
    connect(aSettings, &QAction::triggered, this, &MainWindow::onSettings);
    QAction *aLang = mTools->addAction(tr("语言"));
    connect(aLang, &QAction::triggered, this, &MainWindow::onSettings);
    // 阶段 0 第 10 步: 主题画廊 + 登录 (UI 骨架, 阶段 5+ 真接入 ThemeManager/AuthClient)
    QAction *aTheme = mTools->addAction(tr("主题画廊..."));
    connect(aTheme, &QAction::triggered, this, &MainWindow::onShowThemeGallery);
    QAction *aLogin = mTools->addAction(tr("登录..."));
    connect(aLogin, &QAction::triggered, this, &MainWindow::onShowLogin);
    // (主题切换从工具菜单移除, 统一从标题栏 ☀ 进入, 2026-09-02)
    //   后续加"选择主题"页面也通过标题栏 ☀ 按钮触发

    QMenu *mHelp = mb->addMenu(tr("帮助"));
    QAction *aAbout = mHelp->addAction(tr("关于 Vistella"));
    connect(aAbout, &QAction::triggered, this, [this](){
        QMessageBox::about(this, tr("关于 Vistella"),
            tr("Vistella %1\n多模式多文档处理平台\n\n基于 Qt %2 · OpenCV %3 · assimp %4")
            .arg(QString::fromLatin1(kAppVersion))
            .arg(QString::fromLatin1(qVersion()))
            .arg(QString::fromLatin1(CV_VERSION))
            .arg(QString::fromLatin1("6.0.4")));
    });

    // F-J (2026-09-09): 6 主菜单加子菜单 (图像/图层/文字/选择/滤镜)
    //   F-L/F-N 阶段 action 接实际功能
    //   当前 placeholder: action triggered → LOG_INFO + statusBar message (统一 notImpl lambda)
    auto notImpl = [this](const QString& menuName, const QString& actionName) {
        return [this, menuName, actionName]() {
            LOG_INFO("[F-J] {} > {} 触发 (待 F-L/F-N 实装)", menuName.toStdString(), actionName.toStdString());
            statusBar()->showMessage(tr("%1 > %2 将在后续版本实现").arg(menuName, actionName), 3000);
        };
    };

    // ---- 图像 (Image) ----
    QMenu *mImage = mb->addMenu(tr("图像"));
    QAction *aImgResize = mImage->addAction(tr("调整大小..."));
    aImgResize->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+I")));
    connect(aImgResize, &QAction::triggered, this, notImpl(tr("图像"), tr("调整大小")));
    QAction *aImgConvert = mImage->addAction(tr("转换格式..."));
    connect(aImgConvert, &QAction::triggered, this, notImpl(tr("图像"), tr("转换格式")));
    QAction *aImgCrop = mImage->addAction(tr("裁剪..."));
    aImgCrop->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+C")));
    connect(aImgCrop, &QAction::triggered, this, notImpl(tr("图像"), tr("裁剪")));
    mImage->addSeparator();
    // P0-6.5: 5 翻转/旋转 action (接 m_actImageXxx, 不再用 placeholder)
    mImage->addAction(m_actImageFlipH);
    mImage->addAction(m_actImageFlipV);
    mImage->addAction(m_actImageRotate90CW);
    mImage->addAction(m_actImageRotate90CCW);
    mImage->addAction(m_actImageRotate180);

    // ---- 图层 (Layer) ----
    QMenu *mLayer = mb->addMenu(tr("图层"));
    QAction *aLayerNew = mLayer->addAction(tr("新建图层"));
    aLayerNew->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    connect(aLayerNew, &QAction::triggered, this, notImpl(tr("图层"), tr("新建图层")));
    QAction *aLayerDup = mLayer->addAction(tr("复制图层"));
    aLayerDup->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    connect(aLayerDup, &QAction::triggered, this, notImpl(tr("图层"), tr("复制图层")));
    QAction *aLayerDel = mLayer->addAction(tr("删除图层"));
    connect(aLayerDel, &QAction::triggered, this, notImpl(tr("图层"), tr("删除图层")));
    mLayer->addSeparator();
    QAction *aLayerUp = mLayer->addAction(tr("上移一层"));
    connect(aLayerUp, &QAction::triggered, this, notImpl(tr("图层"), tr("上移一层")));
    QAction *aLayerDown = mLayer->addAction(tr("下移一层"));
    connect(aLayerDown, &QAction::triggered, this, notImpl(tr("图层"), tr("下移一层")));
    QAction *aLayerMerge = mLayer->addAction(tr("合并可见图层"));
    connect(aLayerMerge, &QAction::triggered, this, notImpl(tr("图层"), tr("合并可见图层")));
    QAction *aLayerFlatten = mLayer->addAction(tr("拼合图像"));
    connect(aLayerFlatten, &QAction::triggered, this, notImpl(tr("图层"), tr("拼合图像")));

    // P1.3.4/5/6/7 (2026-09-17): 图层蒙版 5 个真实操作
    mLayer->addSeparator();
    QAction *aMaskPaint = mLayer->addAction(tr("编辑像素蒙版 (Paint Mask)"));
    connect(aMaskPaint, &QAction::triggered, this, [this]{
        onSwitchToMaskBrush();
    });
    QAction *aMaskFromSelection = mLayer->addAction(tr("从选区添加像素蒙版"));
    connect(aMaskFromSelection, &QAction::triggered, this, [this]{
        onAddPixelMaskFromSelection();
    });
    QAction *aMaskRefine = mLayer->addAction(tr("调整蒙版边缘 (Refine Edge)..."));
    connect(aMaskRefine, &QAction::triggered, this, [this]{
        onRefineMaskEdge();
    });
    QAction *aMaskColorRange = mLayer->addAction(tr("颜色范围 (Color Range)..."));
    connect(aMaskColorRange, &QAction::triggered, this, [this]{
        onColorRangeMask();
    });

    // ---- 文字 (Text) ----
    QMenu *mText = mb->addMenu(tr("文字"));
    QAction *aTextFont = mText->addAction(tr("字体..."));
    connect(aTextFont, &QAction::triggered, this, notImpl(tr("文字"), tr("字体")));
    QAction *aTextSize = mText->addAction(tr("字号..."));
    connect(aTextSize, &QAction::triggered, this, notImpl(tr("文字"), tr("字号")));
    QAction *aTextColor = mText->addAction(tr("颜色..."));
    connect(aTextColor, &QAction::triggered, this, notImpl(tr("文字"), tr("颜色")));
    mText->addSeparator();
    QAction *aTextBold = mText->addAction(tr("粗体"));
    aTextBold->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(aTextBold, &QAction::triggered, this, notImpl(tr("文字"), tr("粗体")));
    QAction *aTextItalic = mText->addAction(tr("斜体"));
    aTextItalic->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(aTextItalic, &QAction::triggered, this, notImpl(tr("文字"), tr("斜体")));

    // ---- 选择 (Select) ----
    QMenu *mSelect = mb->addMenu(tr("选择"));
    QAction *aSelAll = mSelect->addAction(tr("全部"));
    aSelAll->setShortcut(QKeySequence(QStringLiteral("Ctrl+A")));
    connect(aSelAll, &QAction::triggered, this, &MainWindow::onImageSelectAll);
    QAction *aSelNone = mSelect->addAction(tr("取消选择"));
    aSelNone->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(aSelNone, &QAction::triggered, this, &MainWindow::onImageDeselect);
    QAction *aSelInverse = mSelect->addAction(tr("反选"));
    aSelInverse->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    connect(aSelInverse, &QAction::triggered, this, &MainWindow::onImageInverse);
    mSelect->addSeparator();
    QAction *aSelFeather = mSelect->addAction(tr("羽化..."));
    connect(aSelFeather, &QAction::triggered, this, notImpl(tr("选择"), tr("羽化")));

    // ---- 滤镜 (Filter) - P0-5 (2026-09-10) 4 action 接真 ----
    QMenu *mFilter = mb->addMenu(tr("滤镜"));
    QAction *aFilterBlur = mFilter->addAction(tr("模糊..."));
    connect(aFilterBlur, &QAction::triggered, this, [this]{
        onFilterMenuTriggered(filter::FilterKind::GaussianBlur);
    });
    QAction *aFilterSharpen = mFilter->addAction(tr("锐化..."));
    connect(aFilterSharpen, &QAction::triggered, this, [this]{
        onFilterMenuTriggered(filter::FilterKind::Sharpen);
    });
    QAction *aFilterEmboss = mFilter->addAction(tr("浮雕..."));
    connect(aFilterEmboss, &QAction::triggered, this, [this]{
        onFilterMenuTriggered(filter::FilterKind::Emboss);
    });
    mFilter->addSeparator();
    QAction *aFilterColorAdjust = mFilter->addAction(tr("色彩调整..."));
    connect(aFilterColorAdjust, &QAction::triggered, this, [this]{
        onFilterMenuTriggered(filter::FilterKind::PhotoFilter);
    });
    mFilter->addSeparator();
    // P1.2.5+6+8 (2026-09-16): Liquify 滤镜 (交互式液化: 6 工具 + 网格 + 冻结 + 撤销)
    QAction *aFilterLiquify = mFilter->addAction(tr("液化..."));
    connect(aFilterLiquify, &QAction::triggered, this, [this]{
        onLiquifyTriggered();
    });
}

// =============================================================
// 自绘标题栏 (L2 方案)
//   3 段: left(logo+name+version) | mid(menu+global tools) | right(mode+theme+user+winctrl)
//   通过 setMenuWidget() 嵌入 QMainWindow 顶部, 替代原生 menubar
//   扩展性: titleBarLeft/Mid/Right() 是 public getter, 后续加按钮直接 addWidget
// =============================================================
void MainWindow::buildTitleBar()
{
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(32);
    // 关键: 标题栏的 mouse 事件能被 MainWindow 的 mousePressEvent 收到
    //       (因为 QWidget 事件向上传播), 所以不需要 install eventFilter

    auto *hl = new QHBoxLayout(m_titleBar);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    m_titleLeft  = new QFrame(m_titleBar);
    m_titleMid   = new QFrame(m_titleBar);
    m_titleRight = new QFrame(m_titleBar);
    m_titleLeft->setObjectName(QStringLiteral("titleLeft"));
    m_titleMid->setObjectName(QStringLiteral("titleMid"));
    m_titleRight->setObjectName(QStringLiteral("titleRight"));
    // 关键: 3 个 QFrame 完全不画 frame + 不画背景, 避免出现"白色矩形"
    //   QFrame 默认 frameShape=StyledPanel + frameShadow=Plain 会画 1px line,
    //   即使 background: transparent 也会因为 palette(Base) 露出浅色块覆盖 dock 下边线
    for (QFrame *f : { m_titleLeft, m_titleMid, m_titleRight }) {
        f->setFrameShape(QFrame::NoFrame);
        f->setFrameShadow(QFrame::Plain);
        f->setLineWidth(0);
        f->setMidLineWidth(0);
        f->setAutoFillBackground(false);
    }

    buildLeftArea();
    buildMidArea();
    buildRightArea();

    hl->addWidget(m_titleLeft);
    hl->addWidget(m_titleMid, /*stretch*/ 1);  // 中段弹性填充
    hl->addWidget(m_titleRight);

    // 关键: setMenuWidget 替代 menuBar 位置, 视觉上"标题栏"在最顶部
    setMenuWidget(m_titleBar);
}

void MainWindow::buildLeftArea()
{
    auto *l = new QHBoxLayout(m_titleLeft);
    l->setContentsMargins(10, 0, 12, 0);
    l->setSpacing(8);

    // logo: 纯文字占位 (无外部资源), 后续准备 SVG 后换 QPixmap
    auto *logo = new QLabel(QStringLiteral("M"), m_titleLeft);
    logo->setObjectName(QStringLiteral("titleLogo"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(20, 20);
    l->addWidget(logo);

    // app name + version (单行显示)
    const QString nameAndVer = QStringLiteral("%1 %2")
        .arg(qApp->applicationName(), qApp->applicationVersion());
    auto *name = new QLabel(nameAndVer, m_titleLeft);
    name->setObjectName(QStringLiteral("titleAppName"));
    name->setFixedHeight(30);
    l->addWidget(name);
}

void MainWindow::buildMidArea()
{
    auto *l = new QHBoxLayout(m_titleMid);
    l->setContentsMargins(4, 0, 4, 0);
    l->setSpacing(0);

    // 重用现有 QMenuBar: 重新 parent 到 titleBarMid
    //   menuBar() 拿到的是 QMainWindow 内部 menubar (空, buildActions 里 addMenu 加菜单)
    //   setNativeMenuBar(false) 已在 buildActions 里调过, Windows 强制内嵌
    auto *mb = menuBar();
    mb->setParent(m_titleMid);
    mb->setObjectName(QStringLiteral("titleMenuBar"));
    l->addWidget(mb);

    // 弹性空间: 后续可在这之后插入 VS 风格全局工具条
    //   m_titleMid->layout()->insertWidget(count-1, globalToolBar);
    l->addStretch(1);
}

void MainWindow::buildRightArea()
{
    auto *l = new QHBoxLayout(m_titleRight);
    l->setContentsMargins(8, 0, 0, 0);
    l->setSpacing(4);

    // (3D 模块已移除, 2026-09-02: mode corner 整段删除, modeBtnMod/MM 字段已删)
    //   后续如果需要标题栏右侧的快速切换 (例如多模态切分), 在这里加回

    // 1) theme 按钮 (新)
    m_btnTheme = new QToolButton(m_titleRight);
    m_btnTheme->setText(QStringLiteral("\u2600"));  // ☀
    m_btnTheme->setToolTip(tr("切换主题 (Ctrl+T)"));
    m_btnTheme->setObjectName(QStringLiteral("titleThemeBtn"));
    m_btnTheme->setCursor(Qt::PointingHandCursor);
    l->addWidget(m_btnTheme);
    connect(m_btnTheme, &QToolButton::clicked, this, &MainWindow::onToggleTheme);

    // 3) user 按钮 (新, 后续接登录系统)
    m_btnUser = new QToolButton(m_titleRight);
    m_btnUser->setText(tr("未登录"));
    m_btnUser->setToolTip(tr("点击登录 / 查看用户信息"));
    m_btnUser->setObjectName(QStringLiteral("titleUserBtn"));
    m_btnUser->setCursor(Qt::PointingHandCursor);
    m_btnUser->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    l->addWidget(m_btnUser);
    // 阶段 0 第 10 步: user 按钮触发 LoginDialog (UI 骨架, 阶段 5+ 真接 AuthClient)
    connect(m_btnUser, &QToolButton::clicked, this, &MainWindow::onShowLogin);

    // 4) 窗口控制按钮 (最右)
    m_btnMin = new QPushButton(QStringLiteral("\u2014"), m_titleRight);  // —
    m_btnMax = new QPushButton(QStringLiteral("\u25A1"), m_titleRight);  // ▢
    m_btnClose = new QPushButton(QStringLiteral("\u2715"), m_titleRight); // ✕
    m_btnMin->setObjectName(QStringLiteral("titleBtnMin"));
    m_btnMax->setObjectName(QStringLiteral("titleBtnMax"));
    m_btnClose->setObjectName(QStringLiteral("titleBtnClose"));
    m_btnMin->setFixedSize(40, 32);
    m_btnMax->setFixedSize(40, 32);
    m_btnClose->setFixedSize(40, 32);
    m_btnMin->setCursor(Qt::PointingHandCursor);
    m_btnMax->setCursor(Qt::PointingHandCursor);
    m_btnClose->setCursor(Qt::PointingHandCursor);
    l->addWidget(m_btnMin);
    l->addWidget(m_btnMax);
    l->addWidget(m_btnClose);
    connect(m_btnMin,   &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(m_btnMax,   &QPushButton::clicked, this, &MainWindow::onMaximizeRestore);
    connect(m_btnClose, &QPushButton::clicked, this, &MainWindow::close);
}

void MainWindow::onMaximizeRestore()
{
    // 走 setMode 统一切 mode + showMaximized/showNormal + 按钮文字
    setMode(m_mode == WindowMode::Maximized ? WindowMode::Normal : WindowMode::Maximized);
}

// =============================================================
// 简单窗口状态机 (2026-09-09 重建, 9/4+ 工作丢失后)
//   1 个 mode (Normal/Maximized) + 1 个 size (m_normalSize)
//   setMode 统一切 m_mode + showMaximized/showNormal + 按钮文字
//   Normal 模式用 m_normalSize, Maximized 模式用 Qt 默认行为
//   不做 sanity check / fullscreenish 检测 / 复杂 fallback
// =============================================================
void MainWindow::setMode(WindowMode m)
{
    LOG_INFO("[State] setMode({}) enter, current m_mode={} m_normalSize={}x{}",
             static_cast<int>(m), static_cast<int>(m_mode), m_normalSize.width(), m_normalSize.height());
    m_mode = m;
    if (m == WindowMode::Maximized) {
        // 用 Qt 标准 showMaximized(). 不要在 Frameless window 上自己 setGeometry
        //   (Frameless + WM 在切状态时偶尔把窗口拉到 (0,0))
        showMaximized();
    } else {
        // Qt 标准 showNormal() 把窗口拉到 maximized 之前的 normal geometry
        //   如果这是从初始 maximized 状态第一次切到 normal, Qt 不知道 saved geometry
        //   退到 (0,0). 我们显式 setGeometry 居中当前屏幕
        showNormal();
        QScreen *scr = QGuiApplication::screenAt(frameGeometry().center());
        if (!scr) scr = windowHandle() ? windowHandle()->screen() : screen();
        if (!scr) scr = QGuiApplication::primaryScreen();
        if (scr) {
            const QRect avail = scr->availableGeometry();
            const QPoint pos(avail.center().x() - m_normalSize.width() / 2,
                             avail.center().y() - m_normalSize.height() / 2);
            setGeometry(QRect(pos, m_normalSize));
            LOG_INFO("[State] setMode(Normal) setGeometry pos=({},{}) size=({},{}) on screen '{}'",
                     pos.x(), pos.y(), m_normalSize.width(), m_normalSize.height(),
                     scr->name().toLocal8Bit().constData());
        }
    }
    if (m_btnMax) {
        m_btnMax->setText(m == WindowMode::Maximized ? QStringLiteral("\u2750")
                                                    : QStringLiteral("\u25A1"));
    }
    LOG_INFO("[State] setMode({}) exit, current m_mode={} isMaximized={} size={}x{}",
             static_cast<int>(m), static_cast<int>(m_mode), isMaximized(),
             size().width(), size().height());
}

void MainWindow::loadWindowState()
{
    QSettings s;
    const int modeInt = s.value(QStringLiteral("window/mode"), int(WindowMode::Maximized)).toInt();
    m_mode = (modeInt == int(WindowMode::Normal)) ? WindowMode::Normal : WindowMode::Maximized;
    // m_normalSize 不持久化 (2026-09-09 user 拍板: 1280x800 写死, 不让 QSettings 污染)
    //   始终用构造函数初始化的默认 1280x800
    LOG_INFO("[State] loadWindowState read m_mode={} m_normalSize={}x{} (default, not persisted)",
             static_cast<int>(m_mode), m_normalSize.width(), m_normalSize.height());

    // 应用 mode (Stage H 简化 2026-09-15: showMaximized / showNormal 标准用法)
    if (m_mode == WindowMode::Maximized) {
        showMaximized();
    } else {
        showNormal();
        // Normal 路径额外居中 (saved geometry 不可靠)
        if (QScreen *scr = screen()) {
            const QRect avail = scr->availableGeometry();
            setGeometry(QRect(avail.center().x() - m_normalSize.width() / 2,
                              avail.center().y() - m_normalSize.height() / 2,
                              m_normalSize.width(), m_normalSize.height()));
        }
    }
}

// 计算标题栏可拖动区域: titleLeft + titleMid (避开右侧的按钮)
// 用 mapTo(this) 把 child geometry 映射到 MainWindow 坐标
QRect MainWindow::titleBarDragRect() const
{
    if (!m_titleLeft || !m_titleMid) return QRect();
    const QPoint p1 = m_titleLeft->mapTo(this, QPoint(0, 0));
    const QPoint p2 = m_titleMid->mapTo(this, QPoint(m_titleMid->width(), m_titleMid->height()));
    return QRect(p1, p2);
}


void MainWindow::buildInfoTree()
{
    m_infoDock = new InfoTreeDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_infoDock);
    resizeDocks({ m_infoDock }, { 300 }, Qt::Horizontal);
    m_infoDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    m_infoDock->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable);
    // 不允许关闭, 固定嵌入左侧
    m_infoDock->setFeatures(m_infoDock->features() & ~QDockWidget::DockWidgetClosable);
    // 关键: 启动时只显示主页, 信息树默认隐藏
    //   (后续从菜单"视图 → 信息树"显式打开, 或选目录后自动打开)
    m_infoDock->hide();
    // 阶段 0 第 7 步: 信息树里双击/回车文件 → 调 MediaDispatcher 路由
    connect(m_infoDock, &InfoTreeDock::openFileRequested,
            this, [this](const QString &path) { onHomeOpenPathRequested(path); });

    // F-G.4 (2026-09-09): tab connect 移到 buildInfoTree 末尾 (从原 buildHome 拆出)
    //   不再有 home tab 锁住第一位, reorder 逻辑简化 (不再有 m_homeTabIndex 例外)
    connect(ui->tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);

    // 右键菜单
    ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tabWidget->tabBar(), &QWidget::customContextMenuRequested,
            this, &MainWindow::onTabContextMenu);

    // tab 移动结束 - 重排 pinned (防抖: tabMoved 可能连续触发)
    connect(ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [this](int from, int to){
        Q_UNUSED(from); Q_UNUSED(to);
        // 关闭期间: m_tabStates 状态可能不一致 (清理中), 跳过 reorder
        if (s_closing) return;
        QTimer::singleShot(0, this, [this]{
            if (s_closing) return;
            // 校验 m_tabStates 和 tabWidget 一致性: 不一致说明 close 流程没结束, 跳过
            if (m_tabStates.size() != ui->tabWidget->count()) return;
            int n = ui->tabWidget->count();
            int firstUnpinned = -1;
            for (int i = 0; i < n; ++i) {
                auto *ts = tabStateAt(i);
                if (ts && !ts->pinned) { firstUnpinned = i; break; }
            }
            if (firstUnpinned < 0) return;
            for (int i = firstUnpinned + 1; i < n; ++i) {
                auto *ts = tabStateAt(i);
                if (ts && ts->pinned) {
                    ui->tabWidget->tabBar()->moveTab(i, firstUnpinned);
                    firstUnpinned++;
                }
            }
        });
    });
}

// ----------------- Tab 管理 -----------------

MainWindow::DocType MainWindow::docTypeOf(QWidget *w)
{
    if (qobject_cast<DocWindow *>(w))   return TypeText;
    if (qobject_cast<ImageWindow *>(w)) return TypeImage;
    // (3D 模块已移除, 2026-09-02: ModelWindow 检查删除)
    return TypeUnknown;
}

QString MainWindow::docTypeName(DocType t) const
{
    switch (t) {
    case TypeText:  return tr("文本");
    case TypeImage: return tr("图像");
    default:        return QString();
    }
}

QWidget *MainWindow::widgetAt(int index) const
{
    if (index < 0 || index >= ui->tabWidget->count()) return nullptr;
    return ui->tabWidget->widget(index);
}

int MainWindow::indexOfWidget(QWidget *w) const
{
    return ui->tabWidget->indexOf(w);
}

void MainWindow::installTabPinButton(int index)
{
    if (index < 0 || index >= ui->tabWidget->count()) return;
    QWidget *w = ui->tabWidget->widget(index);
    if (!w) return;

    for (auto &ts : m_tabStates) {
        if (ts.widget == w && ts.pinBtn) return;
    }

    // 关键: Qt 一个 side 只能放一个 widget, 所以 pin + close 必须放在同一个容器里
    // 容器作为 RightSide, 里面 [pin] [close] 两个按钮并排 (跟 VSCode/Chrome 风格一致)
    auto *corner = new QWidget(ui->tabWidget->tabBar());
    corner->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *lay = new QHBoxLayout(corner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    // pin 按钮
    auto *pin = new QToolButton(corner);
    pin->setText(QStringLiteral("📌"));
    pin->setToolTip(tr("钉住 / 取消钉住"));
    pin->setCheckable(true);
    pin->setAutoRaise(true);
    pin->setCursor(Qt::PointingHandCursor);
    pin->setFixedSize(22, 22);
    applyPinButtonStyle(pin, false);
    lay->addWidget(pin);

    // close 按钮 (自己画, 自己接信号)
    auto *closeBtn = new QToolButton(corner);
    closeBtn->setText(QStringLiteral("✕"));
    closeBtn->setToolTip(tr("关闭"));
    closeBtn->setAutoRaise(true);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(22, 22);
    applyCloseButtonStyle(closeBtn);
    lay->addWidget(closeBtn);

    // 容器整体放到 RightSide
    ui->tabWidget->tabBar()->setTabButton(index, QTabBar::RightSide, corner);

    TabState ts;
    ts.widget   = w;
    ts.pinBtn   = pin;
    ts.closeBtn = closeBtn;
    ts.pinned   = false;
    m_tabStates << ts;

    // 主题切换: 全部 pin + close 按钮重绘
    static bool s_themeConnOnce = false;
    if (!s_themeConnOnce) {
        s_themeConnOnce = true;
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]{
            for (const auto &ts : m_tabStates) {
                if (ts.pinBtn)   applyPinButtonStyle(ts.pinBtn, ts.pinned);
                if (ts.closeBtn) applyCloseButtonStyle(ts.closeBtn);
            }
        });
    }

    // close 按钮: 自己接 clicked -> onTabCloseRequested (用 widget 反查 index, moveTab 不会变)
    connect(closeBtn, &QToolButton::clicked, this, [this, w]{
        int i = ui->tabWidget->indexOf(w);
        if (i >= 0) onTabCloseRequested(i);
    });

    // pin 按钮: toggled -> 标记 + reorder
    connect(pin, &QToolButton::toggled, this, [this, pin](bool checked) {
        for (int i = 0; i < m_tabStates.size(); ++i) {
            if (m_tabStates[i].pinBtn == pin) {
                m_tabStates[i].pinned = checked;
                applyPinButtonStyle(pin, checked);
                break;
            }
        }
        if (!s_closing) reorderPinnedToLeft();
    });
}

int MainWindow::pinButtonIndex(QObject *btn) const
{
    for (int i = 0; i < m_tabStates.size(); ++i) {
        if (m_tabStates[i].pinBtn == btn) return ui->tabWidget->indexOf(m_tabStates[i].widget);
    }
    return -1;
}

MainWindow::TabState *MainWindow::tabStateAt(int index)
{
    QWidget *w = widgetAt(index);
    if (!w) return nullptr;
    for (int i = 0; i < m_tabStates.size(); ++i) {
        if (m_tabStates[i].widget == w) return &m_tabStates[i];
    }
    return nullptr;
}

void MainWindow::onTabPinToggled(int index, bool pinned)
{
    auto *ts = tabStateAt(index);
    if (!ts) return;
    ts->pinned = pinned;
    reorderPinnedToLeft();
}

void MainWindow::reorderPinnedToLeft()
{
    int n = ui->tabWidget->count();
    if (n <= 1) return;

    // 防重入: moveTab 会触发 tabMoved -> 重新进入这个函数
    static bool s_inReorder = false;
    if (s_inReorder) return;
    s_inReorder = true;
    struct Guard { bool &b; ~Guard(){ b = false; } } g{s_inReorder};

    QList<int> pinnedOrder, unpinnedOrder;
    for (int i = 0; i < n; ++i) {
        auto *ts = tabStateAt(i);
        if (ts && ts->pinned) pinnedOrder << i;
        else                  unpinnedOrder << i;
    }
    QList<int> target = pinnedOrder + unpinnedOrder;

    bool same = true;
    for (int i = 0; i < n; ++i) if (target[i] != i) { same = false; break; }
    if (same) return;

    for (int i = 0; i < n; ++i) {
        if (target[i] == i) continue;
        int from = target[i];
        int to   = i;
        ui->tabWidget->tabBar()->moveTab(from, to);
        for (int j = 0; j < n; ++j) {
            if (target[j] == from) target[j] = to;
            else if (from < target[j] && target[j] <= to) target[j]--;
        }
        target[i] = i;
    }
}

void MainWindow::refreshTabTitle(int index)
{
    if (index < 0 || index >= ui->tabWidget->count()) return;
    QWidget *w = ui->tabWidget->widget(index);
    QString title;
    bool dirty = false;
    QString filePath;
    if (auto *d = qobject_cast<DocWindow *>(w)) {
        title = d->displayTitle();
        dirty = d->isDirty();
        filePath = d->filePath();
    } else if (auto *img = qobject_cast<ImageWindow *>(w)) {
        title = img->displayTitle();
        dirty = img->isDirty();
        filePath = img->filePath();
    } else {
        title = w->windowTitle();
    }
    // 加星号
    if (dirty && !title.startsWith("*")) title.prepend("*");
    ui->tabWidget->setTabText(index, title);

    // tooltip: 完整路径
    const QString tip = filePath.isEmpty() ? title : filePath;
    ui->tabWidget->setTabToolTip(index, tip);

    // 标记颜色
    QColor dirtyColor = ThemeManager::instance().palette().tabDirty;
    if (dirty) {
        ui->tabWidget->tabBar()->setTabTextColor(index, dirtyColor);
    } else {
        ui->tabWidget->tabBar()->setTabTextColor(index, QColor());   // 默认
    }
}

int MainWindow::addDocTab(QWidget *doc)
{
    if (!doc) return -1;
    int idx = ui->tabWidget->addTab(doc, QString());
    refreshTabTitle(idx);
    installTabPinButton(idx);

    auto updateTitle = [this, doc]() {
        int i = ui->tabWidget->indexOf(doc);
        if (i < 0) return;
        refreshTabTitle(i);
        if (ui->tabWidget->currentIndex() == i) updateWindowTitle();
    };
    if (auto *d = qobject_cast<DocWindow *>(doc)) {
        connect(d, &DocWindow::dirtyChanged,    this, updateTitle);
        connect(d, &DocWindow::filePathChanged,  this, updateTitle);
    } else if (auto *img = qobject_cast<ImageWindow *>(doc)) {
        connect(img, &ImageWindow::dirtyChanged,    this, updateTitle);
        connect(img, &ImageWindow::filePathChanged,  this, updateTitle);
        // ImageWindow 自身要求关闭 (Ctrl+W 或工具栏关闭按钮) -> 走 tabCloseRequested
        connect(img, &ImageWindow::closeRequested, this, [this, doc]{
            int i = ui->tabWidget->indexOf(doc);
            if (i >= 0) onTabCloseRequested(i);
        });
    }
    return idx;
}

int MainWindow::addImageTab(ImageWindow *img) { return addDocTab(img); }

// (3D 模块已移除, 2026-09-02: addModelTab 删除)

void MainWindow::focusOrCreateWidget(QWidget *doc)
{
    if (!doc) return;
    int idx = ui->tabWidget->indexOf(doc);
    if (idx < 0) idx = addDocTab(doc);
    if (idx >= 0) ui->tabWidget->setCurrentIndex(idx);
}

// ----------------- 主页 / 全局按钮 -----------------

// PS/WPS/VS 风格主页切换 (2026-09-10): 跟 F-G.4 之前 + F-G.4 临时方案都不同
//   HomePage 是独立 widget, 装在 mainStack page 0 (homePageContainer)
//   tabWidget 装在 mainStack page 1 (工作空间)
//   启动默认 page 0, 新建/打开切到 page 1, 关闭所有 tab 切回 page 0
//   跟 F-G.4 之前 "HomePage 是 tab" 方案不同, 也跟 F-G.4 临时 "HomePage dialog" 方案不同
void MainWindow::installHomePage()
{
    m_homePage = new HomePage(this);
    m_homePage->setAttribute(Qt::WA_DeleteOnClose, false);
    // HomePage 信号转发到 mainwindow slot
    connect(m_homePage, &HomePage::newFileRequested,    this, &MainWindow::onHomeNewRequested);
    connect(m_homePage, &HomePage::openFileRequested,   this, &MainWindow::onHomeOpenRequested);
    connect(m_homePage, &HomePage::openFolderRequested, this, &MainWindow::onHomeOpenFolderRequested);
    connect(m_homePage, &HomePage::openPathRequested,   this, &MainWindow::onHomeOpenPathRequested);
    connect(m_homePage, &HomePage::moduleRequested,     this, [this](HomeModule m) {
        onHomeModuleRequested(static_cast<int>(m));
    });
    // 装到 mainStack page 0 (homePageContainer)
    if (ui && ui->homePageContainer) {
        QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->homePageContainer->layout());
        if (layout) {
            layout->addWidget(m_homePage);
        } else {
            ui->homePageContainer->layout()->addWidget(m_homePage);
        }
    }
    // 启动默认 page 0 (HomePage)
    if (ui && ui->mainStack) {
        ui->mainStack->setCurrentIndex(0);
    }
    LOG_INFO("[MainWindow] HomePage installed at mainStack page 0 (homePageContainer)");
}

void MainWindow::switchToHomePage()
{
    // 切回 HomePage (不关闭当前 doc tabs, 用户可再切回工作空间)
    if (ui && ui->mainStack) {
        ui->mainStack->setCurrentIndex(0);
        LOG_INFO("[MainWindow] switchToHomePage: tabWidget has {} tabs preserved",
                 ui->tabWidget ? ui->tabWidget->count() : 0);
    }
}

void MainWindow::switchToWorkspace()
{
    // 切到工作空间 (新建/打开文件后)
    if (ui && ui->mainStack) {
        ui->mainStack->setCurrentIndex(1);
    }
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index);
    updateWindowTitle();
    // 切换时也刷新一下 tooltip (主题色变化时)
    for (int i = 0; i < ui->tabWidget->count(); ++i) refreshTabTitle(i);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index < 0 || index >= ui->tabWidget->count()) return;
    QWidget *w = ui->tabWidget->widget(index);
    if (!w) return;

    // 关闭期间阻止 reorder / tabMoved 重排
    s_closing = true;
    struct ClosingGuard { bool &b; ~ClosingGuard(){ b = false; } } g{s_closing};

    // 1) 检查 dirty
    bool dirty = false;
    if (auto *d = qobject_cast<DocWindow *>(w))   dirty = d->isDirty();
    else if (auto *img = qobject_cast<ImageWindow *>(w)) dirty = img->isDirty();

    if (dirty) {
        const auto ret = QMessageBox::question(
            this, tr("保存确认"),
            tr("文档 \"%1\" 有未保存的修改。是否保存?")
                .arg(ui->tabWidget->tabText(index)));
        if (ret == QMessageBox::Cancel) return;
        if (ret == QMessageBox::Yes) {
            if (auto *d = qobject_cast<DocWindow *>(w)) {
                QString err;
                if (d->filePath().isEmpty()) {
                    const QString p = QFileDialog::getSaveFileName(
                        this, tr("另存为"),
                        QDir::homePath() + QStringLiteral("/untitled") + defaultFileExtension(),
                        defaultFileDialogFilter());
                    if (p.isEmpty()) return;   // 取消保存 -> 整个 close 中止
                    if (!d->saveAsFile(p, &err)) return;
                } else if (!d->saveFile(&err)) return;
            } else if (auto *img = qobject_cast<ImageWindow *>(w)) {
                if (img->filePath().isEmpty()) img->saveAsPublic();
                else                            img->savePublic();
                if (img->isDirty()) return;   // 保存失败, 中止关闭
            }
        }
    }

    // 2) 拆 tab 上的"挂件": 取出 corner 容器 (含 pin + close), 防止 removeTab 之后变成 ghost tab
    //    (这一行是修 "关掉 tab 后残留为空白并自动移到主页后" 的关键)
    //    pin + close 现在都在 RightSide 的 corner 容器里, 取出来 deleteLater
    QWidget *corner = ui->tabWidget->tabBar()->tabButton(index, QTabBar::RightSide);
    ui->tabWidget->tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);

    // 3) 清理 m_tabStates (corner deleteLater 会顺带释放 pin/close)
    for (int i = m_tabStates.size() - 1; i >= 0; --i) {
        if (m_tabStates[i].widget == w) {
            m_tabStates.removeAt(i);
            break;
        }
    }
    if (corner) corner->deleteLater();

    // 4) 屏蔽 tabCloseRequested 二次触发, removeTab
    ui->tabWidget->blockSignals(true);
    ui->tabWidget->removeTab(index);
    ui->tabWidget->blockSignals(false);

    // 6) 拆 widget 与 tabWidget 关系, 释放
    //    注意: 不设 WA_DeleteOnClose, 只用 setParent + deleteLater
    //    (WA_DeleteOnClose 是给 close() 用的, 跟 deleteLater 混用会让状态错乱)
    w->setParent(nullptr);
    w->deleteLater();

    updateWindowTitle();

    // PS/WPS/VS 风格 (2026-09-10): 关闭最后一个 doc tab -> 切回 HomePage (page 0)
    if (ui->tabWidget->count() == 0) {
        LOG_INFO("[MainWindow] last doc tab closed -> switch to HomePage");
        switchToHomePage();
    }
}

void MainWindow::onCloseOthers()
{
    for (int i = ui->tabWidget->count() - 1; i >= 0; --i) {
        if (i == -1) continue;
        if (i == ui->tabWidget->currentIndex()) continue;
        onTabCloseRequested(i);
    }
}

void MainWindow::onCloseSameType()
{
    int cur = ui->tabWidget->currentIndex();
    QWidget *curW = widgetAt(cur);
    if (!curW) return;
    DocType curType = docTypeOf(curW);
    if (curType == TypeUnknown) return;

    for (int i = ui->tabWidget->count() - 1; i >= 0; --i) {
        if (i == -1) continue;
        if (i == cur) continue;
        if (docTypeOf(widgetAt(i)) == curType) onTabCloseRequested(i);
    }
}

void MainWindow::onTabContextMenu(const QPoint &pos)
{
    int idx = ui->tabWidget->tabBar()->tabAt(pos);
    if (idx < 0) return;
    QWidget *w = widgetAt(idx);
    if (!w) return;

    QMenu menu(this);
    // PS/WPS/VS 风格 (2026-09-10): 任何 tab 右键都显示"返回主页"
    QAction *aHome = menu.addAction(tr("返回主页"));
    aHome->setEnabled(true);
    connect(aHome, &QAction::triggered, this, &MainWindow::switchToHomePage);

    menu.addSeparator();
    QAction *aClose = menu.addAction(tr("关闭"));
    connect(aClose, &QAction::triggered, this, [this, idx]{ onTabCloseRequested(idx); });

    QAction *aCloseOthers = menu.addAction(tr("关闭其他"));
    connect(aCloseOthers, &QAction::triggered, this, [this, idx]{
        ui->tabWidget->setCurrentIndex(idx);
        onCloseOthers();
    });

    QAction *aCloseSame = menu.addAction(tr("其他同类型一并关闭"));
    connect(aCloseSame, &QAction::triggered, this, [this, idx]{
        ui->tabWidget->setCurrentIndex(idx);
        onCloseSameType();
    });

    menu.addSeparator();
    QAction *aPin = menu.addAction(tr("钉住 / 取消钉住"));
    aPin->setCheckable(true);
    if (auto *ts = tabStateAt(idx)) aPin->setChecked(ts->pinned);
    connect(aPin, &QAction::toggled, this, [this, idx](bool c){
        if (auto *ts = tabStateAt(idx)) {
            if (ts->pinBtn) ts->pinBtn->setChecked(c);
        }
    });
    menu.exec(ui->tabWidget->tabBar()->mapToGlobal(pos));
}

// ----------------- 文件 / 编辑 菜单 -----------------

void MainWindow::onNewFile()
{
    // 阶段 0 第 5 步: 新建默认只建 txt 文本
    //   4 大模块 (imageWorker / audioWorker / videoWorker / visionWorker) 全部通过"打开文件"进入,
    //   没有"新建"的概念 (新建图像工程 / 视频工程留阶段 1+ 单独设计)
    auto *doc = new DocWindow;
    int idx = addDocTab(doc);
    if (idx >= 0) {
        ui->tabWidget->setCurrentIndex(idx);
        // PS/WPS/VS 风格 (2026-09-10): 新建后切到工作空间 (page 1)
        switchToWorkspace();
    }
}

void MainWindow::onOpenFile()
{
    const QString startDir = RecentManager::instance().entries().value(0).filePath.isEmpty()
        ? QDir::homePath()
        : QFileInfo(RecentManager::instance().entries().value(0).filePath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开文件"), startDir, defaultFileDialogFilter());
    if (path.isEmpty()) return;

    onHomeOpenPathRequested(path);
}

void MainWindow::onSaveFile()
{
    auto *w = widgetAt(ui->tabWidget->currentIndex());
    if (!w || w == widgetAt(-1)) return;

    if (auto *d = qobject_cast<DocWindow *>(w)) {
        QString err;
        if (d->filePath().isEmpty()) {
            const QString p = QFileDialog::getSaveFileName(
                this, tr("另存为"),
                QDir::homePath() + QStringLiteral("/untitled") + defaultFileExtension(),
                defaultFileDialogFilter());
            if (p.isEmpty()) return;
            if (!d->saveAsFile(p, &err)) return;
            RecentManager::instance().touchOpen(p, RecentModeMultimedia);
        } else if (!d->saveFile(&err)) {
            QMessageBox::warning(this, tr("保存失败"), err);
        }
        return;
    }

    if (auto *img = qobject_cast<ImageWindow *>(w)) {
        if (img->filePath().isEmpty()) {
            onSaveAsFile();   // 没路径 → 走另存为
        } else {
            img->savePublic();
        }
        return;
    }
}

void MainWindow::onSaveAsFile()
{
    auto *w = widgetAt(ui->tabWidget->currentIndex());
    if (!w || w == widgetAt(-1)) return;

    if (auto *d = qobject_cast<DocWindow *>(w)) {
        const QString suggest = d->filePath().isEmpty()
            ? (QDir::homePath() + QStringLiteral("/untitled") + defaultFileExtension())
            : d->filePath();
        const QString p = QFileDialog::getSaveFileName(
            this, tr("另存为"), suggest, defaultFileDialogFilter());
        if (p.isEmpty()) return;
        QString err;
        if (!d->saveAsFile(p, &err)) {
            QMessageBox::warning(this, tr("保存失败"), err);
            return;
        }
        RecentManager::instance().touchOpen(p, RecentModeMultimedia);
        return;
    }

    if (auto *img = qobject_cast<ImageWindow *>(w)) {
        // ImageWindow 自己的 onSaveAs 弹文件选择框, 这里直接转发
        const QString prevPath = img->filePath();
        img->saveAsPublic();
        // 如果用户选了新路径, 记到最近
        if (!img->filePath().isEmpty() && img->filePath() != prevPath) {
            RecentManager::instance().touchOpen(img->filePath(), RecentModeMultimedia);
        }
    }
}

// P0-8.2 (2026-09-15): 多格式导出 (Ctrl+Shift+E)
void MainWindow::onExportFile()
{
    auto *w = widgetAt(ui->tabWidget->currentIndex());
    if (!w || w == widgetAt(-1)) return;
    if (auto *img = qobject_cast<ImageWindow *>(w)) {
        img->exportPublic();
    }
}

void MainWindow::onCloseCurrent()
{
    int i = ui->tabWidget->currentIndex();
    if (i < 0 || i == -1) return;
    onTabCloseRequested(i);
}

void MainWindow::onCloseAll()
{
    for (int i = ui->tabWidget->count() - 1; i >= 0; --i) {
        if (i == -1) continue;
        onTabCloseRequested(i);
    }
    if (-1 >= 0) ui->tabWidget->setCurrentIndex(-1);
    // PS/WPS/VS 风格 (2026-09-10): 关闭所有 doc -> 切回 HomePage
    switchToHomePage();
}

// ----- Undo / Redo: 真正转发到当前 ImageWindow (一次一步) -----
void MainWindow::onUndo()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->undoPublic();
    } else {
        statusBar()->showMessage(tr("当前页面没有可撤销的操作"), 2000);
    }
}
void MainWindow::onRedo()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->redoPublic();
    } else {
        statusBar()->showMessage(tr("当前页面没有可重做的操作"), 2000);
    }
}
void MainWindow::onCut()     { if (auto *d = qobject_cast<DocWindow *>(widgetAt(ui->tabWidget->currentIndex()))) d->editor()->cut(); }
void MainWindow::onCopy()    { if (auto *d = qobject_cast<DocWindow *>(widgetAt(ui->tabWidget->currentIndex()))) d->editor()->copy(); }
void MainWindow::onPaste()   { if (auto *d = qobject_cast<DocWindow *>(widgetAt(ui->tabWidget->currentIndex()))) d->editor()->paste(); }
void MainWindow::onSelectAll(){ if (auto *d = qobject_cast<DocWindow *>(widgetAt(ui->tabWidget->currentIndex()))) d->editor()->selectAll(); }

// P0-4.7 (2026-09-10): 选择菜单 3 action 实装
//   操作当前 ImageWindow 的 SelectionModel, push SelectionCommand 到 undoStack 支持 undo/redo
void MainWindow::onImageSelectAll()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        if (auto *sel = img->selectionModel()) {
            img->undoStack()->push(new selection::SelectionCommand(sel, selection::SelectionCommand::Op::SelectAll));
        }
    } else {
        statusBar()->showMessage(tr("当前页面没有可全选的图像"), 2000);
    }
}

void MainWindow::onImageDeselect()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        if (auto *sel = img->selectionModel()) {
            img->undoStack()->push(new selection::SelectionCommand(sel, selection::SelectionCommand::Op::Deselect));
        }
    } else {
        statusBar()->showMessage(tr("当前页面没有可取消的选区"), 2000);
    }
}

void MainWindow::onImageInverse()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        if (auto *sel = img->selectionModel()) {
            img->undoStack()->push(new selection::SelectionCommand(sel, selection::SelectionCommand::Op::Invert));
        }
    } else {
        statusBar()->showMessage(tr("当前页面没有可反选的选区"), 2000);
    }
}
void MainWindow::onFind()    { statusBar()->showMessage(tr("查找 (未实现)"), 3000); }
void MainWindow::onReplace() { statusBar()->showMessage(tr("替换 (未实现)"), 3000); }
void MainWindow::onZoomIn()  { statusBar()->showMessage(tr("放大"), 2000); }
void MainWindow::onZoomOut() { statusBar()->showMessage(tr("缩小"), 2000); }
void MainWindow::onResetLayout() { statusBar()->showMessage(tr("重置布局"), 2000); }

// P0-6.5 (2026-09-14): 图像变换 6 槽 — 调当前 ImageWindow 接口
//   P0-6.6 ImageWindow::onImageTransform 完整实装, 槽留 forward + statusBar 提示
void MainWindow::onFreeTransform()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->onFreeTransform();
    } else {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
    }
}

void MainWindow::onImageFlipH()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->onImageFlipH();
    } else {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
    }
}

void MainWindow::onImageFlipV()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->onImageFlipV();
    } else {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
    }
}

void MainWindow::onImageRotate90CW()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->onImageRotate90CW();
    } else {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
    }
}

void MainWindow::onImageRotate90CCW()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->onImageRotate90CCW();
    } else {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
    }
}

void MainWindow::onImageRotate180()
{
    if (auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()))) {
        img->onImageRotate180();
    } else {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
    }
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(this);
    dlg.exec();
}

// =============================================================
// 阶段 0 第 10 步: 主题画廊 (UI 骨架)
//   - 只弹 dialog, 选 accent/模式不真改 ThemeManager
//   - 阶段 5+: ThemeGalleryDialog::onApplyClicked 内部调
//              ThemeManager::setAccentColor / setMode / persistToSettings
//   - 这里也用作"从设置弹主题画廊"的入口, 跟工具菜单触发器共用
// =============================================================
void MainWindow::onShowThemeGallery()
{
    ThemeGalleryDialog dlg(this);
    dlg.exec();
}

// =============================================================
// 阶段 0 第 10 步: 登录对话框 (UI 骨架)
//   - dialog 选 provider 后 emit providerChosen, 这里接住打 statusBar 提示
//   - 阶段 5+: LoginDialog 内部调 AuthClient::loginXxxAsync(...)
//              AuthClient::loginFinished -> UserManager::setState(Authenticated, user)
//              这里只需 listen AuthClient 信号, 不再处理 providerChosen
// =============================================================
void MainWindow::onShowLogin()
{
    LoginDialog dlg(this);
    connect(&dlg, &LoginDialog::providerChosen, this,
            [this](LoginDialog::Provider p) {
        const QString name = (p == LoginDialog::ProviderGitHub) ? "GitHub"
                          : (p == LoginDialog::ProviderQQ)     ? "QQ"
                          : (p == LoginDialog::ProviderWeChat) ? "微信"
                          : (p == LoginDialog::ProviderPhone)  ? "手机号"
                          : (p == LoginDialog::ProviderGuest)  ? "游客"
                          : "本地";
        statusBar()->showMessage(tr("已选择登录方式: %1 (阶段 0 仅占位)").arg(name), 4000);
        // 阶段 5+ 这里调 UserManager::instance().setUser(provider, "stub_user_id", name);
    });
    dlg.exec();
}

// (3D 模块已移除, 2026-09-02: 模式切换 onModeModeling / onModeMultimedia / confirmSwitchMode / setAppMode / modeDisplayName / applyModeToActions 整段删除)
//   后续如果需要多模态切分, 在这里加回

QString MainWindow::defaultFileDialogFilter() const
{
    // 决策 5 (2026-09-02): 用 FileExtensionRegistry 动态生成
    //   跟 MediaDispatcher 路由表保持一致; 后续增/删扩展名只改 FileExtensionRegistry
    auto glob = [](const QStringList &exts) {
        QStringList pat;
        pat.reserve(exts.size());
        for (const QString &e : exts) {
            pat.append(QStringLiteral("*%1").arg(e));
        }
        return pat.join(QLatin1Char(' '));
    };

    const QString imgF = glob(FileExtensionRegistry::extensionsFor(
        QString::fromLatin1(FileExtensionRegistry::ModuleId::ImageWorker)));
    const QString audF = glob(FileExtensionRegistry::extensionsFor(
        QString::fromLatin1(FileExtensionRegistry::ModuleId::AudioWorker)));
    const QString vidF = glob(FileExtensionRegistry::extensionsFor(
        QString::fromLatin1(FileExtensionRegistry::ModuleId::VideoWorker)));

    return tr("图像 (%1);; "
              "音频 (%2);; "
              "视频 (%3);; "
              "文本 (*.txt *.md);; 全部文件 (*)")
        .arg(imgF, audF, vidF);
}

QString MainWindow::defaultFileExtension() const
{
    return QStringLiteral(".txt");
}

void MainWindow::updateWindowTitle()
{
    // 3D 模块移除后, 不再有 [模式] 字段, 只显示版本号
    QString base = QStringLiteral("Vistella %1")
        .arg(QString::fromLatin1(kAppVersion));
    int idx = ui->tabWidget->currentIndex();
    QWidget *w = widgetAt(idx);
    QString suffix;
    if (w) {
        if (auto *d = qobject_cast<DocWindow *>(w)) {
            const QString p = d->filePath();
            suffix = p.isEmpty() ? tr("未命名") : p;
            if (d->isDirty()) suffix.append(QStringLiteral(" *"));
        } else if (auto *img = qobject_cast<ImageWindow *>(w)) {
            const QString p = img->filePath();
            suffix = p.isEmpty() ? tr("未命名图像") : p;
            if (img->isDirty()) suffix.append(QStringLiteral(" *"));
        }
    }
    if (!suffix.isEmpty()) setWindowTitle(QStringLiteral("%1 - %2").arg(base, suffix));
    else                    setWindowTitle(base);
}

// PS/WPS/VS 风格 (2026-09-10): HomePage signal handlers
//   转发到 onNewFile / onOpenFile, 调完切到工作空间 (page 1)
void MainWindow::onHomeNewRequested()    { onNewFile(); switchToWorkspace(); }
void MainWindow::onHomeOpenRequested()   { onOpenFile(); switchToWorkspace(); }
void MainWindow::onHomeModuleRequested(int moduleKind)
{
    // 阶段 1+ 接 MediaDispatcher, 当前 P0 阶段只 log
    LOG_INFO("[Home] module requested: kind={}", moduleKind);
}

// ----------------- HomePage -----------------
// F-G.4 (2026-09-09): onHomeNewRequested / onHomeOpenRequested 删 (HomePage dialog 直接 connect 到 onNewFile / onOpenFile)

// P0-5 (2026-09-10): 滤镜菜单 4 action 弹 FilterDialog
//   Apply = preview, OK = push FilterCommand, Cancel = close
//   P0 简化: 不可调参数, 用 strategy 默认值
void MainWindow::onFilterMenuTriggered(filter::FilterKind kind)
{
    auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()));
    if (!img) {
        statusBar()->showMessage(tr("当前页面不可应用滤镜 (需要图像窗口)"), 2000);
        return;
    }
    auto *dlg = new filter::FilterDialog(img, kind, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &filter::FilterDialog::applyRequested, this, [this, dlg, img, kind](){
        // P0 简化: Apply 不真做 preview, 只显示状态
        statusBar()->showMessage(
            tr("Apply %1 (P0 简化: 用默认值预览)").arg(QString::fromUtf8(filter::filterName(kind))),
            2000);
        (void)img;
        (void)dlg;
    });
    connect(dlg, &filter::FilterDialog::okRequested, this, [this, img, kind](){
        // OK: 调 ImageWindow::applyFilter push FilterCommand
        img->applyFilter(kind);
        statusBar()->showMessage(
            tr("已应用 %1 (入撤销栈)").arg(QString::fromUtf8(filter::filterName(kind))),
            2000);
    });
    dlg->show();
}

// P1.2.5+6+8 (2026-09-16): 启动 Liquify 交互式液化对话框
//   - 拿当前图像 -> 开 LiquifyDialog (modal)
//   - OK: 拿 resultImage, push LiquifyCommand (入撤销栈)
void MainWindow::onLiquifyTriggered()
{
    auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()));
    if (!img) {
        statusBar()->showMessage(tr("当前页面不可应用 Liquify (需要图像窗口)"), 2000);
        return;
    }

    // 取当前图像 (QImage from m_current cv::Mat)
    const QImage source = img->currentImageAsQImage();
    if (source.isNull()) {
        statusBar()->showMessage(tr("Liquify: 当前图像为空"), 2000);
        return;
    }

    auto *dlg = new filters::liquify::LiquifyDialog(source, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg, img, source]() {
        const QImage result = dlg->resultImage();
        if (result.isNull()) return;

        // before = current cv::Mat (clone), after = QImage -> cv::Mat
        const cv::Mat before = img->currentImage().clone();
        cv::Mat after;
        if (result.format() == QImage::Format_ARGB32 || result.format() == QImage::Format_RGB32) {
            after = cv::Mat(result.height(), result.width(), CV_8UC4,
                             (void*)result.bits(), result.bytesPerLine()).clone();
        } else if (result.format() == QImage::Format_RGB888) {
            after = cv::Mat(result.height(), result.width(), CV_8UC3,
                             (void*)result.bits(), result.bytesPerLine()).clone();
        } else {
            const QImage converted = result.convertToFormat(QImage::Format_ARGB32);
            after = cv::Mat(converted.height(), converted.width(), CV_8UC4,
                             (void*)converted.bits(), converted.bytesPerLine()).clone();
        }

        if (after.empty()) return;

        auto *cmd = new filters::liquify::LiquifyCommand(img, before, after);
        img->undoStack()->push(cmd);
        statusBar()->showMessage(tr("已应用 Liquify (入撤销栈)"), 2000);
    });
    dlg->show();
}

// =====================================================================
//  P1.3.4/5/6/7 (2026-09-17): 图层蒙版入口实现
// =====================================================================

// P1.3.4: 切换到 MaskBrushTool (像素蒙版画笔).
//   Mediator 通知 ImageWindow 的 ToolContext,后者通过 LeftToolBar 的
//   switch 工厂创建 MaskBrushTool. ImageOptionBar 会自动显示 MaskOptionsPanel.
void MainWindow::onSwitchToMaskBrush()
{
    auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()));
    if (!img) {
        statusBar()->showMessage(tr("需要图像窗口才能编辑蒙版"), 2000);
        return;
    }
    if (auto *med = img->toolMediator()) {
        med->switchTool(mediators::ToolId::MaskBrush);
        statusBar()->showMessage(tr("已切换到 Mask Brush 工具 (画笔编辑当前层像素蒙版)"), 3000);
    } else {
        statusBar()->showMessage(tr("ToolMediator 未初始化"), 2000);
    }
}

// P1.3.5: 从选区添加像素蒙版 (全白=全显示, 后续可以用 MaskBrush 涂黑遮罩).
//   当前没有复杂选区轮廓转换: 用整个图层尺寸 255 初始化 mask, 用户再编辑.
void MainWindow::onAddPixelMaskFromSelection()
{
    auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()));
    if (!img) {
        statusBar()->showMessage(tr("需要图像窗口"), 2000);
        return;
    }
    auto *stack = img->layerStack();
    if (!stack) {
        statusBar()->showMessage(tr("LayerStack 未初始化"), 2000);
        return;
    }
    const int idx = stack->selection();
    if (idx < 0) {
        statusBar()->showMessage(tr("请先选中图层"), 2000);
        return;
    }
    auto l = stack->at(idx);
    if (!l || l->image.empty()) {
        statusBar()->showMessage(tr("图层无图像数据"), 2000);
        return;
    }
    // 初始化全白 255 像素蒙版
    cv::Mat whiteMask(l->image.rows, l->image.cols, CV_8UC1, cv::Scalar(255));
    // 备份 before + 推 undo command
    const auto beforeMask = l->mask;  // 拷贝当前 mask struct (含 pixel)
    const auto beforeMaskPixel = (l->mask.kind == layers::LayerMask::Pixel)
                                    ? l->mask.pixel.clone() : cv::Mat();
    stack->addPixelMask(idx, whiteMask);
    const auto afterMask = stack->maskAt(idx) ? *stack->maskAt(idx) : layers::LayerMask();

    if (auto *undo = img->undoStack()) {
        auto *cmd = new layers::LayerMaskCommand(stack, idx,
                                                  beforeMask, afterMask,
                                                  QStringLiteral("Add Pixel Mask"));
        undo->push(cmd);
    }
    statusBar()->showMessage(tr("已添加像素蒙版 (全白, 现在用 Mask Brush 涂黑遮罩)"), 3000);
}

// P1.3.6: Refine Edge 对话框 - 4 sliders (smooth/feather/contrast/shift)
//   应用到当前图层的像素蒙版 (没有则提示).
void MainWindow::onRefineMaskEdge()
{
    auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()));
    if (!img) {
        statusBar()->showMessage(tr("需要图像窗口"), 2000);
        return;
    }
    auto *stack = img->layerStack();
    if (!stack) {
        statusBar()->showMessage(tr("LayerStack 未初始化"), 2000);
        return;
    }
    const int idx = stack->selection();
    if (idx < 0) {
        statusBar()->showMessage(tr("请先选中图层"), 2000);
        return;
    }
    auto l = stack->at(idx);
    if (!l || l->mask.kind != layers::LayerMask::Pixel || l->mask.pixel.empty()) {
        statusBar()->showMessage(tr("当前图层没有像素蒙版 (先添加像素蒙版)"), 3000);
        return;
    }

    // 内嵌对话框: 用 QInputDialog 简化版 (4 sliders 用 4 个 getInt).
    bool ok = false;
    const int smooth = QInputDialog::getInt(this, tr("Refine Edge"),
        tr("Smooth (0-100):"), 0, 0, 100, 1, &ok);
    if (!ok) return;
    const int feather = QInputDialog::getInt(this, tr("Refine Edge"),
        tr("Feather (0-100):"), 0, 0, 100, 1, &ok);
    if (!ok) return;
    const int contrast = QInputDialog::getInt(this, tr("Refine Edge"),
        tr("Contrast (-100..+100):"), 0, -100, 100, 1, &ok);
    if (!ok) return;
    const int shift = QInputDialog::getInt(this, tr("Refine Edge"),
        tr("Shift (-100..+100, 收缩/扩展):"), 0, -100, 100, 1, &ok);
    if (!ok) return;

    masks::RefineEdgeParams params2;
    params2.smooth = smooth;
    params2.feather = feather;
    params2.contrast = contrast;
    params2.shift = shift;
    const cv::Mat refined = masks::RefineEdge::apply(l->mask.pixel, params2);

    // 备份 + 推 undo
    const auto beforeMask = *stack->maskAt(idx);
    stack->addPixelMask(idx, refined);  // 用新的 mask 替换
    const auto afterMask = *stack->maskAt(idx);

    if (auto *undo = img->undoStack()) {
        auto *cmd = new layers::LayerMaskCommand(stack, idx,
                                                  beforeMask, afterMask,
                                                  QStringLiteral("Refine Edge"));
        undo->push(cmd);
    }
    statusBar()->showMessage(
        tr("已应用 Refine Edge (s=%1 f=%2 c=%3 s=%5)").arg(smooth).arg(feather)
                                                        .arg(contrast).arg(shift),
        3000);
}

// P1.3.7: Color Range 对话框 - 取当前鼠标位置颜色 + Fuzziness slider
//   (简化版: 用一个 QInputDialog 链取样 + fuzziness, 不画完整预览).
void MainWindow::onColorRangeMask()
{
    auto *img = qobject_cast<ImageWindow *>(widgetAt(ui->tabWidget->currentIndex()));
    if (!img) {
        statusBar()->showMessage(tr("需要图像窗口"), 2000);
        return;
    }
    auto *stack = img->layerStack();
    if (!stack) {
        statusBar()->showMessage(tr("LayerStack 未初始化"), 2000);
        return;
    }
    const int idx = stack->selection();
    if (idx < 0) {
        statusBar()->showMessage(tr("请先选中图层"), 2000);
        return;
    }
    auto l = stack->at(idx);
    if (!l || l->image.empty()) {
        statusBar()->showMessage(tr("图层无图像数据"), 2000);
        return;
    }

    // 简化: 用图层中心 3 个固定采样点 + fuzziness slider.
    //   PS 同款会画预览 + 让用户吸管点选, 但完整 dialog 需要预览画布;
    //   这里给可用的 v1 入口.
    cv::Mat bgr = l->image.clone();
    if (bgr.channels() == 4) cv::cvtColor(bgr, bgr, cv::COLOR_BGRA2BGR);
    QVector<QPoint> samples;
    const int w = bgr.cols, h = bgr.rows;
    samples.append(QPoint(w / 4,     h / 4));
    samples.append(QPoint(w / 2,     h / 2));
    samples.append(QPoint(3 * w / 4, 3 * h / 4));

    bool ok = false;
    const int fuzziness = QInputDialog::getInt(this, tr("Color Range"),
        tr("Fuzziness (0-255):"), 30, 0, 255, 1, &ok);
    if (!ok) return;
    const int invert = QInputDialog::getInt(this, tr("Color Range"),
        tr("Invert (0/1):"), 0, 0, 1, 1, &ok);
    if (!ok) return;

    masks::ColorRangeParams params;
    params.samplePoints = samples;
    params.fuzziness = fuzziness;
    params.invert = (invert != 0);

    const QImage layerImg = img->layerStackAsQImage(idx);
    cv::Mat newMask = masks::ColorRange::computeMask(layerImg, params);
    if (newMask.empty()) return;

    const auto beforeMask = *stack->maskAt(idx);
    stack->addPixelMask(idx, newMask);
    const auto afterMask = *stack->maskAt(idx);
    if (auto *undo = img->undoStack()) {
        auto *cmd = new layers::LayerMaskCommand(stack, idx,
                                                  beforeMask, afterMask,
                                                  QStringLiteral("Color Range"));
        undo->push(cmd);
    }
    statusBar()->showMessage(
        tr("已应用 Color Range (fuzziness=%1, invert=%2)").arg(fuzziness).arg(invert),
        3000);
}

void MainWindow::onHomeOpenFolderRequested()
{
    // 阶段 0 第 7 步: 打开文件夹不再一次性扫描 + 全部打开
    //   改为: 弹目录选择 → 启动 InfoTreeDock → 加载整个目录的文件树
    //         用户在信息树里双击/回车 → MediaDispatcher 路由到对应模块
    const QString startDir = RecentManager::instance().entries().value(0).filePath.isEmpty()
        ? QDir::homePath()
        : QFileInfo(RecentManager::instance().entries().value(0).filePath).absolutePath();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开文件夹"), startDir);
    if (dir.isEmpty()) return;

    // 启动 InfoTreeDock (默认隐藏, 这里打开) + 加载目录
    if (m_infoDock) {
        m_infoDock->setRootPath(dir);
        m_infoDock->show();
    }
    statusBar()->showMessage(tr("已加载目录: %1 (在信息树里双击/回车打开文件)").arg(dir), 5000);
}

void MainWindow::onHomeOpenPathRequested(const QString &path)
{
    LOG_INFO("[Home] onHomeOpenPathRequested path='{}'",
             path.toLocal8Bit().constData());
    if (path.isEmpty()) {
        LOG_WARN("[Home] empty path, abort");
        return;
    }
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget *w = ui->tabWidget->widget(i);
        QString p;
        if (auto *d = qobject_cast<DocWindow *>(w))   p = d->filePath();
        else if (auto *img = qobject_cast<ImageWindow *>(w)) p = img->filePath();
        // (3D 模块已移除, 2026-09-02: ModelWindow 检查删除)
        if (!p.isEmpty() && p == path) {
            ui->tabWidget->setCurrentIndex(i);
            // Stage G v2 (2026-09-15): 文件已开时也要切到工作空间
            //   之前只 setCurrentIndex, 没切 mainStack page, 用户视觉上"没反应"
            //   现在跟新打开文件保持一致: 切到 workspace page 1 显示图片
            switchToWorkspace();
            LOG_INFO("[Home] file already open, switched to tab {}", i);
            return;
        }
    }
    QString err;
    // 3D 模块移除后, 不再分流: 全部按扩展名识别 (图片/文本/...)
    //   简单做法: 优先尝试 ImageWindow.loadFile, 失败回退 DocWindow
    auto *img = new ImageWindow;
    // F-G.3 (2026-09-09): 注入 WorkspaceMediator
    img->attachWorkspaceMed(m_workspaceMed.get());
    if (img->loadFile(path, &err)) {
        int idx = addImageTab(img);
        if (idx >= 0) {
            ui->tabWidget->setCurrentIndex(idx);
            // PS/WPS/VS 风格 (2026-09-10): 打开文件后切到工作空间 (page 1)
            switchToWorkspace();
        }
        RecentManager::instance().touchOpen(path, RecentModeMultimedia);
        LOG_INFO("[Home] opened '{}' in tab {}", path.toLocal8Bit().constData(), idx);
        return;
    }
    LOG_WARN("[Home] loadFile failed for '{}': {}", path.toLocal8Bit().constData(), err.toLocal8Bit().constData());
    delete img;
    auto *doc = new DocWindow;
    if (!doc->loadFile(path, &err)) {
        QMessageBox::warning(this, tr("打开失败"), err);
        delete doc;
        return;
    }
    int idx = addDocTab(doc);
    if (idx >= 0) {
        ui->tabWidget->setCurrentIndex(idx);
        // PS/WPS/VS 风格 (2026-09-10): 打开文件后切到工作空间 (page 1)
        switchToWorkspace();
    }
    RecentManager::instance().touchOpen(path, RecentModeMultimedia);
}

// ----------------- Session -----------------

void MainWindow::saveSession()
{
    QStringList files;
    int activeIdx = ui->tabWidget->currentIndex();
    QString activeFile;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget *w = ui->tabWidget->widget(i);
        QString p;
        if (auto *d = qobject_cast<DocWindow *>(w))   p = d->filePath();
        else if (auto *img = qobject_cast<ImageWindow *>(w)) p = img->filePath();
        if (!p.isEmpty()) {
            files << p;
            if (i == activeIdx) activeFile = p;
        }
    }
    SessionManager::instance().saveOpenFiles(files);
    SessionManager::instance().saveActiveFile(activeFile);
}

void MainWindow::restoreSession()
{
    // 内部接口: 保留向后兼容, 实际由 restoreSessionFiles 走
    restoreSessionFiles(
        SessionManager::instance().restoreOpenFiles(),
        SessionManager::instance().restoreActiveFile());
}

void MainWindow::restoreSessionFiles(const QStringList &files, const QString &activeFile)
{
    if (files.isEmpty()) return;
    // 启动入口恢复 (2026-09-10): 恢复文件后切到第一个 image/doc tab, 隐藏 HomePage
    int targetIdx = -1;
    for (const QString &p : files) {
        if (!QFileInfo::exists(p)) continue;
        QString err;
        // (3D 模块已移除, 2026-09-02: 模式分支删除, 全部按扩展名识别)
        //   优先尝试 ImageWindow, 失败回退 DocWindow
        ImageWindow *img = new ImageWindow;
        // F-G.3 (2026-09-09): 注入 WorkspaceMediator
        img->attachWorkspaceMed(m_workspaceMed.get());
        if (img->loadFile(p, &err)) {
            int idx = addImageTab(img);
            if (idx >= 0) {
                installTabPinButton(idx);
                if (p == activeFile) targetIdx = idx;
            }
            continue;
        }
        delete img;
        DocWindow *doc = new DocWindow;
        if (!doc->loadFile(p, &err)) { delete doc; continue; }
        int idx = addDocTab(doc);
        if (idx >= 0) {
            installTabPinButton(idx);
            if (p == activeFile) targetIdx = idx;
        }
    }
    if (targetIdx >= 0) {
        ui->tabWidget->setCurrentIndex(targetIdx);
    } else {
        // restore 后没找到 activeFile, 切到第一个 tab
        for (int i = 0; i < ui->tabWidget->count(); ++i) {
            ui->tabWidget->setCurrentIndex(i);
            break;
        }
    }
    // PS/WPS/VS 风格 (2026-09-10): restore session 完 -> 切到工作空间 (page 1)
    if (ui->tabWidget->count() > 0) {
        switchToWorkspace();
    }
}

// ----------------- 鼠标 (留作扩展) -----------------

// =============================================================
// 标题栏拖动 (无边框窗口必须自己实现, 原生标题栏帮我们做的事)
//   mousePressEvent  → 检测鼠标在 titleLeft/Mid 区域, 记下 offset, 启动拖动
//   mouseMoveEvent    → 按 offset 移动窗口
//   mouseReleaseEvent → 结束拖动
//   mouseDoubleClick  → 最大化/还原
// 关键: 不要拦截在 titleRight 区域 (那里有按钮, 应该走 button 自己的事件)
// =============================================================
void MainWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && titleBarDragRect().contains(e->pos())) {
        // 决策 5 后续修复 (2026-09-02): 最大化状态时不要立即 showNormal
        //   单击不拖 = 啥也不做 (跟系统行为一致, 双击才切最大化/还原)
        //   拖动距离 > 阈值才 showNormal + 跟随鼠标 (跟系统行为一致)
        m_titleDragging = true;
        m_titleRestoredOnDrag = false;
        m_titleDragStartGlobal = e->globalPosition().toPoint() - frameGeometry().topLeft();
        m_titleDragStartLocal  = e->pos();
        m_titlePressGlobal     = e->globalPosition().toPoint();
        e->accept();
        return;
    }
    QMainWindow::mousePressEvent(e);
}

void MainWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (m_titleDragging && (e->buttons() & Qt::LeftButton)) {
        // 第一次超过阈值 + 当前是最大化 → 还原成半屏, 鼠标位置保持原比例
        //   阈值 4 px 跟 Qt QStyleHints::startDragDistance 默认 + Windows 主流一致
        if (!m_titleRestoredOnDrag && m_isMaximized) {
            const QPoint delta = e->globalPosition().toPoint() - m_titlePressGlobal;
            if (delta.manhattanLength() > 4) {
                const qreal ratioX = (m_titleDragStartLocal.x() - 0.0) / qreal(width());
                // 标题栏拖动还原 — showNormal 让窗口回到 saved geometry
                showNormal();
                const QPoint newPos = e->globalPosition().toPoint()
                                      - QPoint(int(width() * ratioX), m_titleDragStartLocal.y());
                move(newPos);
                m_titleRestoredOnDrag = true;
                // 重新计算 dragStartGlobal, 因为窗口大小/位置都变了
                m_titleDragStartGlobal = e->globalPosition().toPoint() - frameGeometry().topLeft();
                e->accept();
                return;
            }
        }
        // 普通拖动 (还原后 / 本来就没最大化)
        move(e->globalPosition().toPoint() - m_titleDragStartGlobal);
        e->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(e);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_titleDragging) {
        // 单击没拖 = 啥也不做 (修复前这里会把已 showNormal 留下来的状态泄漏)
        m_titleDragging = false;
        m_titleRestoredOnDrag = false;
        e->accept();
        return;
    }
    QMainWindow::mouseReleaseEvent(e);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && titleBarDragRect().contains(e->pos())) {
        onMaximizeRestore();
        e->accept();
        return;
    }
    QMainWindow::mouseDoubleClickEvent(e);
}

// ----------------- 重译 -----------------

void MainWindow::retranslateUiTexts()
{
    // (3D 模块已移除, 2026-09-02: mode 按钮文字更新删除)

    if (-1 >= 0)
        ui->tabWidget->setTabText(-1, tr("主页"));
    if (m_infoDock) m_infoDock->setWindowTitle(tr("信息树"));

    for (int i = 0; i < ui->tabWidget->count(); ++i) refreshTabTitle(i);
}
