// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <QCoroTask>

#include "MpvVideo.h"

namespace ember {

// Plays one Jellyfin item through an MpvVideo: negotiates direct play or a
// server transcode (POST /Items/{id}/PlaybackInfo), picks audio and subtitle
// tracks, reports progress to the server, and finds the next episode.
//
// Behaviour follows Plezy's Jellyfin backend (GPL-3.0): playback.dart,
// playback_progress_tracker.dart, track_selection_service.dart.
class Playback : public QObject {
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(MpvVideo* video READ video WRITE setVideo NOTIFY videoChanged)
  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap item READ item NOTIFY itemChanged)
  Q_PROPERTY(QString title READ title NOTIFY itemChanged)
  Q_PROPERTY(QString subtitle READ subtitle NOTIFY itemChanged)
  Q_PROPERTY(double position READ position NOTIFY positionChanged)
  Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
  Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
  Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
  // True once the first frame is up; until then QML keeps the screen black.
  Q_PROPERTY(bool videoShown READ videoShown NOTIFY videoShownChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(QString playMethod READ playMethod NOTIFY streamChanged)
  Q_PROPERTY(QVariantList chapters READ chapters NOTIFY itemChanged)
  // {index, title, language, codec, channels, isDefault, isForced, isExternal, selected}
  Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
  Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
  Q_PROPERTY(int audioIndex READ audioIndex NOTIFY tracksChanged)
  Q_PROPERTY(int subtitleIndex READ subtitleIndex NOTIFY tracksChanged)
  // Intro and credits ranges in seconds (-1 when unknown).
  Q_PROPERTY(double introStart READ introStart NOTIFY segmentsChanged)
  Q_PROPERTY(double introEnd READ introEnd NOTIFY segmentsChanged)
  Q_PROPERTY(double creditsStart READ creditsStart NOTIFY segmentsChanged)
  // The episode after this one, or empty.
  Q_PROPERTY(QVariantMap nextItem READ nextItem NOTIFY nextItemChanged)
  // {url (with %1 for the tile index), width, height, tileWidth, tileHeight,
  //  count, interval (seconds)}, or empty.
  Q_PROPERTY(QVariantMap trickplay READ trickplay NOTIFY itemChanged)

 public:
  enum class State { Idle, Loading, Playing, Ended, Failed };
  Q_ENUM(State)

  explicit Playback(QObject* parent = nullptr);
  ~Playback() override;

  MpvVideo* video() const { return video_; }
  void setVideo(MpvVideo* video);
  State state() const { return state_; }
  QVariantMap item() const { return item_; }
  QString title() const;
  QString subtitle() const;
  double position() const { return position_; }
  double duration() const { return duration_; }
  bool paused() const { return paused_; }
  bool buffering() const { return buffering_; }
  bool videoShown() const { return video_shown_; }
  QString errorString() const { return error_string_; }
  QString playMethod() const { return play_method_; }
  QVariantList chapters() const { return chapters_; }
  QVariantList audioTracks() const { return Tracks(QStringLiteral("Audio"), audio_index_); }
  QVariantList subtitleTracks() const { return Tracks(QStringLiteral("Subtitle"), subtitle_index_); }
  int audioIndex() const { return audio_index_; }
  int subtitleIndex() const { return subtitle_index_; }
  double introStart() const { return intro_start_; }
  double introEnd() const { return intro_end_; }
  double creditsStart() const { return credits_start_; }
  QVariantMap nextItem() const { return next_item_; }
  QVariantMap trickplay() const { return trickplay_; }

  // Starts |item_id|, resuming from the saved position unless |from_start|.
  Q_INVOKABLE void play(const QString& item_id, bool from_start = false);
  // Starts |item_id| at |seconds| (a remote "Play On" request).
  Q_INVOKABLE void playAt(const QString& item_id, double seconds);
  // Stops and reports the position; emits finished().
  Q_INVOKABLE void stop();
  Q_INVOKABLE void togglePause();
  Q_INVOKABLE void setPaused(bool paused);
  Q_INVOKABLE void seekRelative(double seconds);
  Q_INVOKABLE void seekTo(double seconds);
  Q_INVOKABLE void selectAudio(int index);
  // -1 turns subtitles off.
  Q_INVOKABLE void selectSubtitle(int index);
  Q_INVOKABLE void playNext();

 signals:
  void videoChanged();
  void stateChanged();
  void itemChanged();
  void positionChanged();
  void durationChanged();
  void pausedChanged();
  void bufferingChanged();
  void videoShownChanged();
  void errorStringChanged();
  void streamChanged();
  void tracksChanged();
  void segmentsChanged();
  void nextItemChanged();
  // Playback ended by the user or at the end of the file, and the server
  // has been told. |completed| is true at the natural end.
  void finished(bool completed);
  // A seek jumped the position (for MPRIS).
  void seeked();

 private:
  struct Stream {
    QString url;
    QString method;  // DirectPlay, DirectStream, Transcode
    QString media_source_id;
    QString play_session_id;
    QJsonObject source;
  };

  // |start_seconds| < 0 resumes from the saved position (or 0 with |from_start|).
  QCoro::Task<> playTask(QString item_id, bool from_start, double start_seconds, quint64 generation);
  QCoro::Task<bool> negotiate(double start_seconds, quint64 generation);
  QCoro::Task<> loadSegments(QString item_id, quint64 generation);
  QCoro::Task<> loadNextItem(QJsonObject item, quint64 generation);
  QCoro::Task<> report(QString what);
  QJsonObject deviceProfile() const;
  QJsonObject progressBody() const;
  void loadStream(double start_seconds);
  void applyAudioSettings();
  void chooseDefaultTracks();
  void applyTrackSelection();
  void onMpvEvent(const QString& name, const QVariantMap& data);
  void onMpvProperty(const QString& name, const QVariant& value);
  void setState(State state);
  void setError(const QString& error);
  void endSession(bool completed);
  QVariantList Tracks(const QString& type, int selected) const;
  QJsonObject streamByIndex(int index) const;
  int mpvTrackId(const QString& type, int jellyfin_index) const;

  QPointer<MpvVideo> video_;
  State state_ = State::Idle;
  QVariantMap item_;
  QJsonObject item_json_;
  Stream stream_;
  double position_ = 0;
  double duration_ = 0;
  double start_seconds_ = 0;
  bool paused_ = false;
  bool buffering_ = false;
  bool video_shown_ = false;
  QString error_string_;
  QString play_method_;
  QVariantList chapters_;
  int audio_index_ = -1;
  int subtitle_index_ = -1;
  double intro_start_ = -1;
  double intro_end_ = -1;
  double credits_start_ = -1;
  QVariantMap next_item_;
  QVariantMap trickplay_;
  QVariantList mpv_tracks_;
  bool started_reported_ = false;
  bool session_open_ = false;
  bool file_loaded_ = false;
  // The next play() comes from Up Next: mark the current item watched.
  bool mark_previous_played_ = false;
  quint64 generation_ = 0;
  QTimer progress_timer_;
};

}  // namespace ember
