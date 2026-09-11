#include "UserManager.h"
#include "UserDatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDebug>

UserManager &UserManager::instance()
{
    static UserManager inst;
    return inst;
}

UserManager::UserManager(QObject *parent)
    : QObject(parent)
    , m_db(std::make_unique<UserDatabase>())
{
}

bool UserManager::init(const QString &dbPath)
{
    if (!m_db) {
        m_db = std::make_unique<UserDatabase>();
    }
    if (!m_db->open(dbPath)) {
        qWarning("[UserManager] open db '%s' failed", qPrintable(dbPath));
        return false;
    }
    qInfo("[UserManager] db opened: %s", qPrintable(dbPath));
    return true;
}

void UserManager::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void UserManager::login(const QString &username, const QString &password)
{
    // 阶段 0 占位: 模拟登录, 不真发网络请求
    //   阶段 5+ 接 AuthClient, 调真实 OAuth 接口
    qInfo("[UserManager] login requested: user='%s'", qPrintable(username));
    setState(Authenticating);

    if (username.isEmpty() || password.isEmpty()) {
        emit loginFailed(tr("用户名或密码不能为空"));
        setState(Guest);
        return;
    }

    // 模拟成功: 构造一个本地 user
    User u;
    u.id            = QStringLiteral("local-%1").arg(username);
    u.username      = username;
    u.displayName   = username;
    u.email         = QString();
    u.avatarUrl     = QString();
    u.authProvider  = QStringLiteral("local");
    u.accessToken   = QStringLiteral("mock-token-%1").arg(QDateTime::currentMSecsSinceEpoch());
    u.refreshToken  = QString();
    u.expiresAt     = 0;
    u.lastLoginAt   = QDateTime::currentDateTime();

    if (m_db) m_db->saveUser(u);
    m_currentUser = u;
    emit userInfoUpdated(u);
    emit loginSucceeded(u);
    setState(Authenticated);
}

void UserManager::loginWithToken(const QString &accessToken)
{
    // 阶段 0 占位: 模拟
    qInfo("[UserManager] loginWithToken: token='%s...'", qPrintable(accessToken.left(8)));
    setState(Authenticating);
    // 阶段 5+: 调 AuthClient 验证 token, 拿 user info
    User u;
    u.id            = QStringLiteral("token-user");
    u.username      = QStringLiteral("token_user");
    u.displayName   = QStringLiteral("Token User");
    u.authProvider  = QStringLiteral("token");
    u.accessToken   = accessToken;
    m_currentUser = u;
    emit userInfoUpdated(u);
    emit loginSucceeded(u);
    setState(Authenticated);
}

void UserManager::logout()
{
    qInfo("[UserManager] logout: user='%s'", qPrintable(m_currentUser.username));
    if (m_db && !m_currentUser.id.isEmpty()) {
        m_db->removeToken(m_currentUser.id);
    }
    m_currentUser = User{};
    setState(Guest);
}

void UserManager::registerLocal(const QString &username, const QString &password)
{
    Q_UNUSED(password);  // 阶段 5 才做密码哈希
    qInfo("[UserManager] registerLocal: user='%s'", qPrintable(username));
    if (username.isEmpty()) {
        emit loginFailed(tr("用户名不能为空"));
        return;
    }
    User u;
    u.id           = QStringLiteral("local-reg-%1").arg(username);
    u.username     = username;
    u.displayName  = username;
    u.authProvider = QStringLiteral("local");
    u.lastLoginAt  = QDateTime::currentDateTime();
    if (m_db) m_db->saveUser(u);
    m_currentUser = u;
    emit userInfoUpdated(u);
    emit loginSucceeded(u);
    setState(Authenticated);
}

void UserManager::tryAutoLogin()
{
    // 阶段 0: 读本地最近 user, 直接切到 Authenticated
    // 阶段 5+: 验证 token 有效性, 失败回 Offline
    if (!m_db || !m_db->isOpen()) {
        qWarning("[UserManager] tryAutoLogin: db not open");
        return;
    }
    const QList<User> users = m_db->allUsers();
    if (users.isEmpty()) {
        qInfo("[UserManager] tryAutoLogin: no local user, stay Guest");
        return;
    }
    const User &u = users.first();
    m_currentUser = u;
    qInfo("[UserManager] tryAutoLogin: use '%s' (%s)",
          qPrintable(u.username), qPrintable(u.authProvider));
    emit userInfoUpdated(u);
    emit loginSucceeded(u);
    setState(Authenticated);
}

QList<User> UserManager::localUsers() const
{
    return m_db ? m_db->allUsers() : QList<User>{};
}

int UserManager::localUserCount() const
{
    return m_db ? m_db->allUsers().size() : 0;
}

QList<LoginLogEntry> UserManager::recentLoginLogs(int limit) const
{
    return m_db ? m_db->recentLogins(limit) : QList<LoginLogEntry>{};
}
