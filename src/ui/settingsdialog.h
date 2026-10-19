#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

private slots:
    void applyLanguage(int index);

    // 日志设置页
    void onLogApplyClicked();
    void onLogBrowseClicked();
    void onLogListClicked();
    void refreshLogStatus();

private:
    void setupLogPage();
    void setupGpuPage();   // P8.3.x

    Ui::SettingsDialog *ui;
    class SettingsGpuPanel* m_gpuPanel = nullptr;   // P8.3.x: owned via stack
};

#endif // SETTINGSDIALOG_H
