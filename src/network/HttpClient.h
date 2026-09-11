#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

// =============================================================
// HttpClient — 通用 HTTP 客户端 (阶段 0 第 9 步, 占位)
//
// 阶段 0: 空 stub, 不真发请求
// 阶段 5+ 真正实现:
//   - 基于 QNetworkAccessManager (Qt Network 模块)
//   - 支持 HTTPS / 自定义 CA / Bearer Token / 超时 / 重试
//   - 异步回调 (response signal)
//   - AuthClient 拼 URL + 解析 response
//
// 设计原则:
//   - 单例 (跟 AuthClient / MediaDispatcher / AIManager 一致)
//   - 异步不阻塞, 错误统一走 error signal
// =============================================================

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QUrl>
#include <QMap>

class HttpClient : public QObject
{
    Q_OBJECT
public:
    struct Response {
        int statusCode = 0;
        QByteArray body;
        QMap<QString, QString> headers;
        QString errorMessage;
        bool success = false;       // statusCode 200-299 + 无网络错误
    };

    static HttpClient &instance();

    // ----- 配置 -----
    void setTimeout(int ms) { m_timeoutMs = ms; }
    int  timeout() const { return m_timeoutMs; }

    // 全局 Bearer token (AuthClient 调 loginAsync 拿到 access_token 后, 调这里)
    void setBearerToken(const QString &token) { m_bearerToken = token; }
    QString bearerToken() const { return m_bearerToken; }

    // ----- 通用 GET / POST (阶段 0 空 stub, 阶段 5+ 用 QNetworkAccessManager 实现) -----
    void getAsync(const QUrl &url, const QMap<QString, QString> &headers = {});
    void postAsync(const QUrl &url,
                   const QByteArray &body,
                   const QMap<QString, QString> &headers = {});

    // ----- JSON 便利 (阶段 5+ 实现: postAsync + 解析 response.body) -----
    void postJsonAsync(const QUrl &url,
                       const QJsonObject &json,
                       const QMap<QString, QString> &headers = {});

signals:
    void response(const HttpClient::Response &resp);
    void error(const QUrl &url, const QString &err);

private:
    explicit HttpClient(QObject *parent = nullptr);
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    int     m_timeoutMs = 30 * 1000;  // 30s default
    QString m_bearerToken;
};

#endif // HTTPCLIENT_H
