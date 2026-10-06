// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

#include <QCoroTask>

class QNetworkAccessManager;

namespace ember::jellyfin {

// The outcome of one HTTP request to the server.
struct Reply {
  int status = 0;           // HTTP status; 0 when the request never got one
  QString error;            // set when the request failed for any reason
  QJsonDocument json;       // the body, when it was JSON
  bool ok() const { return error.isEmpty(); }
  QJsonObject object() const { return json.object(); }
  QJsonArray array() const { return json.array(); }
};

// Talks to one Jellyfin server. Every request carries the MediaBrowser
// Authorization header (Jellyfin 12 rejects X-Emby-Token and api_key), and
// the coroutines all resume on the GUI thread.
class ApiClient : public QObject {
  Q_OBJECT

 public:
  ApiClient(QNetworkAccessManager* network, QString device_id, QObject* parent = nullptr);

  QUrl baseUrl() const { return base_url_; }
  void setBaseUrl(const QUrl& url);
  QString token() const { return token_; }
  void setToken(const QString& token) { token_ = token; }
  QString deviceId() const { return device_id_; }

  // The header value, with the token when one is set.
  QByteArray authorization() const;

  // |path| starts with "/". |query| is appended as is.
  QUrl url(const QString& path, const QUrlQuery& query = {}) const;
  // The same URL with ApiKey added, for players that can't send headers.
  QUrl authenticatedUrl(const QString& path, QUrlQuery query = {}) const;

  QCoro::Task<Reply> get(QString path, QUrlQuery query = {});
  QCoro::Task<Reply> post(QString path, QJsonDocument body = {}, QUrlQuery query = {});
  QCoro::Task<Reply> del(QString path, QUrlQuery query = {});
  // A request against an arbitrary server URL (used before a server is
  // chosen, to probe candidates).
  QCoro::Task<Reply> getAt(QUrl base, QString path, int timeout_ms);

  static QString clientName();
  static QString clientVersion();
  static QString deviceName();

 private:
  QCoro::Task<Reply> send(QByteArray verb, QUrl url, QByteArray body, int timeout_ms);

  QNetworkAccessManager* network_;
  QUrl base_url_;
  QString token_;
  QString device_id_;
};

}  // namespace ember::jellyfin
