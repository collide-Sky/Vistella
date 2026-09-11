#include "AuthClient.h"

#include <QDateTime>
#include <QUuid>

// =============================================================
// AuthClient 单例实现 (阶段 0 占位)
//
// 阶段 0: 所有 loginXxxAsync 都立即 emit 模拟成功的 AuthResult
// 阶段 5+ 接真 OAuth: 改用 HttpClient POST 到 auth server, 解析 JSON 返回
// =============================================================

AuthClient &AuthClient::instance()
{
    static AuthClient inst;
    return inst;
}

AuthClient::AuthClient(QObject *parent)
    : QObject(parent)
{
}

AuthClient::AuthResult AuthClient::mockSuccess(Provider p, const QString &username)
{
    AuthResult r;
    r.success     = true;
    r.provider    = p;
    r.accessToken = QStringLiteral("mock-access-%1")
                        .arg(QDateTime::currentMSecsSinceEpoch());
    r.refreshToken = QStringLiteral("mock-refresh-%1")
                         .arg(QDateTime::currentMSecsSinceEpoch());
    r.expiresAt   = QDateTime::currentMSecsSinceEpoch() + 3600 * 1000;  // 1h

    // 构造 mock user
    r.user.id           = QStringLiteral("mock-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    r.user.username     = username;
    r.user.displayName  = username;
    r.user.email        = QString();
    r.user.avatarUrl    = QString();
    r.user.authProvider = [&]{
        switch (p) {
        case Provider::Local:   return QStringLiteral("local");
        case Provider::GitHub:  return QStringLiteral("github");
        case Provider::QQ:      return QStringLiteral("qq");
        case Provider::WeChat:  return QStringLiteral("wechat");
        case Provider::Phone:   return QStringLiteral("phone");
        }
        return QStringLiteral("unknown");
    }();
    r.user.lastLoginAt  = QDateTime::currentDateTime();
    return r;
}

void AuthClient::loginLocalAsync(const QString &username, const QString &password)
{
    Q_UNUSED(password);
    // 阶段 0: 立即模拟成功
    // 阶段 5+: 调 HttpClient POST {auth_server}/auth/local, 解析返回 token + user
    emit loginResult(mockSuccess(Provider::Local, username));
}

void AuthClient::loginGitHubAsync(const QString &oauthCode)
{
    Q_UNUSED(oauthCode);
    // 阶段 0: 模拟成功 (用 oauthCode 当 username, 方便调试)
    // 阶段 5+: 调 HttpClient POST {auth_server}/auth/github/exchange
    //            body: { code: oauthCode } -> { access_token, refresh_token, user }
    emit loginResult(mockSuccess(Provider::GitHub,
                                  QStringLiteral("github_%1").arg(oauthCode.left(8))));
}

void AuthClient::loginQQAsync(const QString &oauthCode)
{
    Q_UNUSED(oauthCode);
    emit loginResult(mockSuccess(Provider::QQ,
                                  QStringLiteral("qq_%1").arg(oauthCode.left(8))));
}

void AuthClient::loginWeChatAsync(const QString &oauthCode)
{
    Q_UNUSED(oauthCode);
    emit loginResult(mockSuccess(Provider::WeChat,
                                  QStringLiteral("wechat_%1").arg(oauthCode.left(8))));
}

void AuthClient::loginPhoneAsync(const QString &phone, const QString &smsCode)
{
    Q_UNUSED(smsCode);
    // 阶段 5+: 调 HttpClient POST {auth_server}/auth/phone
    //            body: { phone, code } -> { access_token, ... }
    emit loginResult(mockSuccess(Provider::Phone, phone));
}

void AuthClient::requestSmsCodeAsync(const QString &phone)
{
    Q_UNUSED(phone);
    // 阶段 5+: 调 HttpClient POST {auth_server}/auth/phone/send-code
    emit smsCodeSent(true, QString());
}

void AuthClient::refreshTokenAsync(const QString &refreshToken)
{
    Q_UNUSED(refreshToken);
    // 阶段 5+: 调 HttpClient POST {auth_server}/auth/refresh
    const qint64 newExpires = QDateTime::currentMSecsSinceEpoch() + 3600 * 1000;
    emit refreshTokenResult(true,
                            QStringLiteral("mock-refreshed-access"),
                            QStringLiteral("mock-refreshed-refresh"),
                            newExpires);
}

void AuthClient::logoutAsync(const QString &accessToken)
{
    Q_UNUSED(accessToken);
    // 阶段 5+: 调 HttpClient POST {auth_server}/auth/logout
    // 阶段 0: 无操作, UserManager.logout() 本地清理即可
}
