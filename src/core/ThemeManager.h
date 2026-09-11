#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QString>
#include <QFont>

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    enum Theme {
        Light = 0,
        Dark  = 1,
    };
    Q_ENUM(Theme)

    struct Palette {
        // 窗口基底
        QColor windowBg;          // 主背景 (tab 区域、菜单、工具栏)
        QColor base;              // widget 默认底
        QColor alternateBase;     // 斑马纹行
        QColor text;
        QColor textSubtle;

        // 强调
        QColor accent;            // 主色 (浅绿)
        QColor accentHover;
        QColor accentText;        // 强调色上的文字

        // Tab
        QColor tabBg;
        QColor tabBgHover;
        QColor tabBgSelected;
        QColor tabText;
        QColor tabTextSelected;
        QColor tabBorderSelected; // 选中 tab 边框 (浅绿)
        QColor tabDirty;          // 修改标记色

        // 面板
        QColor panelBg;           // 左侧功能面板
        QColor panelHeader;       // group 标题
        QColor panelBorder;       // group 边框

        // Dock (信息面板)
        QColor dockBg;
        QColor dockHeader;

        // 菜单
        QColor menuBg;
        QColor menuText;
        QColor menuHover;
        QColor menuBorder;

        // 工具栏
        QColor toolbarBg;

        // 状态栏
        QColor statusBarBg;
    };

    static ThemeManager &instance();

    Theme currentTheme() const { return m_theme; }
    void setTheme(Theme t);
    void toggleTheme();   // 在 light/dark 之间切换

    const Palette &palette() const { return m_palette; }

    // 生成对应主题的全局 QSS 字符串 (从 .qss 文件 + palette 拼装)
    QString globalStyleSheet() const;

    // 设置静态 QSS 文件路径; 空 = 用内置默认值 (QRC 里的 styles/Vistella.qss)
    void setStyleSheetPath(const QString &path);
    QString styleSheetPath() const { return m_qssPath; }

    // 启动时调用, 装载用户上次的设置
    void load();
    // 用户切换主题时调用, 写回 QSettings
    void save();

signals:
    void themeChanged(Theme t);

private:
    explicit ThemeManager(QObject *parent = nullptr);
    ThemeManager(const ThemeManager &) = delete;
    ThemeManager &operator=(const ThemeManager &) = delete;

    void rebuildPalette();
    QString resolveQss() const;  // 读 .qss 文件 + 替换占位符

    Theme m_theme = Light;
    Palette m_palette;
    QString m_qssPath;  // 静态 QSS 路径, 空 = 默认 src/app/styles/Vistella.qss
};

#endif // THEMEMANAGER_H
