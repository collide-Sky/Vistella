#include "themegallerydialog.h"
#include "ui_themegallerydialog.h"

#include <QMessageBox>
#include <QToolButton>
#include <QButtonGroup>
#include <QSignalMapper>
#include <QFrame>
#include <QLabel>

// 8 套预设 accent 颜色 (阶段 0 占位, 阶段 5+ 由 ThemeManager 注册表提供)
struct AccentPreset {
    QString name;   // 内部 id
    QString hex;    // #RRGGBB
    QString label;  // 显示文字
};

static const AccentPreset kAccents[] = {
    { "green",  "#4caf80", "青绿" },
    { "blue",   "#4a90e2", "天空" },
    { "purple", "#8e6ddf", "紫罗兰" },
    { "red",    "#e57373", "珊瑚" },
    { "orange", "#f0a040", "橙黄" },
    { "teal",   "#26a69a", "水鸭" },
    { "pink",   "#ec7aa6", "粉樱" },
    { "gray",   "#90a4ae", "雾灰" },
};

ThemeGalleryDialog::ThemeGalleryDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ThemeGalleryDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("主题画廊"));
    setModal(true);

    setupAccentPalette();
    updatePreview();
}

ThemeGalleryDialog::~ThemeGalleryDialog()
{
    delete ui;
}

void ThemeGalleryDialog::setupAccentPalette()
{
    // 把 .ui 里预留的 8 个 toolButton (accent01..accent08) 绑成互斥组,
    // 点哪个就更新预览 (阶段 0 不发信号也不持久化)
    if (!ui->accentGrid) return;

    QButtonGroup *group = new QButtonGroup(this);
    group->setExclusive(true);

    int n = qMin<int>(8, int(sizeof(kAccents) / sizeof(kAccents[0])));
    for (int i = 0; i < n; ++i) {
        const AccentPreset &p = kAccents[i];
        QString btnName = QString("accent%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        QToolButton *btn = findChild<QToolButton *>(btnName);
        if (!btn) continue;

        // 用 setStyleSheet 给按钮填色 (阶段 0 简单做法, 阶段 5+ 用 theme-aware qproperty)
        QString qss = QString("QToolButton { background-color: %1; border: 1px solid #80808080; border-radius: 4px; }"
                              "QToolButton:hover { border: 1px solid %1; }"
                              "QToolButton:checked { border: 2px solid #ffffff; outline: 2px solid %1; }")
                          .arg(p.hex);
        btn->setStyleSheet(qss);
        btn->setToolTip(p.label);
        btn->setCheckable(true);
        btn->setProperty("accentHex", p.hex);
        btn->setProperty("accentName", p.name);
        group->addButton(btn, i);

        // 默认选中第 1 个 (青绿 #4caf80, 跟项目主色一致)
        if (i == 0) {
            btn->setChecked(true);
            m_currentAccent = p.hex;
        }

        connect(btn, &QToolButton::clicked, this, &ThemeGalleryDialog::onAccentClicked);
    }

    // 模式 radio (light / dark) 阶段 0 占位
    if (ui->lightRadio) {
        ui->lightRadio->setChecked(true);
        connect(ui->lightRadio, &QRadioButton::toggled, this, &ThemeGalleryDialog::onModeToggled);
    }
    if (ui->darkRadio) {
        connect(ui->darkRadio, &QRadioButton::toggled, this, &ThemeGalleryDialog::onModeToggled);
    }

    // 应用 / 关闭
    if (ui->applyBtn)  connect(ui->applyBtn,  &QPushButton::clicked, this, &ThemeGalleryDialog::onApplyClicked);
    if (ui->closeBtn)  connect(ui->closeBtn,  &QPushButton::clicked, this, &QDialog::close);
}

void ThemeGalleryDialog::onAccentClicked()
{
    QToolButton *btn = qobject_cast<QToolButton *>(sender());
    if (!btn) return;
    m_currentAccent = btn->property("accentHex").toString();
    updatePreview();
}

void ThemeGalleryDialog::onModeToggled()
{
    if (!ui->lightRadio) return;
    m_currentMode = ui->lightRadio->isChecked() ? 0 : 1;
    updatePreview();
}

void ThemeGalleryDialog::updatePreview()
{
    // 预览框底色: light = 白底 + accent 强调线, dark = 暗灰底 + accent 强调线
    if (ui->previewFrame) {
        QString bg  = (m_currentMode == 0) ? "#ffffff" : "#2b2b2b";
        QString fg  = (m_currentMode == 0) ? "#222222" : "#dddddd";
        QString qss = QString("QFrame#previewFrame { background-color: %1; border: 1px solid #80808080; border-radius: 4px; }"
                              "QLabel { color: %2; }"
                              "QFrame#accentBar { background-color: %3; border: none; border-radius: 2px; }")
                          .arg(bg, fg, m_currentAccent);
        ui->previewFrame->setStyleSheet(qss);
    }
    if (ui->accentLabel) {
        ui->accentLabel->setText(QString("Accent: %1").arg(m_currentAccent));
    }
}

void ThemeGalleryDialog::onApplyClicked()
{
    // 阶段 0 占位: 不真调 ThemeManager
    // 阶段 5+: ThemeManager::instance().setAccentColor(m_currentAccent);
    //           ThemeManager::instance().setMode(m_currentMode == 0 ? Light : Dark);
    //           ThemeManager::instance().persistToSettings();
    QMessageBox::information(this,
                             tr("主题画廊"),
                             tr("已选中 accent=%1, mode=%2\n阶段 0 仅占位, 阶段 5+ 真正接入 ThemeManager.")
                                 .arg(m_currentAccent)
                                 .arg(m_currentMode == 0 ? tr("亮色") : tr("暗色")));
    accept();
}
