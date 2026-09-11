#include "HttpClient.h"

#include <QJsonObject>
#include <QDebug>

// =============================================================
// HttpClient 单例实现 (阶段 0 占位)
// 阶段 5+ 真正实现: 用 QNetworkAccessManager 包装
// =============================================================

HttpClient &HttpClient::instance()
{
    static HttpClient inst;
    return inst;
}

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
{
}

void HttpClient::getAsync(const QUrl &url, const QMap<QString, QString> &headers)
{
    Q_UNUSED(headers);
    // 阶段 0 占位: 立即 emit 错误 (不真发请求)
    //   阶段 5+ 用 QNetworkAccessManager::get(QNetworkRequest(url))
    qWarning("[HttpClient] getAsync stub: %s", qPrintable(url.toString()));
    Response r;
    r.success = false;
    r.errorMessage = QStringLiteral("HttpClient not implemented yet (阶段 0 stub)");
    emit response(r);
    emit error(url, r.errorMessage);
}

void HttpClient::postAsync(const QUrl &url,
                           const QByteArray &body,
                           const QMap<QString, QString> &headers)
{
    Q_UNUSED(body);
    Q_UNUSED(headers);
    qWarning("[HttpClient] postAsync stub: %s (%1 bytes)",
             qPrintable(url.toString()), body.size());
    Response r;
    r.success = false;
    r.errorMessage = QStringLiteral("HttpClient not implemented yet (阶段 0 stub)");
    emit response(r);
    emit error(url, r.errorMessage);
}

void HttpClient::postJsonAsync(const QUrl &url,
                               const QJsonObject &json,
                               const QMap<QString, QString> &headers)
{
    // 阶段 5+: QJsonDocument(json).toJson(QJsonDocument::Compact) -> postAsync
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QMap<QString, QString> h = headers;
    h[QStringLiteral("Content-Type")] = QStringLiteral("application/json");
    if (!m_bearerToken.isEmpty()) {
        h[QStringLiteral("Authorization")] = QStringLiteral("Bearer ") + m_bearerToken;
    }
    postAsync(url, body, h);
}
