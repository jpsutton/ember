// SPDX-License-Identifier: GPL-3.0-only

#include "Mpris.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMpris, "ember.mpris")

namespace ember {
namespace {

const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kTrackPath = QStringLiteral("/org/couchbox/ember/track/current");

}  // namespace

MprisRootAdaptor::MprisRootAdaptor(Mpris* parent) : QDBusAbstractAdaptor(parent), mpris_(parent) {}
void MprisRootAdaptor::Raise() { emit mpris_->raiseRequested(); }
void MprisRootAdaptor::Quit() { emit mpris_->quitRequested(); }

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris* parent) : QDBusAbstractAdaptor(parent), mpris_(parent) {}

QString MprisPlayerAdaptor::playbackStatus() const { return mpris_->status(); }

QVariantMap MprisPlayerAdaptor::metadata() const {
  if (mpris_->status() == QLatin1String("Stopped")) return {};
  QVariantMap map{{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(kTrackPath))},
                  {QStringLiteral("xesam:title"), mpris_->title()},
                  {QStringLiteral("mpris:length"), qlonglong(mpris_->duration() * 1e6)}};
  if (!mpris_->subtitle().isEmpty()) map.insert(QStringLiteral("xesam:artist"), QStringList{mpris_->subtitle()});
  if (!mpris_->artUrl().isEmpty()) map.insert(QStringLiteral("mpris:artUrl"), mpris_->artUrl());
  return map;
}

qlonglong MprisPlayerAdaptor::position() const { return qlonglong(mpris_->position() * 1e6); }
bool MprisPlayerAdaptor::canGoNext() const { return mpris_->canGoNext(); }
bool MprisPlayerAdaptor::canControl() const { return mpris_->status() != QLatin1String("Stopped"); }
void MprisPlayerAdaptor::Next() { emit mpris_->nextRequested(); }
void MprisPlayerAdaptor::Pause() { emit mpris_->pauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { emit mpris_->playPauseRequested(); }
void MprisPlayerAdaptor::Stop() { emit mpris_->stopRequested(); }
void MprisPlayerAdaptor::Play() { emit mpris_->playRequested(); }
void MprisPlayerAdaptor::Seek(qlonglong offset) { emit mpris_->seekRequested(double(offset) / 1e6); }
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath&, qlonglong position) {
  emit mpris_->setPositionRequested(double(position) / 1e6);
}

Mpris::Mpris(QObject* parent) : QObject(parent) {
  new MprisRootAdaptor(this);
  player_ = new MprisPlayerAdaptor(this);
  QDBusConnection bus = QDBusConnection::sessionBus();
  // One name per process, so couchbox-focus can match it by PID.
  const QString service = QStringLiteral("org.mpris.MediaPlayer2.ember");
  if (bus.registerObject(kObjectPath, this) && bus.registerService(service)) {
    registered_ = true;
  } else {
    qCWarning(lcMpris) << "could not register" << service << bus.lastError().message();
  }
}

Mpris::~Mpris() {
  if (registered_) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterService(QStringLiteral("org.mpris.MediaPlayer2.ember"));
    bus.unregisterObject(kObjectPath);
  }
}

void Mpris::publishChanges(const QStringList& properties) {
  if (!registered_) return;
  QVariantMap changed;
  for (const QString& name : properties) {
    if (name == QLatin1String("PlaybackStatus")) changed.insert(name, player_->playbackStatus());
    if (name == QLatin1String("Metadata")) changed.insert(name, player_->metadata());
    if (name == QLatin1String("CanGoNext")) changed.insert(name, player_->canGoNext());
    if (name == QLatin1String("CanControl")) {
      for (const char* control : {"CanPlay", "CanPause", "CanSeek", "CanControl"}) {
        changed.insert(QString::fromLatin1(control), player_->canControl());
      }
    }
  }
  QDBusMessage signal = QDBusMessage::createSignal(kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                   QStringLiteral("PropertiesChanged"));
  signal << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
  QDBusConnection::sessionBus().send(signal);
}

void Mpris::setStatus(const QString& v) {
  if (v == status_) return;
  status_ = v;
  emit changed();
  publishChanges({QStringLiteral("PlaybackStatus"), QStringLiteral("Metadata"), QStringLiteral("CanControl")});
}

void Mpris::setTitle(const QString& v) {
  if (v == title_) return;
  title_ = v;
  emit changed();
  publishChanges({QStringLiteral("Metadata")});
}

void Mpris::setSubtitle(const QString& v) {
  if (v == subtitle_) return;
  subtitle_ = v;
  emit changed();
  publishChanges({QStringLiteral("Metadata")});
}

void Mpris::setArtUrl(const QString& v) {
  if (v == art_url_) return;
  art_url_ = v;
  emit changed();
  publishChanges({QStringLiteral("Metadata")});
}

void Mpris::setDuration(double v) {
  if (qFuzzyCompare(v, duration_)) return;
  duration_ = v;
  emit changed();
  publishChanges({QStringLiteral("Metadata")});
}

void Mpris::setPosition(double v) {
  // Position is polled by clients, not signalled.
  position_ = v;
  emit positionChanged();
}

void Mpris::setCanGoNext(bool v) {
  if (v == can_go_next_) return;
  can_go_next_ = v;
  emit changed();
  publishChanges({QStringLiteral("CanGoNext")});
}

void Mpris::notifySeeked() { emit player_->Seeked(qlonglong(position_ * 1e6)); }

}  // namespace ember
