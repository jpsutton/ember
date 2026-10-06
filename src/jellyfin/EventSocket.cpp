// SPDX-License-Identifier: GPL-3.0-only
// Follows Plezy's lib/services/library_events/media_browser_library_event_socket.dart
// (GPL-3.0).

#include "EventSocket.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUrlQuery>

#include "ApiClient.h"

Q_LOGGING_CATEGORY(lcEvents, "ember.events")

namespace ember::jellyfin {
namespace {

constexpr int kMaxReconnectDelayMs = 60000;

}  // namespace

EventSocket::EventSocket(ApiClient* api, QObject* parent) : QObject(parent), api_(api) {
  keep_alive_.setInterval(30000);
  connect(&keep_alive_, &QTimer::timeout, this, [this]() {
    socket_.sendTextMessage(QStringLiteral(R"({"MessageType":"KeepAlive"})"));
  });
  reconnect_.setSingleShot(true);
  connect(&reconnect_, &QTimer::timeout, this, &EventSocket::connectSocket);
  connect(&socket_, &QWebSocket::connected, this, [this]() {
    qCInfo(lcEvents) << "connected";
    reconnect_delay_ms_ = 2000;
    keep_alive_.start();
  });
  connect(&socket_, &QWebSocket::disconnected, this, [this]() {
    keep_alive_.stop();
    if (running_) scheduleReconnect();
  });
  connect(&socket_, &QWebSocket::textMessageReceived, this, &EventSocket::onMessage);
}

void EventSocket::start() {
  if (running_) return;
  running_ = true;
  connectSocket();
}

void EventSocket::stop() {
  running_ = false;
  reconnect_.stop();
  keep_alive_.stop();
  socket_.close();
}

void EventSocket::connectSocket() {
  if (!running_ || api_->token().isEmpty()) return;
  if (socket_.state() != QAbstractSocket::UnconnectedState) return;
  qCDebug(lcEvents) << "connecting";
  QUrl url = api_->authenticatedUrl(QStringLiteral("/socket"), [this]() {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("deviceId"), api_->deviceId());
    return query;
  }());
  url.setScheme(url.scheme() == QLatin1String("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
  socket_.open(url);
}

void EventSocket::scheduleReconnect() {
  if (reconnect_.isActive()) return;
  reconnect_.start(reconnect_delay_ms_);
  reconnect_delay_ms_ = std::min(reconnect_delay_ms_ * 2, kMaxReconnectDelayMs);
}

void EventSocket::onMessage(const QString& text) {
  const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
  const QString type = message.value(QStringLiteral("MessageType")).toString();
  qCDebug(lcEvents).noquote() << "message" << type << text.left(300);
  if (type == QLatin1String("ForceKeepAlive")) {
    // The server's timeout in seconds; keep alive at half of it.
    const int seconds = message.value(QStringLiteral("Data")).toInt(60);
    keep_alive_.start(std::clamp(seconds * 500, 10000, 300000));
  } else if (type == QLatin1String("UserDataChanged")) {
    const QJsonObject data = message.value(QStringLiteral("Data")).toObject();
    for (const QJsonValue& value : data.value(QStringLiteral("UserDataList")).toArray()) {
      const QJsonObject user_data = value.toObject();
      emit userDataChanged(user_data.value(QStringLiteral("ItemId")).toString(), user_data);
    }
  } else if (type == QLatin1String("LibraryChanged")) {
    emit libraryChanged();
  }
}

}  // namespace ember::jellyfin
