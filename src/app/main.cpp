#include "mainwindow.h"

#include "../ai/AIManager.h"
#include "../core/LanguageManager.h"
#include "../core/MediaDispatcher.h"
#include "../core/SessionManager.h"
#include "../core/ThemeManager.h"
#include "../core/ThreadPool/engine_context.h"
#include "../media/imageworker/ImageWorker.h"
#include "../user/UserManager.h"

#include "Logger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPainterPath>
#include <QPolygonF>
#include <QQuickView>
#include <QRegion>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <exception>

#ifdef _WIN32
// Stage H v4 (2026-09-15): filter Qt6/DWrite first-chance C++ exceptions
//   Qt6 + DWrite 退出时主动抛 0xE06D7363 (C++ EH) 但被 Qt 内部 catch,
//   在调试器 (cdb.exe) 下被当 first-chance 异常断在 main() 返回处
//   注册 unhandled exception filter 让 0xE06D7363 跳过 (EXCEPTION_CONTINUE_EXECUTION)
#include <windows.h>
static LONG WINAPI DWriteExceptionFilter(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord
        && ep->ExceptionRecord->ExceptionCode == 0xE06D7363) {
        return EXCEPTION_CONTINUE_EXECUTION;  // Qt 内部 try/catch 会处理, 不让调试器断
    }
    return EXCEPTION_UNWIND;
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(DWriteExceptionFilter);
#endif
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QSG_RHI_BACKEND", "software");

    // 阶段 1 Step A Bug DEBUG (2026-09-04): 所有 qDebug 同时写 D:\vistella_qdebug.log
    //   用来诊断"slider init 触发但点击不触发"问题
    //   用户跑 EXE 后看这个 log 即可知道信号有没有到
    static QFile qDebugLog("D:/vistella_qdebug.log");
    qDebugLog.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        const char *prefix = "";
        switch (type) {
            case QtDebugMsg:    prefix = "[D]"; break;
            case QtInfoMsg:     prefix = "[I]"; break;
            case QtWarningMsg:  prefix = "[W]"; break;
            case QtCriticalMsg: prefix = "[C]"; break;
            case QtFatalMsg:    prefix = "[F]"; break;
        }
        QTextStream(&qDebugLog) << prefix << " " << msg << "\n";
        qDebugLog.flush();
    });

    int ret = 0;
    try {
    // (2) 高 DPI 缩放策略 (QApplication 构造**前**, Qt 6 强制要求)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // QApplication 构造
    QApplication a(argc, argv);
    QApplication::setApplicationName("Vistella");
    QApplication::setOrganizationName("Vistella");

    // 公共 ThreadPool v2 初始化 (2026-09-07 一次性到位):
    //   6 池 (Interactive/Background/IO/Gpu/AI/Offline) + 1 GPU 串行线程
    //   任何模块要开子线程都从 engine.xxx() 拿
    //   EngineContext 是普通对象, 析构时显式 shutdown() (可控, 不像单例析构顺序未定义)
    auto engine_cfg = vistella::tp::EngineContext::make_profile(
        vistella::tp::EngineContext::Profile::ImageEditor);
    vistella::tp::EngineContext engine(std::move(engine_cfg));
    {
        vistella::tp::EngineContext::ScopedCurrent scope(&engine);
        LOG_INFO("EngineContext initialized (profile=ImageEditor)");
    }

    // (3) Fusion style: 100% software 渲染, 不调 native DWrite/D2D/DWM
    a.setStyle(QStringLiteral("Fusion"));
    {
        const QStringList families = QFontDatabase::families();
        const QStringList preferred = {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("PingFang SC"),
            QStringLiteral("Source Han Sans CN"),
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("Arial"),
        };
        QString chosen;
        for (const QString &name : preferred) {
            if (families.contains(name, Qt::CaseInsensitive)) {
                chosen = name;
                break;
            }
        }
        if (chosen.isEmpty()) {
            // 全部 fallback 都没找到, 用 QFont 默认 sans-serif
            QFont f = QApplication::font();
            f.setPointSize(9);
            QApplication::setFont(f);
        } else {
            QApplication::setFont(QFont(chosen, 9));
        }
    }
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/logs");
    QDir().mkpath(logDir);  // 确保目录存在
    vistella::Logger::init("Vistella",
                           logDir.toStdString(),
                           vistella::LogLevel::Debug,
                           4 * 1024 * 1024);
    LOG_INFO("Vistella 启动, version=dev, logDir={}", logDir.toStdString());

    // ---- 加载翻译 (默认 zh_CN) ----
    LanguageManager::instance().installInitial();
    LOG_DEBUG("翻译加载完成, language={}",
              QLocale::system().name().toStdString());

    // ---- 应用全局 QSS (从 src/app/styles/Vistella.qss 读 + ThemeManager 替换主题色) ----
    qApp->setStyleSheet(ThemeManager::instance().globalStyleSheet());

    try {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                + QStringLiteral("/data");
        QDir().mkpath(dataDir);
        const QString dbPath = dataDir + QStringLiteral("/user.db");
        if (!UserManager::instance().init(dbPath)) {
            LOG_WARN("UserManager init 失败, db={}, 启动后用户管理功能不可用", dbPath.toStdString());
        } else {
            LOG_INFO("UserManager 初始化完成, db={}", dbPath.toStdString());
            // 阶段 0: 不自动登录, 阶段 5+ 加 tryAutoLogin
        }
    } catch (const std::exception &e) {
        LOG_EXCEPTION(vistella::LogLevel::Error, "UserManager init", e);
    } catch (...) {
        LOG_ERROR("UserManager init 未知异常");
    }

    {
        AIManager &ai = AIManager::instance();
        LOG_INFO("AIManager 初始化完成, backend={}", ai.backendName().toStdString());
    }

    {
        auto &disp = MediaDispatcher::instance();
        static ImageWorker s_imageWorker;
        disp.registerModule(&s_imageWorker);

        // 拼 module id list 给 log
        QStringList ids;
        for (IModule *m : disp.modules()) {
            ids << m->info().id;   // ModuleInfo::id 已是 QString
        }
        LOG_INFO("MediaDispatcher 初始化完成, moduleCount={}, modules=[{}]",
                 disp.moduleCount(),
                 ids.join(QStringLiteral(", ")).toStdString());
    }

    SessionManager::instance().setLastExitClean(false);

    // 1) 启动屏: QML 加载, 5 秒后自动关闭
    const QString qmlPath = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/qml/splash_screen.qml");

    QQuickView splash;
    QObject::connect(&splash, &QQuickView::statusChanged, &splash,
        [&splash](QQuickView::Status s) {
            if (s == QQuickView::Error) {
                for (const auto &err : splash.errors()) {
                    qWarning("[Splash] QML error: %s",
                             qPrintable(err.toString()));
                }
            }
        });
    splash.setSource(QUrl::fromLocalFile(qmlPath));

    if (splash.status() == QQuickView::Error) {
        MainWindow w;
        w.show();
        return a.exec();
    }

    // ---- 商业级外观: 圆角 + 半透明 + 透明背景 ----
    splash.setColor(QColor(0, 0, 0, 0));
    splash.setOpacity(0.96);
    {
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, 600, 400), 20, 20);
        QPolygonF polyF = path.toFillPolygon(QTransform());
        splash.setMask(QRegion(polyF.toPolygon()));
    }

    splash.setFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    splash.show();

    MainWindow w;
    w.hide();
    QTimer::singleShot(3000, &splash, [&splash, &w]() {
        try {
        splash.close();

        const bool clean = SessionManager::instance().isLastExitClean();
        if (clean) {
            // 正常退出: 不弹窗, 直接显示
            // PS/WPS/VS 风格 (2026-09-10): mainStack ctor 默认 page 0 (HomePage)
            //   无 session 看到 HomePage, 有 session 已经被 restore
            LOG_DEBUG("上次正常退出, 显示主窗口 (mainStack ctor 默认 page 0)");
            w.applyWindowMode();   // 走 setMode 唯一入口 (修复 cancel-recovery 路径)
            return;
        }

        LOG_WARN("上次程序异常退出, 进入恢复流程");

        // 异常退出: 读取上次打开的文件
        QStringList files = SessionManager::instance().restoreOpenFiles();
        // 过滤掉不存在的文件
        QStringList validFiles;
        for (const QString &p : files) {
            if (QFileInfo::exists(p)) validFiles << p;
        }

        LOG_INFO("待恢复文件数={}, 有效文件数={}", files.size(), validFiles.size());

        if (validFiles.isEmpty()) {
            // 没有可恢复的文件, 进 HomePage (PS/WPS/VS 风格 page 0)
            // mainStack ctor 默认 page 0, 不需要额外切
            w.applyWindowMode();   // 走 setMode 唯一入口
            return;
        }

        // 弹窗问是否恢复
        const auto ret = QMessageBox::question(
            nullptr,
            QObject::tr("检测到上次未正常退出"),
            QObject::tr("上次程序异常退出, 仍有 %1 个文件未关闭。\n"
                        "是否恢复这些文件?")
                .arg(validFiles.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);   // 默认 No, 防误操作

        if (ret == QMessageBox::Yes) {
            const QString active = SessionManager::instance().restoreActiveFile();
            LOG_INFO("用户选择恢复文件, active={}", active.toStdString());
            w.restoreSessionFiles(validFiles, active);
            // restoreSessionFiles 内部已经切到工作空间 (page 1)
        } else {
            // 选 No: 清空 session 记录 + 标记为干净, 下次正常启动直接进主页不再询问
            LOG_INFO("用户选择不恢复, 清空 session");
            SessionManager::instance().clear();
            SessionManager::instance().saveOpenFiles({});
            SessionManager::instance().saveActiveFile(QString());
            SessionManager::instance().setLastExitClean(true);
            // mainStack ctor 默认 page 0 (HomePage), 不需要额外切
        }
        // 不管恢复 / 不恢复, 最后都用唯一入口 applyWindowMode (= setMode(m_mode))
        //   修复 9/17 报告的 cancel-recovery → maximized → 不能 normal 路径
        w.applyWindowMode();
        } catch (const std::exception &e) {
            LOG_EXCEPTION(vistella::LogLevel::Error, "5s splash timer", e);
        } catch (...) {
            LOG_ERROR("5s splash timer 未知 C++ 异常 (Windows COM / 驱动 unload 等)");
        }
    });

    // ---- 进入事件循环 (主线程跑所有 UI/业务) ----
    try {
        ret = a.exec();
        LOG_INFO("主程序正常退出, ret={}", ret);
    } catch (const std::exception& e) {
        LOG_EXCEPTION(vistella::LogLevel::Critical, "a.exec()", e);
        ret = 1;
    } catch (...) {
        LOG_CRITICAL("a.exec() 抛出未知异常");
        ret = 1;
    }

    // 公共 ThreadPool 关闭 (2026-09-07): EngineContext RAII 自动 shutdown
    //   析构顺序: AI/GPU → 业务池 → 交互池 (见 EngineContext::~EngineContext)
    LOG_INFO("EngineContext shutting down...");

    // 2026-09-03: 关闭顶层 try, 主函数退出时统一处理
    } catch (const std::exception &e) {
        try { LOG_EXCEPTION(vistella::LogLevel::Critical, "main body", e); } catch (...) {}
        ret = 1;
    } catch (...) {
        try { LOG_CRITICAL("main body 抛出未知 C++ 异常 (DWrite / COM / 驱动 unload 等)"); } catch (...) {}
        ret = 1;
    }

    vistella::Logger::shutdown();
    return ret;
}
