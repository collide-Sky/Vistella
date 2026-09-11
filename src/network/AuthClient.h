#ifndef AUTHCLIENT_H
#define AUTHCLIENT_H

// =============================================================
// AuthClient — 第三方登录 OAuth 客户端 (阶段 0 第 9 步, 占位)
//
// 设计目标:
//   1. 阶段 0: 纯接口 + 空 stub, 模拟成功返回 (配合 UserManager 阶段 0 流程跑通)
//   2. 阶段 5+ 真正接入 OAuth:
//      - GitHub:    OAuth 2.0 Authorization Code 流程
//      - QQ:        QQ 互联 OAuth 2.0
//      - WeChat:    微信开放平台 OAuth 2.0
//      - Phone:     短信验证码 (自定义 provider, 调 SMS 网关)
//
// 接口设计原则:
//   - 异步 (全部 signal 返回), 不阻塞 UI
//   - 统一 AuthResult 结构 (success / tokens / user / errorMessage)
//   - HttpClient 通用 GET/POST, AuthClient 拼 URL + 解析 response
// =============================================================

#include "../user/User.h"

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QUrl>

class AuthClient : public QObject
{
    Q_OBJECT
public:
    enum class Provider {
        Local,        // 本地用户名+密码 (阶段 0 占位模拟, 阶段 5+ 真用 UserDatabase)
        GitHub,
        QQ,
        WeChat,
        Phone,        // 手机号 + 短信验证码
    };
    Q_ENUM(Provider)

    // 通用登录结果 (所有 provider 共用)
    struct AuthResult {
        bool success = false;
        Provider provider = Provider::Local;
        QString accessToken;
        QString refreshToken;
        qint64  expiresAt = 0;        // epoch ms, 0 = 永不过期
        User    user;                // 服务端返回的 user profile
        QString errorMessage;        // 失败时填
        QString errorCode;           // 机器可读错误码 (阶段 5+ 接入 OAuth 用)
    };

    static AuthClient &instance();

    // ----- 服务端地址 (阶段 5+ 申请完 OAuth 后填真地址) -----
    void setAuthServerBaseUrl(const QUrl &url) { m_authServerBaseUrl = url; }
    QUrl authServerBaseUrl() const { return m_authServerBaseUrl; }

    // ----- 各种 provider 登录 (异步, 通过 loginResult 信号返回) -----
    //   阶段 0: 模拟成功 (返回本地 mock User)
    //   阶段 5+ 接真 OAuth 时:
    //     - GitHub/QQ/WeChat: 拉浏览器 OAuth 流程, 拿到 code 后调 loginXxxAsync(code)
    //     - Phone: 先 requestSmsCodeAsync(phone), 用户填验证码, 调 loginPhoneAsync(phone, code)
    void loginLocalAsync(const QString &username, const QString &password);
    void loginGitHubAsync(const QString &oauthCode);
    void loginQQAsync(const QString &oauthCode);
    void loginWeChatAsync(const QString &oauthCode);
    void loginPhoneAsync(const QString &phone, const QString &smsCode);

    // 短信验证码 (阶段 5+ 接 SMS 网关)
    void requestSmsCodeAsync(const QString &phone);

    // 刷新 token (阶段 5+ 接 OAuth 后用, 避免 access token 过期要重新登录)
    void refreshTokenAsync(const QString &refreshToken);

    // 注销 (阶段 5+ 接 OAuth 后用, 调 auth server /logout)
    void logoutAsync(const QString &accessToken);

signals:
    // 通用登录结果 (所有 provider 共用)
    void loginResult(const AuthClient::AuthResult &result);

    // 短信验证码发送结果
    void smsCodeSent(bool success, const QString &errorMessage);

    // token 刷新结果
    void refreshTokenResult(bool success,
                            const QString &newAccessToken,
                            const QString &newRefreshToken,
                            qint64 expiresAt);

    // 网络错误 (阶段 0 不会触发, 阶段 5+ 网络异常时触发)
    void networkError(const QString &url, const QString &err);

private:
    explicit AuthClient(QObject *parent = nullptr);
    AuthClient(const AuthClient &) = delete;
    AuthClient &operator=(const AuthClient &) = delete;

    // 阶段 0 内部辅助: 构造一个 mock User + token, 用于所有 loginXxxAsync 占位返回
    static AuthResult mockSuccess(Provider p, const QString &username);

    QUrl m_authServerBaseUrl;   // 阶段 0 留空, 阶段 5+ 配真地址
};

#endif // AUTHCLIENT_H
