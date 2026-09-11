#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class LoginDialog; }
QT_END_NAMESPACE

// =============================================================
// LoginDialog — 登录对话框 (2026-09-02 阶段 0 第 10 步, 骨架)
//
// 阶段 0: 只搭 UI, 按钮点击不真发请求
//   - 4 个第三方登录按钮 (GitHub / QQ / 微信 / 手机号) + "继续以游客身份"
//   - 点任意按钮: 关闭对话框 + emit providerChosen(provider)
//   - 不连 AuthClient::loginXxxAsync, 也不真改 UserManager 状态
//
// 阶段 5+:
//   - 接 AuthClient::loginGitHubAsync() / loginQQAsync() / ...
//   - 等 AuthClient::loginFinished 信号回来再弹"成功/失败"提示
//   - 成功则调 UserManager::instance().setState(Authenticated, user)
// =============================================================
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    enum Provider {
        ProviderLocal      = 0,
        ProviderGitHub     = 1,
        ProviderQQ         = 2,
        ProviderWeChat     = 3,
        ProviderPhone      = 4,
        ProviderGuest      = 5,
    };
    Q_ENUM(Provider)

    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog() override;

    // 对外暴露: 调用方可以指定默认 provider (比如上次失败过的)
    void setDefaultProvider(Provider p) { m_defaultProvider = p; }

signals:
    // 阶段 0: 选了就发, 不等真结果; 阶段 5+ 改成等 AuthClient 回调
    void providerChosen(LoginDialog::Provider provider);

private slots:
    void onProviderClicked(int id);

private:
    void setupProviderButtons();

    Ui::LoginDialog *ui;
    Provider         m_defaultProvider = ProviderGitHub;
};

#endif // LOGINDIALOG_H
