#include "HttpClient.h"

#include <QJsonObject>
#include <QDebug>

// =============================================================
// HttpClient 单例实现 (mock: 不发真实请求)
//  真正实现: 用 QNetworkAccessManager 包装 (未实装, 见 docs/DEBT.md#A-AUTH)
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
    //  mock: 立即 emit 错误 (不发真实请求)
    //    用 QNetworkAccessManager::get(QNetworkRequest(url)) (未实装, 见 docs/DEBT.md#A-AUTH)
    qWarning("[HttpClient] getAsync stub: %s", qPrintable(url.toString()));
    Response r;
    r.success = false;
    r.errorMessage = QStringLiteral("HttpClient not implemented yet");
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
    r.errorMessage = QStringLiteral("HttpClient not implemented yet");
    emit response(r);
    emit error(url, r.errorMessage);
}

void HttpClient::postJsonAsync(const QUrl &url,
                               const QJsonObject &json,
                               const QMap<QString, QString> &headers)
{
    // QJsonDocument(json).toJson(QJsonDocument::Compact) -> postAsync (未实装, 见 docs/DEBT.md#A-AUTH)
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QMap<QString, QString> h = headers;
    h[QStringLiteral("Content-Type")] = QStringLiteral("application/json");
    if (!m_bearerToken.isEmpty()) {
        h[QStringLiteral("Authorization")] = QStringLiteral("Bearer ") + m_bearerToken;
    }
    postAsync(url, body, h);
}
