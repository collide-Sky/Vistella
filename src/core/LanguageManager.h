#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QTranslator>

class QApplication;

// ============================================================
// LanguageManager — 全局唯一的翻译器管理
//   - 持有 1 个 QTranslator 实例
//   - 切换时: 卸载旧 → 加载新 → install 到 qApp
//   - 失败回退 zh_CN (源字符串)
//   - 启动时调用 installInitial() 把 QSettings 里的语言装上
// ============================================================
class LanguageManager
{
public:
    static LanguageManager &instance();

    struct LangInfo {
        QString code;       // zh_CN / en_US / ja_JP
        QString display;    // 简体中文 / English / 日本語
    };
    QList<LangInfo> availableLanguages() const;

    QString currentLanguage() const;   // 失败时为 zh_CN

    // 设置语言: 卸载旧 → 加载新 → 写 QSettings
    // 不会自动 retranslateUi, 调用方需自己决定怎么刷 UI
    void setLanguage(const QString &code);

    // 启动时调用: 读 QSettings 装上对应翻译
    // 必须在 QApplication 构造之后、MainWindow 构造之前调用
    void installInitial();

    void uninstall();

private:
    LanguageManager() = default;
    bool loadAndInstall(const QString &code);

    QTranslator *m_translator = nullptr;
    QString      m_currentCode;
};

#endif // LANGUAGEMANAGER_H
