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
  // Remote control from another Jellyfin client ("Play On").
  // |command|: PlayNow, PlayNext, PlayLast.
  void playRequested(const QStringList& item_ids, qint64 start_ticks, int start_index, const QString& command);
  // |command|: Stop, Pause, Unpause, PlayPause, NextTrack, PreviousTrack,
  // Seek, Rewind, FastForward.
  void playstateRequested(const QString& command, qint64 seek_ticks);
  // GeneralCommand: navigation (MoveUp, Select, Back, GoHome...),
  // DisplayMessage, SetAudioStreamIndex, SetSubtitleStreamIndex...
  void generalCommand(const QString& name, const QJsonObject& arguments);

 private:
  void connectSocket();
  void scheduleReconnect();
  void onMessage(const QString& text);
  // Tells the server this device can be played to and remote-controlled.
  void reportCapabilities();

  ApiClient* api_;
  QWebSocket socket_;
  QTimer keep_alive_;
  QTimer reconnect_;
  int reconnect_delay_ms_ = 2000;
  bool running_ = false;
};

}  // namespace ember::jellyfin
