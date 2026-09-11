#include "UserDatabase.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <QDebug>

UserDatabase::UserDatabase(QObject *parent)
    : QObject(parent)
{
    // 连接名唯一 (避免多 UserDatabase 实例冲突)
    m_connName = QStringLiteral("user_db_%1")
                     .arg(reinterpret_cast<quintptr>(this));
}

UserDatabase::~UserDatabase()
{
    close();
}

bool UserDatabase::open(const QString &dbPath)
{
    if (m_db.isOpen()) close();

    // 确保目录存在
    QFileInfo fi(dbPath);
    QDir().mkpath(fi.absolutePath());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning("[UserDatabase] open '%s' failed: %s",
                 qPrintable(dbPath), qPrintable(m_db.lastError().text()));
        return false;
    }
    if (!ensureSchema()) {
        qWarning("[UserDatabase] ensureSchema failed");
        m_db.close();
        return false;
    }
    return true;
}

void UserDatabase::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::removeDatabase(m_connName);
    }
}

bool UserDatabase::ensureSchema()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "  id            TEXT PRIMARY KEY,"
            "  username      TEXT UNIQUE NOT NULL,"
            "  display_name  TEXT,"
            "  email         TEXT,"
            "  avatar_url    TEXT,"
            "  auth_provider TEXT NOT NULL DEFAULT 'local',"
            "  last_login_at INTEGER"
            ")"))) {
        qWarning("[UserDatabase] create users failed: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tokens ("
            "  user_id       TEXT PRIMARY KEY,"
            "  access_token  TEXT,"
            "  refresh_token TEXT,"
            "  expires_at    INTEGER DEFAULT 0,"
            "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
            ")"))) {
        qWarning("[UserDatabase] create tokens failed: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS login_logs ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  username   TEXT NOT NULL,"
            "  success    INTEGER NOT NULL,"
            "  ip         TEXT,"
            "  at         INTEGER NOT NULL"
            ")"))) {
        qWarning("[UserDatabase] create login_logs failed: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

User UserDatabase::userFromQuery(QSqlQuery &q)
{
    User u;
    u.id            = q.value(QStringLiteral("id")).toString();
    u.username      = q.value(QStringLiteral("username")).toString();
    u.displayName   = q.value(QStringLiteral("display_name")).toString();
    u.email         = q.value(QStringLiteral("email")).toString();
    u.avatarUrl     = q.value(QStringLiteral("avatar_url")).toString();
    u.authProvider  = q.value(QStringLiteral("auth_provider")).toString();
    const qint64 ts = q.value(QStringLiteral("last_login_at")).toLongLong();
    if (ts > 0) u.lastLoginAt = QDateTime::fromMSecsSinceEpoch(ts);
    return u;
}

LoginLogEntry UserDatabase::logFromQuery(QSqlQuery &q)
{
    LoginLogEntry e;
    e.id       = q.value(QStringLiteral("id")).toLongLong();
    e.username = q.value(QStringLiteral("username")).toString();
    e.success  = q.value(QStringLiteral("success")).toInt() != 0;
    e.ip       = q.value(QStringLiteral("ip")).toString();
    const qint64 ts = q.value(QStringLiteral("at")).toLongLong();
    if (ts > 0) e.at = QDateTime::fromMSecsSinceEpoch(ts);
    return e;
}

bool UserDatabase::saveUser(const User &u)
{
    if (u.username.isEmpty()) return false;
    // 没 id 自动生成 (本地新建)
    const QString id = u.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                       : u.id;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO users (id, username, display_name, email, avatar_url, auth_provider, last_login_at) "
        "VALUES (:id, :un, :dn, :em, :av, :ap, :ts) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  username=excluded.username, display_name=excluded.display_name, "
        "  email=excluded.email, avatar_url=excluded.avatar_url, "
        "  auth_provider=excluded.auth_provider, last_login_at=excluded.last_login_at"));
    q.bindValue(QStringLiteral(":id"), id);
    q.bindValue(QStringLiteral(":un"), u.username);
    q.bindValue(QStringLiteral(":dn"), u.displayName);
    q.bindValue(QStringLiteral(":em"), u.email);
    q.bindValue(QStringLiteral(":av"), u.avatarUrl);
    q.bindValue(QStringLiteral(":ap"), u.authProvider);
    q.bindValue(QStringLiteral(":ts"),
                u.lastLoginAt.isValid() ? u.lastLoginAt.toMSecsSinceEpoch() : 0);
    if (!q.exec()) {
        qWarning("[UserDatabase] saveUser failed: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

bool UserDatabase::removeUser(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM users WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    return q.exec();
}

User UserDatabase::loadUser(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, display_name, email, avatar_url, auth_provider, last_login_at "
        "FROM users WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) return userFromQuery(q);
    return User{};
}

User UserDatabase::loadUserByUsername(const QString &username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, display_name, email, avatar_url, auth_provider, last_login_at "
        "FROM users WHERE username = :un"));
    q.bindValue(QStringLiteral(":un"), username);
    if (q.exec() && q.next()) return userFromQuery(q);
    return User{};
}

QList<User> UserDatabase::allUsers()
{
    QList<User> result;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral(
            "SELECT id, username, display_name, email, avatar_url, auth_provider, last_login_at "
            "FROM users ORDER BY last_login_at DESC"))) {
        while (q.next()) result.append(userFromQuery(q));
    }
    return result;
}

bool UserDatabase::saveToken(const QString &userId,
                             const QString &accessToken,
                             const QString &refreshToken,
                             qint64 expiresAt)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO tokens (user_id, access_token, refresh_token, expires_at) "
        "VALUES (:u, :a, :r, :e) "
        "ON CONFLICT(user_id) DO UPDATE SET "
        "  access_token=excluded.access_token, refresh_token=excluded.refresh_token, "
        "  expires_at=excluded.expires_at"));
    q.bindValue(QStringLiteral(":u"), userId);
    q.bindValue(QStringLiteral(":a"), accessToken);
    q.bindValue(QStringLiteral(":r"), refreshToken);
    q.bindValue(QStringLiteral(":e"), expiresAt);
    if (!q.exec()) {
        qWarning("[UserDatabase] saveToken failed: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

QString UserDatabase::loadAccessToken(const QString &userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT access_token FROM tokens WHERE user_id = :u"));
    q.bindValue(QStringLiteral(":u"), userId);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

QString UserDatabase::loadRefreshToken(const QString &userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT refresh_token FROM tokens WHERE user_id = :u"));
    q.bindValue(QStringLiteral(":u"), userId);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

bool UserDatabase::removeToken(const QString &userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM tokens WHERE user_id = :u"));
    q.bindValue(QStringLiteral(":u"), userId);
    return q.exec();
}

bool UserDatabase::logLogin(const QString &username, bool success, const QString &ip)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO login_logs (username, success, ip, at) VALUES (:u, :s, :ip, :at)"));
    q.bindValue(QStringLiteral(":u"), username);
    q.bindValue(QStringLiteral(":s"), success ? 1 : 0);
    q.bindValue(QStringLiteral(":ip"), ip);
    q.bindValue(QStringLiteral(":at"), QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        qWarning("[UserDatabase] logLogin failed: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

QList<LoginLogEntry> UserDatabase::recentLogins(int limit)
{
    QList<LoginLogEntry> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, success, ip, at FROM login_logs ORDER BY at DESC LIMIT :n"));
    q.bindValue(QStringLiteral(":n"), limit);
    if (q.exec()) {
        while (q.next()) result.append(logFromQuery(q));
    }
    return result;
}
