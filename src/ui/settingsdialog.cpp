#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "../core/LanguageManager.h"
#include "recentmanager.h"
#include "Logger.h"

#include <QFileDialog>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("设置"));
    // 不调 resize — .ui 里 geometry 写多大就多大, 避免覆盖设计尺寸

    // 通用页: 最近文件上限
    ui->recentCount->setRange(0, 100);
    ui->recentCount->setValue(RecentManager::instance().maxUnpinned());
    connect(ui->recentCount, qOverload<int>(&QSpinBox::valueChanged), this,
            [](int v) { RecentManager::instance().setMaxUnpinned(v); });

    // 语言页: 从 LanguageManager 动态填充 (单一数据源)
    const auto langs = LanguageManager::instance().availableLanguages();
    for (const auto &info : langs) {
        ui->langCombo->addItem(info.display, info.code);
    }
    const QString cur = LanguageManager::instance().currentLanguage();
    for (int i = 0; i < ui->langCombo->count(); ++i) {
        if (ui->langCombo->itemData(i).toString() == cur) {
            ui->langCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(ui->langCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::applyLanguage);

    // 日志页: 初始化当前值 + 绑定信号
    setupLogPage();
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::applyLanguage(int index)
{
    QComboBox *cb = qobject_cast<QComboBox *>(sender());
    if (!cb) return;
    const QString lang = cb->itemData(index).toString();
    if (lang.isEmpty()) return;

    // 切翻译: 会立刻让后续 tr() 拿到新字符串; 现有 widget 文字不会自动变
    LanguageManager::instance().setLanguage(lang);

    // 提示: 当前已切换, 但已显示的 widget 文字需重启或重新打开对话框才完全生效
    QMessageBox::information(
        this,
        tr("语言已切换"),
        tr("界面语言已切换为 %1。\n部分已显示的文本需要重新打开窗口或重启程序后才会更新。")
            .arg(cb->currentText()));
}

// =============================================================================
//  日志设置页
// =============================================================================
void SettingsDialog::setupLogPage()
{
    // 用 QSignalBlocker 防止 setText/setCurrentIndex 触发不必要信号
    QSignalBlocker blockModule(ui->logModuleName);
    QSignalBlocker blockDir(ui->logDir);
    QSignalBlocker blockLevel(ui->logLevelCombo);
    QSignalBlocker blockSize(ui->logSizeMB);

    // 填当前值
    ui->logModuleName->setText(QString::fromStdString(vistella::Logger::currentModuleName()));
    ui->logDir->setText(QString::fromStdString(vistella::Logger::currentLogDir()));
    const int mb = static_cast<int>(vistella::Logger::currentMaxFileSize() / (1024 * 1024));
    ui->logSizeMB->setValue(mb > 0 ? mb : 4);
    const QString curLevel = QString::fromStdString(vistella::Logger::currentLevelName());
    int idx = 0;
    for (int i = 0; i < ui->logLevelCombo->count(); ++i) {
        // itemText 形如 "Debug (调试)"; itemData 是 std::string level name
        const QString data = QString::fromStdString(
            ui->logLevelCombo->itemData(i).toString().toStdString());
        // 没存 userData, 我们用 lower-case 文本前缀匹配
        const QString lower = ui->logLevelCombo->itemText(i).split(' ').first().toLower();
        if (lower == curLevel.toLower()) { idx = i; break; }
    }
    ui->logLevelCombo->setCurrentIndex(idx);

    // 绑定信号
    connect(ui->logApplyBtn, &QPushButton::clicked,
            this, &SettingsDialog::onLogApplyClicked);
    connect(ui->logBrowseBtn, &QPushButton::clicked,
            this, &SettingsDialog::onLogBrowseClicked);
    connect(ui->logListBtn, &QPushButton::clicked,
            this, &SettingsDialog::onLogListClicked);

    refreshLogStatus();
}

void SettingsDialog::refreshLogStatus()
{
    const QString status = tr("当前状态: 模块=%1, 级别=%2, 单文件=%3 MB, 累计写入=%4 条")
        .arg(QString::fromStdString(vistella::Logger::currentModuleName()))
        .arg(QString::fromStdString(vistella::Logger::currentLevelName()))
        .arg(vistella::Logger::currentMaxFileSize() / (1024 * 1024))
        .arg(vistella::Logger::sinkCallCount());
    ui->logStatusLabel->setText(status);
}

void SettingsDialog::onLogBrowseClicked()
{
    const QString start = ui->logDir->text().isEmpty()
        ? QCoreApplication::applicationDirPath() : ui->logDir->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择日志目录"), start);
    if (!dir.isEmpty()) {
        ui->logDir->setText(dir);
    }
}

void SettingsDialog::onLogApplyClicked()
{
    // 收集参数
    const QString moduleName = ui->logModuleName->text().trimmed();
    const QString logDir = ui->logDir->text().trimmed();
    const int mb = ui->logSizeMB->value();
    const std::size_t bytes = static_cast<std::size_t>(mb) * 1024 * 1024;
    const QString levelText = ui->logLevelCombo->currentText().split(' ').first();
    const std::string level = levelText.toLower().toStdString();

    QStringList msgs;
    bool anyFail = false;

    if (moduleName.isEmpty()) {
        msgs << tr("模块名不能为空");
        anyFail = true;
    } else {
        if (!vistella::Logger::setModuleName(moduleName.toStdString())) {
            msgs << tr("模块名修改失败");
            anyFail = true;
        }
    }

    if (!logDir.isEmpty()) {
        if (!vistella::Logger::setLogDir(logDir.toStdString())) {
            msgs << tr("日志目录修改失败");
            anyFail = true;
        }
    }

    if (!vistella::Logger::setMaxFileSize(bytes)) {
        msgs << tr("单文件大小修改失败");
        anyFail = true;
    }

    if (!vistella::Logger::setLevelByName(level)) {
        msgs << tr("日志级别修改失败");
        anyFail = true;
    }

    refreshLogStatus();

    if (anyFail) {
        QMessageBox::warning(this, tr("日志设置"), msgs.join('\n'));
    } else {
        QMessageBox::information(
            this, tr("日志设置"),
            tr("日志设置已应用。\n模块=%1\n目录=%2\n级别=%3\n单文件=%4 MB")
                .arg(moduleName, logDir, levelText)
                .arg(mb));
    }
}

void SettingsDialog::onLogListClicked()
{
    const auto files = vistella::Logger::logFilesToday();
    if (files.empty()) {
        QMessageBox::information(this, tr("今日日志"),
            tr("今天没有日志文件。\n可能日志目录还没有创建文件。"));
        return;
    }
    QString msg = tr("今天的日志文件 (%1 个):\n").arg(files.size());
    for (const auto& f : files) {
        msg += QString::fromStdString(f) + "\n";
    }
    QMessageBox::information(this, tr("今日日志"), msg);
}
