// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWebSocket>

namespace ember::jellyfin {

class ApiClient;

// The server's live event stream (/socket). Ember listens for changes to
// watched state and to the library, so lists stay current when something
// else (another client, the server) changes them.
class EventSocket : public QObject {
  Q_OBJECT

 public:
  explicit EventSocket(ApiClient* api, QObject* parent = nullptr);

  void start();
  void stop();

 signals:
  // UserData of one item changed: {Played, PlaybackPositionTicks, ...}.
  void userDataChanged(const QString& item_id, const QJsonObject& user_data);
  // Items were added, removed or updated.
  void libraryChanged();

 private:
  void connectSocket();
  void scheduleReconnect();
  void onMessage(const QString& text);

  ApiClient* api_;
  QWebSocket socket_;
  QTimer keep_alive_;
  QTimer reconnect_;
  int reconnect_delay_ms_ = 2000;
  bool running_ = false;
};

}  // namespace ember::jellyfin
