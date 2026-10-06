// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace ember {

class Mpris;

// org.mpris.MediaPlayer2
class MprisRootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(bool CanQuit READ canQuit)
  Q_PROPERTY(bool CanRaise READ canRaise)
  Q_PROPERTY(bool HasTrackList READ hasTrackList)
  Q_PROPERTY(QString Identity READ identity)
  Q_PROPERTY(QString DesktopEntry READ desktopEntry)
  Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
  Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

 public:
  explicit MprisRootAdaptor(Mpris* parent);
  bool canQuit() const { return true; }
  bool canRaise() const { return true; }
  bool hasTrackList() const { return false; }
  QString identity() const { return QStringLiteral("Ember"); }
  QString desktopEntry() const { return QStringLiteral("org.couchbox.ember"); }
  QStringList supportedUriSchemes() const { return {}; }
  QStringList supportedMimeTypes() const { return {}; }

 public slots:
  void Raise();
  void Quit();

 private:
  Mpris* mpris_;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(double Rate READ rate)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(double Volume READ volume)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(double MinimumRate READ rate)
  Q_PROPERTY(double MaximumRate READ rate)
  Q_PROPERTY(bool CanGoNext READ canGoNext)
  Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
  Q_PROPERTY(bool CanPlay READ canControl)
  Q_PROPERTY(bool CanPause READ canControl)
  Q_PROPERTY(bool CanSeek READ canControl)
  Q_PROPERTY(bool CanControl READ canControl)

 public:
  explicit MprisPlayerAdaptor(Mpris* parent);
  QString playbackStatus() const;
  double rate() const { return 1.0; }
  QVariantMap metadata() const;
  double volume() const { return 1.0; }
  qlonglong position() const;
  bool canGoNext() const;
  bool canGoPrevious() const { return false; }
  bool canControl() const;

 public slots:
  void Next();
  void Previous() {}
  void Pause();
  void PlayPause();
  void Stop();
  void Play();
  void Seek(qlonglong offset);
  void SetPosition(const QDBusObjectPath& track, qlonglong position);

 signals:
  void Seeked(qlonglong position);

 private:
  Mpris* mpris_;
};

// Publishes the player on D-Bus as org.mpris.MediaPlayer2.ember, so Plasma's
// media keys and couchbox's pause-on-minimize (couchbox-focus pauses the
// MPRIS player of the minimized window's process) reach it. QML sets the
// state and handles the requests.
class Mpris : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY changed)  // Playing, Paused, Stopped
  Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
  Q_PROPERTY(QString subtitle READ subtitle WRITE setSubtitle NOTIFY changed)
  Q_PROPERTY(QString artUrl READ artUrl WRITE setArtUrl NOTIFY changed)
  Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY changed)  // seconds
  Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)  // seconds
  Q_PROPERTY(bool canGoNext READ canGoNext WRITE setCanGoNext NOTIFY changed)

 public:
  explicit Mpris(QObject* parent = nullptr);
  ~Mpris() override;

  QString status() const { return status_; }
  void setStatus(const QString& v);
  QString title() const { return title_; }
  void setTitle(const QString& v);
  QString subtitle() const { return subtitle_; }
  void setSubtitle(const QString& v);
  QString artUrl() const { return art_url_; }
  void setArtUrl(const QString& v);
  double duration() const { return duration_; }
  void setDuration(double v);
  double position() const { return position_; }
  void setPosition(double v);
  bool canGoNext() const { return can_go_next_; }
  void setCanGoNext(bool v);

  // Tells MPRIS clients the position jumped.
  Q_INVOKABLE void notifySeeked();

 signals:
  void changed();
  void positionChanged();
  void playRequested();
  void pauseRequested();
  void playPauseRequested();
  void stopRequested();
  void nextRequested();
  void seekRequested(double seconds);  // relative
  void setPositionRequested(double seconds);
  void raiseRequested();
  void quitRequested();

 private:
  void publishChanges(const QStringList& properties);

  MprisPlayerAdaptor* player_;
  QString status_ = QStringLiteral("Stopped");
  QString title_;
  QString subtitle_;
  QString art_url_;
  double duration_ = 0;
  double position_ = 0;
  bool can_go_next_ = false;
  bool registered_ = false;
};

}  // namespace ember
