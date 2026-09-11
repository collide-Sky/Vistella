#include "ThemeManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QApplication>

namespace {
constexpr const char *kThemeKey = "App/theme";
}

ThemeManager &ThemeManager::instance()
{
    static ThemeManager inst;
    return inst;
}

ThemeManager::ThemeManager(QObject *parent) : QObject(parent)
{
    load();
    rebuildPalette();
}

void ThemeManager::load()
{
    QSettings s;
    const int v = s.value(kThemeKey, int(Light)).toInt();
    m_theme = (v == int(Dark)) ? Dark : Light;
}

void ThemeManager::save()
{
    QSettings().setValue(kThemeKey, int(m_theme));
}

void ThemeManager::setTheme(Theme t)
{
    if (m_theme == t) return;
    m_theme = t;
    rebuildPalette();
    save();
    emit themeChanged(t);
}

void ThemeManager::toggleTheme()
{
    setTheme(m_theme == Light ? Dark : Light);
}

void ThemeManager::setStyleSheetPath(const QString &path)
{
    m_qssPath = path;
}

QString ThemeManager::resolveQss() const
{
    // 候选路径顺序: 调用方指定 > rcc 内嵌 > exe 同级 styles/
    QStringList candidates;
    if (!m_qssPath.isEmpty()) candidates << m_qssPath;
    candidates << QStringLiteral(":/styles/Vistella.qss");
    candidates << QCoreApplication::applicationDirPath()
                  + QStringLiteral("/styles/Vistella.qss");

    QString qss;
    bool ok = false;
    for (const auto &p : candidates) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qss = QString::fromUtf8(f.readAll());
            f.close();
            ok = true;
            break;
        }
    }
    if (!ok) {
        qWarning("[Theme] QSS not found in any of: %s",
                 qPrintable(candidates.join(", ")));
        return {};
    }

    // 占位符替换
    const Palette &p = m_palette;
    qss.replace(QStringLiteral("{{ACCENT}}"),       p.accent.name());
    qss.replace(QStringLiteral("{{ACCENT_HOVER}}"),  p.accentHover.name());
    qss.replace(QStringLiteral("{{ACCENT_TEXT}}"),   p.accentText.name());
    qss.replace(QStringLiteral("{{BG}}"),            p.windowBg.name());
    qss.replace(QStringLiteral("{{BASE}}"),          p.base.name());
    qss.replace(QStringLiteral("{{ALTERNATE}}"),     p.alternateBase.name());
    qss.replace(QStringLiteral("{{TEXT}}"),          p.text.name());
    qss.replace(QStringLiteral("{{TEXT_SUBTLE}}"),   p.textSubtle.name());
    qss.replace(QStringLiteral("{{BORDER}}"),        p.menuBorder.name());
    qss.replace(QStringLiteral("{{TAB_BG}}"),        p.tabBg.name());
    qss.replace(QStringLiteral("{{TAB_HOVER}}"),     p.tabBgHover.name());
    qss.replace(QStringLiteral("{{TAB_SELECTED}}"),  p.tabBgSelected.name());
    qss.replace(QStringLiteral("{{TAB_TEXT}}"),      p.tabText.name());
    qss.replace(QStringLiteral("{{TAB_TEXT_SEL}}"),  p.tabTextSelected.name());
    qss.replace(QStringLiteral("{{TAB_DIRTY}}"),     p.tabDirty.name());
    qss.replace(QStringLiteral("{{MENU_BG}}"),       p.menuBg.name());
    qss.replace(QStringLiteral("{{MENU_TEXT}}"),     p.menuText.name());
    qss.replace(QStringLiteral("{{MENU_HOVER}}"),    p.menuHover.name());
    qss.replace(QStringLiteral("{{DOCK_HEADER}}"),   p.dockHeader.name());
    qss.replace(QStringLiteral("{{DOCK_BG}}"),       p.dockBg.name());
    qss.replace(QStringLiteral("{{TOOLBAR_BG}}"),    p.toolbarBg.name());
    qss.replace(QStringLiteral("{{STATUS_BG}}"),     p.statusBarBg.name());
    qss.replace(QStringLiteral("{{PANEL_BG}}"),      p.panelBg.name());
    qss.replace(QStringLiteral("{{PANEL_HEADER}}"),  p.panelHeader.name());
    qss.replace(QStringLiteral("{{PANEL_BORDER}}"),  p.panelBorder.name());
    return qss;
}

void ThemeManager::rebuildPalette()
{
    if (m_theme == Light) {
        m_palette.windowBg         = QColor("#f5f5f5");
        m_palette.base             = QColor("#ffffff");
        m_palette.alternateBase    = QColor("#fafafa");
        m_palette.text             = QColor("#1f1f1f");
        m_palette.textSubtle       = QColor("#6a6a6a");

        m_palette.accent           = QColor("#4caf80");  // 浅绿
        m_palette.accentHover      = QColor("#3e9970");
        m_palette.accentText       = QColor("#ffffff");

        m_palette.tabBg            = QColor("#e8e8e8");
        m_palette.tabBgHover       = QColor("#dcdcdc");
        m_palette.tabBgSelected    = QColor("#ffffff");
        m_palette.tabText          = QColor("#3a3a3a");
        m_palette.tabTextSelected  = QColor("#0d0d0d");
        m_palette.tabBorderSelected = QColor("#4caf80");
        m_palette.tabDirty         = QColor("#e67e22");

        m_palette.panelBg          = QColor("#f0f0f0");
        m_palette.panelHeader      = QColor("#3a3a3a");
        m_palette.panelBorder      = QColor("#cccccc");

        m_palette.dockBg           = QColor("#fafafa");
        m_palette.dockHeader       = QColor("#eaeaea");

        m_palette.menuBg           = QColor("#ffffff");
        m_palette.menuText         = QColor("#1f1f1f");
        m_palette.menuHover        = QColor("#d6f0e3");
        m_palette.menuBorder       = QColor("#cccccc");

        m_palette.toolbarBg        = QColor("#f5f5f5");
        m_palette.statusBarBg      = QColor("#ececec");
    } else {
        // Dark
        m_palette.windowBg         = QColor("#1e1e1e");
        m_palette.base             = QColor("#252526");
        m_palette.alternateBase    = QColor("#2a2a2b");
        m_palette.text             = QColor("#e6e6e6");
        m_palette.textSubtle       = QColor("#9d9d9d");

        m_palette.accent           = QColor("#4caf80");
        m_palette.accentHover      = QColor("#5dc295");
        m_palette.accentText       = QColor("#0d0d0d");

        m_palette.tabBg            = QColor("#2d2d2d");
        m_palette.tabBgHover       = QColor("#3a3a3a");
        m_palette.tabBgSelected    = QColor("#1e1e1e");
        m_palette.tabText          = QColor("#c0c0c0");
        m_palette.tabTextSelected  = QColor("#ffffff");
        m_palette.tabBorderSelected = QColor("#4caf80");
        m_palette.tabDirty         = QColor("#f0a04b");

        m_palette.panelBg          = QColor("#252526");
        m_palette.panelHeader      = QColor("#dcdcdc");
        m_palette.panelBorder      = QColor("#3f3f3f");

        m_palette.dockBg           = QColor("#252526");
        m_palette.dockHeader       = QColor("#2d2d2d");

        m_palette.menuBg           = QColor("#2d2d2d");
        m_palette.menuText         = QColor("#e6e6e6");
        m_palette.menuHover        = QColor("#3a3a3a");
        m_palette.menuBorder       = QColor("#3f3f3f");

        m_palette.toolbarBg        = QColor("#2d2d2d");
        m_palette.statusBarBg      = QColor("#0e0e0e");
    }
}

QString ThemeManager::globalStyleSheet() const
{
    // 从 .qss 文件读静态规则 + 替换主题色占位符
    return resolveQss();
}
