#ifndef USERDATABASE_H
#define USERDATABASE_H

// =============================================================
// UserDatabase — 本地 SQLite 存储 (阶段 0 第 4 步)
//
// 3 张表:
//   users       - 用户基本信息 (id / username / display / email / avatar / provider)
//   tokens      - access/refresh token 缓存 (阶段 5 加密, 阶段 0 明文)
//   login_logs  - 登录日志 (用户名 / 成功失败 / IP / 时间)
//
// 线程: 单线程 (MainWindow 调), 不做 QSqlDatabase 多线程考虑
// =============================================================

#include "User.h"

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QDateTime>

class UserDatabase : public QObject
{
    Q_OBJECT
public:
    explicit UserDatabase(QObject *parent = nullptr);
    ~UserDatabase() override;

    // 打开 db 文件 (会自动 create + ensureSchema), 失败返回 false
    bool open(const QString &dbPath);
    void close();
    bool isOpen() const { return m_db.isOpen(); }

    // ----- users 表 -----
    bool saveUser(const User &u);
    bool removeUser(const QString &id);
    User loadUser(const QString &id);
    User loadUserByUsername(const QString &username);
    QList<User> allUsers();

    // ----- tokens 表 (阶段 0 明文, 阶段 5 加密) -----
    bool saveToken(const QString &userId,
                   const QString &accessToken,
                   const QString &refreshToken,
                   qint64 expiresAt = 0);
    QString loadAccessToken(const QString &userId);
    QString loadRefreshToken(const QString &userId);
    bool removeToken(const QString &userId);

    // ----- login_logs 表 -----
    bool logLogin(const QString &username, bool success, const QString &ip = QString());
    QList<LoginLogEntry> recentLogins(int limit = 100);

private:
    bool ensureSchema();
    static User userFromQuery(class QSqlQuery &q);
    static LoginLogEntry logFromQuery(class QSqlQuery &q);

    QSqlDatabase m_db;
    QString m_connName;
};

#endif // USERDATABASE_H
