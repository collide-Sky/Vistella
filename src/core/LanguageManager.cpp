#include "LanguageManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

// 单例
LanguageManager &LanguageManager::instance()
{
    static LanguageManager inst;
    return inst;
}

QList<LanguageManager::LangInfo> LanguageManager::availableLanguages() const
{
    return {
        { QStringLiteral("zh_CN"), QStringLiteral("简体中文") },
        { QStringLiteral("en_US"), QStringLiteral("English") },
        { QStringLiteral("ja_JP"), QStringLiteral("日本語") },
    };
}

QString LanguageManager::currentLanguage() const
{
    return m_currentCode.isEmpty() ? QStringLiteral("zh_CN") : m_currentCode;
}

bool LanguageManager::loadAndInstall(const QString &code)
{
    // 1) 卸旧
    if (m_translator) {
        QApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    // zh_CN 不需要翻译文件 (源字符串本身就是中文)
    if (code == QLatin1String("zh_CN")) {
        m_currentCode = code;
        return true;
    }

    // 2) 找翻译文件
    m_translator = new QTranslator;
    const QString base = QStringLiteral("vistella_%1").arg(code);
    // 优先 exe 同级 translatefile/ 目录 (开发期方便覆盖)
    const QString ext = QCoreApplication::applicationDirPath()
                      + QStringLiteral("/translatefile/") + base + QStringLiteral(".qm");
    bool loaded = m_translator->load(ext);
    if (!loaded) {
        // rcc 内嵌资源
        loaded = m_translator->load(QStringLiteral(":/translatefile/%1.qm").arg(base));
    }
    if (!loaded) {
        qWarning("[lang] failed to load %s, fallback to source strings",
                 qPrintable(base));
        delete m_translator;
        m_translator = nullptr;
        m_currentCode = QStringLiteral("zh_CN");   // 失败回退
        return false;
    }
    QApplication::installTranslator(m_translator);
    m_currentCode = code;
    return true;
}

void LanguageManager::setLanguage(const QString &code)
{
    // 写设置 (不管加载成不成功, 都记下用户的选择, 下次启动再试)
    QSettings s;
    s.setValue(QStringLiteral("App/language"), code);

    if (loadAndInstall(code)) {
        qInfo("[lang] language set: %s", qPrintable(code));
    }
}

void LanguageManager::installInitial()
{
    QSettings s;
    const QString requested = s.value(QStringLiteral("App/language"),
                                      QStringLiteral("zh_CN")).toString();
    loadAndInstall(requested);
    qInfo("[lang] initial language: %s", qPrintable(m_currentCode));
}

void LanguageManager::uninstall()
{
    if (m_translator) {
        QApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
}
