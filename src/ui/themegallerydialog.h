#ifndef THEMEGALLERYDIALOG_H
#define THEMEGALLERYDIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class ThemeGalleryDialog; }
QT_END_NAMESPACE

// =============================================================
// ThemeGalleryDialog — 主题画廊 (2026-09-02 阶段 0 第 10 步, 骨架)
//
// 阶段 0: 只搭 UI, 不真改 ThemeManager, 也不发 themeChanged 信号
//   - 颜色卡片点选后只更新预览
//   - "应用" 按钮: 阶段 0 只弹个 QMessageBox 提示 "阶段 5+ 真正接入 ThemeManager"
//   - "取消" 按钮: close
//
// 阶段 5+:
//   - 调 ThemeManager::setAccentColor(...) + emit themeChanged
//   - 重启当前所有窗口 (或让 ThemeManager 触发 app-level qApp->setStyleSheet)
//   - 持久化到 QSettings("App/theme/accent")
//
// 设计: 不直接 new widget, 全部走 .ui XML (跟项目其他 dialog 一致)
// =============================================================
class ThemeGalleryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ThemeGalleryDialog(QWidget *parent = nullptr);
    ~ThemeGalleryDialog() override;

private slots:
    // 色卡被点击 -> 更新预览
    void onAccentClicked();

    // 亮 / 暗 模式切换
    void onModeToggled();

    // 应用按钮: 阶段 0 占位, 弹个提示
    void onApplyClicked();

private:
    void setupAccentPalette();
    void updatePreview();

    Ui::ThemeGalleryDialog *ui;

    // 当前选中的 accent 颜色 (阶段 0 仅 UI 状态)
    QString m_currentAccent;
    // 0 = light, 1 = dark
    int     m_currentMode = 0;
};

#endif // THEMEGALLERYDIALOG_H
