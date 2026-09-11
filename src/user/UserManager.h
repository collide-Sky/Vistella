#ifndef USERMANAGER_H
#define USERMANAGER_H

// =============================================================
// UserManager — 用户管理单例 (阶段 0 第 4 步)
//
// 状态机:
//   Guest            未登录
//   Authenticating   正在登录 (网络请求中)
//   Authenticated    登录成功, 有 user / token
//   Offline          离线模式 (本地有缓存 user, 但 token 过期, 自动登录失败)
//
// 阶段 0 占位:
//   - login / registerLocal / logout 都是 NoOp + emit 状态变化
//   - 网络层 (AuthClient) 留阶段 5
//   - 自动登录 (tryAutoLogin) 只读本地 SQLite, 找到 user 就切到 Authenticated
// 阶段 5+:
//   - 接 AuthClient (QQ/微信/手机号/GitHub OAuth)
//   - token 加密存数据库
// =============================================================

#include "User.h"

#include <QObject>
#include <QString>
#include <QList>
#include <memory>

class UserDatabase;

class UserManager : public QObject
{
    Q_OBJECT
public:
    enum State {
        Guest,            // 未登录
        Authenticating,   // 登录中
        Authenticated,    // 已登录
        Offline,          // 离线 (本地有缓存 user, 网络不可用/token 过期)
    };
    Q_ENUM(State)

    static UserManager &instance();

    // 启动时调, 打开本地 SQLite
    bool init(const QString &dbPath);

    // 状态
    State state() const { return m_state; }
    User currentUser() const { return m_currentUser; }
    bool isLoggedIn() const { return m_state == Authenticated; }

    // ----- 网络登录 (阶段 5 接 OAuth, 阶段 0 占位 NoOp) -----
    //   阶段 0: emit Authenticating → Authenticated/Failed (模拟, 不真发网络请求)
    //   阶段 5: 调 AuthClient, 成功后 saveUser + saveToken + emit loginSucceeded
    void login(const QString &username, const QString &password);
    void loginWithToken(const QString &accessToken);
    void logout();

    // ----- 本地注册 (offline-first) -----
    //   阶段 0: 直接 create User + saveUser + emit Authenticated
    void registerLocal(const QString &username, const QString &password);

    // ----- 自动登录 (启动时调) -----
    //   从本地 SQLite 读最近 user + token, 阶段 0 直接 emit Authenticated
    //   阶段 5: 调 AuthClient.refreshToken 验证 token 是否有效, 失败回 Offline
    void tryAutoLogin();

    // ----- 调试 / 查询 -----
    QList<User> localUsers() const;          // 本地数据库所有 user
    int localUserCount() const;
    QList<LoginLogEntry> recentLoginLogs(int limit = 50) const;

signals:
    void stateChanged(UserManager::State state);
    void loginSucceeded(const User &user);
    void loginFailed(const QString &reason);
    void userInfoUpdated(const User &user);
    void networkError(const QString &err);   // 阶段 5+

private:
    explicit UserManager(QObject *parent = nullptr);
    void setState(State s);

    State m_state = Guest;
    User  m_currentUser;
    std::unique_ptr<UserDatabase> m_db;
};

#endif // USERMANAGER_H
