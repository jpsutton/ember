// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QQmlNetworkAccessManagerFactory>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace ember {

// Holds org.freedesktop.ScreenSaver.Inhibit while active. couchbox turns the
// display off after ten minutes and Bigscreen's own inhibit is disabled, so
// playback must hold one. The inhibit is tied to the bus connection, so a
// crash releases it.
class ScreenInhibitor : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

 public:
  explicit ScreenInhibitor(QObject* parent = nullptr);
  ~ScreenInhibitor() override;

  bool active() const { return active_; }
  void setActive(bool active);

 signals:
  void activeChanged();

 private:
  bool active_ = false;
  uint cookie_ = 0;
};

// Settings couchbox keeps for every player, in ~/.config/couchboxrc [Video],
// set from the GPU's decode capabilities by couchbox-video-profile.
class CouchboxConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool transcodeHevc READ transcodeHevc CONSTANT)
  Q_PROPERTY(bool transcodeHevc10 READ transcodeHevc10 CONSTANT)
  Q_PROPERTY(bool transcodeAv1 READ transcodeAv1 CONSTANT)
  Q_PROPERTY(bool transcodeVp9 READ transcodeVp9 CONSTANT)
  // couchbox's "fast" scaling profile for weak GPUs.
  Q_PROPERTY(bool fastScaling READ fastScaling CONSTANT)

 public:
  explicit CouchboxConfig(QObject* parent = nullptr);

  bool transcodeHevc() const { return transcode_hevc_; }
  bool transcodeHevc10() const { return transcode_hevc10_; }
  bool transcodeAv1() const { return transcode_av1_; }
  bool transcodeVp9() const { return transcode_vp9_; }
  bool fastScaling() const { return fast_scaling_; }

 private:
  bool transcode_hevc_ = false;
  bool transcode_hevc10_ = false;
  bool transcode_av1_ = false;
  bool transcode_vp9_ = false;
  bool fast_scaling_ = false;
};

// Per-library view choices (sort, order, hide watched, list style),
// remembered in ~/.config/emberrc.
class ViewSettings : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  using QObject::QObject;

  Q_INVOKABLE QVariantMap load(const QString& key) const;
  Q_INVOKABLE void save(const QString& key, const QVariantMap& values);
};

// Gives QML's image loader a disk cache and the server's auth header.
class ImageNetworkFactory : public QQmlNetworkAccessManagerFactory {
 public:
  QNetworkAccessManager* create(QObject* parent) override;
};

// Presses keys on behalf of a remote-control command from another Jellyfin
// client, as if the remote had sent them.
class KeyInjector : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  using QObject::QObject;

  Q_INVOKABLE void press(int key) const;
};

// Formatting helpers for QML.
class Format : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  using QObject::QObject;

  // "1 hour, 32 minutes"
  Q_INVOKABLE QString spelloutDuration(double seconds) const;
  // "1:32:05" / "32:05"
  Q_INVOKABLE QString clock(double seconds) const;
  // Local time of day, "21:47"
  Q_INVOKABLE QString timeOfDay(double seconds_from_now) const;
};

}  // namespace ember
