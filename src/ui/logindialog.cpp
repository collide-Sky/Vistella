#include "logindialog.h"
#include "ui_logindialog.h"

#include <QPushButton>
#include <QButtonGroup>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("登录"));
    setModal(true);
    // 不调 resize, 用 .ui 里的 geometry

    setupProviderButtons();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::setupProviderButtons()
{
    // 4 个第三方 + 游客: 走 QButtonGroup 把 5 个按钮绑互斥, id = Provider 枚举
    if (!ui->providerBox) return;

    QButtonGroup *group = new QButtonGroup(this);
    group->setExclusive(false);   // 互斥会让"点游客自动取消其他", 不想要这个

    struct Btn {
        const char   *name;
        Provider      id;
        const char   *fallbackLabel;   // 阶段 0 没图标, 用文字
    };
    const Btn kBtns[] = {
        { "githubBtn",  ProviderGitHub, "GitHub"  },
        { "qqBtn",      ProviderQQ,     "QQ"      },
        { "wechatBtn",  ProviderWeChat, "微信"    },
        { "phoneBtn",   ProviderPhone,  "手机号"  },
        { "guestBtn",   ProviderGuest,  "继续以游客身份" },
    };

    for (const auto &b : kBtns) {
        QPushButton *btn = findChild<QPushButton *>(QString::fromLatin1(b.name));
        if (!btn) continue;
        // 阶段 0 占位: 按钮文字 (阶段 5+ 配图标 + 颜色)
        if (btn->text().isEmpty()) {
            btn->setText(QString::fromUtf8(b.fallbackLabel));
        }
        btn->setProperty("providerId", int(b.id));
        group->addButton(btn, int(b.id));
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            onProviderClicked(btn->property("providerId").toInt());
        });
    }
}

void LoginDialog::onProviderClicked(int id)
{
    // 阶段 0: 直接 emit + accept (关闭)
    // 阶段 5+: 先调 AuthClient::loginXxxAsync(...), 改按钮文字为"登录中...", disable
    //          等 AuthClient::loginFinished 再 accept/reject
    Provider p = static_cast<Provider>(id);
    emit providerChosen(p);
    accept();
}
