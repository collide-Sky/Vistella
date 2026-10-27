#ifndef USER_H
#define USER_H

// =============================================================
// User / LoginLogEntry — 用户 + 登录日志数据结构
//
// 纯数据, 不含加密
// token 加密存数据库, 加 Q_DECLARE_METATYPE (未实装, 见 docs/DEBT.md#A-AUTH)
// =============================================================

#include <QString>
#include <QDateTime>

struct User {
    QString id;             // server id (UUID) / 本地 uuid
    QString username;       // 登录名
    QString displayName;    // 显示名
    QString email;
    QString avatarUrl;      // 头像 URL
    QString authProvider;   // 登录方式: "local" / "github" / "qq" / "wechat" / "phone"
    QString accessToken;    // 当前 access token (本地缓存, 加密)
    QString refreshToken;
    qint64  expiresAt = 0;  // token 过期时间 (epoch ms, 0 = 永不过期)
    QDateTime lastLoginAt;

    bool isValid() const { return !id.isEmpty() || !username.isEmpty(); }
};

struct LoginLogEntry {
    qint64 id = 0;
    QString username;
    bool success = false;
    QString ip;
    QDateTime at;
};

#endif // USER_H
