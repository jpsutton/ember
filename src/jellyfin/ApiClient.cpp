// SPDX-License-Identifier: GPL-3.0-only

#include "ApiClient.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>

#include <QCoroNetworkReply>

Q_LOGGING_CATEGORY(lcApi, "ember.api")

namespace ember::jellyfin {
namespace {

constexpr int kDefaultTimeoutMs = 20000;

// Header values are quoted; keep quotes and control characters out of them.
QString HeaderValue(QString value) {
  value.remove(QLatin1Char('"'));
  value.removeIf([](QChar c) { return c.category() == QChar::Other_Control; });
  return QString::fromLatin1(QUrl::toPercentEncoding(value, " ()._-"));
}

}  // namespace

ApiClient::ApiClient(QNetworkAccessManager* network, QString device_id, QObject* parent)
    : QObject(parent), network_(network), device_id_(std::move(device_id)) {}

void ApiClient::setBaseUrl(const QUrl& url) {
  base_url_ = url;
  // No trailing slash: paths are appended with their own.
  QString path = base_url_.path();
  while (path.endsWith(QLatin1Char('/'))) path.chop(1);
  base_url_.setPath(path);
}

QString ApiClient::clientName() { return QStringLiteral("Ember"); }

QString ApiClient::clientVersion() {
  const QString version = QCoreApplication::applicationVersion();
  return version.isEmpty() ? QStringLiteral("0.0.0") : version;
}

QString ApiClient::deviceName() {
  const QString host = QSysInfo::machineHostName();
  return host.isEmpty() ? QStringLiteral("couchbox") : host;
}

QByteArray ApiClient::authorization() const {
  QString value = QStringLiteral("MediaBrowser Client=\"%1\", Device=\"%2\", DeviceId=\"%3\", Version=\"%4\"")
                      .arg(HeaderValue(clientName()), HeaderValue(deviceName()), HeaderValue(device_id_),
                           HeaderValue(clientVersion()));
  if (!token_.isEmpty()) value += QStringLiteral(", Token=\"%1\"").arg(HeaderValue(token_));
  return value.toUtf8();
}

QUrl ApiClient::url(const QString& path, const QUrlQuery& query) const {
  QUrl result = base_url_;
  result.setPath(base_url_.path() + path);
  if (!query.isEmpty()) result.setQuery(query);
  return result;
}

QUrl ApiClient::authenticatedUrl(const QString& path, QUrlQuery query) const {
  if (!token_.isEmpty()) query.addQueryItem(QStringLiteral("ApiKey"), token_);
  return url(path, query);
}

QCoro::Task<Reply> ApiClient::get(QString path, QUrlQuery query) {
  co_return co_await send("GET", url(path, query), {}, kDefaultTimeoutMs);
}

QCoro::Task<Reply> ApiClient::post(QString path, QJsonDocument body, QUrlQuery query) {
  co_return co_await send("POST", url(path, query), body.isNull() ? QByteArray() : body.toJson(QJsonDocument::Compact),
                          kDefaultTimeoutMs);
}

QCoro::Task<Reply> ApiClient::del(QString path, QUrlQuery query) {
  co_return co_await send("DELETE", url(path, query), {}, kDefaultTimeoutMs);
}

QCoro::Task<Reply> ApiClient::getAt(QUrl base, QString path, int timeout_ms) {
  QString base_path = base.path();
  while (base_path.endsWith(QLatin1Char('/'))) base_path.chop(1);
  base.setPath(base_path + path);
  co_return co_await send("GET", base, {}, timeout_ms);
}

QCoro::Task<Reply> ApiClient::send(QByteArray verb, QUrl url, QByteArray body, int timeout_ms) {
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", authorization());
  request.setRawHeader("Accept", "application/json");
  // No charset: /Sessions/Playing answers 415 when one is given.
  if (!body.isEmpty() || verb == "POST") request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setTransferTimeout(timeout_ms);

  QNetworkReply* reply = network_->sendCustomRequest(request, verb, body);
  co_await qCoro(reply).waitForFinished();
  reply->deleteLater();

  Reply result;
  result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray data = reply->readAll();
  if (!data.isEmpty()) result.json = QJsonDocument::fromJson(data);
  if (reply->error() != QNetworkReply::NoError) {
    result.error = result.status != 0 ? QStringLiteral("HTTP %1").arg(result.status) : reply->errorString();
    qCInfo(lcApi).noquote() << verb << url.path() << "failed:" << result.error;
  } else {
    qCDebug(lcApi).noquote() << verb << url.path() << result.status;
  }
  co_return result;
}

}  // namespace ember::jellyfin
