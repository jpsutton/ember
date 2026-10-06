// SPDX-License-Identifier: GPL-3.0-only

#include "SystemIntegration.h"

#include <KConfigGroup>
#include <KFormat>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>

#include "../jellyfin/ApiClient.h"
#include "../jellyfin/Session.h"

Q_LOGGING_CATEGORY(lcSystem, "ember.system")

namespace ember {

ScreenInhibitor::ScreenInhibitor(QObject* parent) : QObject(parent) {}

ScreenInhibitor::~ScreenInhibitor() { setActive(false); }

void ScreenInhibitor::setActive(bool active) {
  if (active == active_) return;
  active_ = active;
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (active) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Inhibit"));
    call << QStringLiteral("org.couchbox.ember") << QStringLiteral("Playing video");
    const QDBusReply<uint> reply = bus.call(call, QDBus::Block, 2000);
    if (reply.isValid()) {
      cookie_ = reply.value();
      qCInfo(lcSystem) << "screen saver inhibited";
    } else {
      qCWarning(lcSystem) << "could not inhibit the screen saver:" << reply.error().message();
    }
  } else if (cookie_ != 0) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("UnInhibit"));
    call << cookie_;
    bus.call(call, QDBus::NoBlock);
    cookie_ = 0;
    qCInfo(lcSystem) << "screen saver allowed again";
  }
  emit activeChanged();
}

CouchboxConfig::CouchboxConfig(QObject* parent) : QObject(parent) {
  const KConfigGroup video = KSharedConfig::openConfig(QStringLiteral("couchboxrc"))->group(QStringLiteral("Video"));
  transcode_hevc_ = video.readEntry("TranscodeHEVC", false);
  transcode_hevc10_ = video.readEntry("TranscodeHEVC10", false);
  transcode_av1_ = video.readEntry("TranscodeAV1", false);
  transcode_vp9_ = video.readEntry("TranscodeVP9", false);
  fast_scaling_ = video.readEntry("PlezyScaling", QString()) == QLatin1String("fast");
  qCInfo(lcSystem, "couchboxrc: transcode hevc=%d hevc10=%d av1=%d vp9=%d, fast scaling=%d", transcode_hevc_,
         transcode_hevc10_, transcode_av1_, transcode_vp9_, fast_scaling_);
}

QVariantMap ViewSettings::load(const QString& key) const {
  const KConfigGroup group = KSharedConfig::openConfig(QStringLiteral("emberrc"))->group(QStringLiteral("View-") + key);
  QVariantMap values;
  for (const QString& entry : group.keyList()) values.insert(entry, group.readEntry(entry, QString()));
  return values;
}

void ViewSettings::save(const QString& key, const QVariantMap& values) {
  KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("emberrc"));
  KConfigGroup group = config->group(QStringLiteral("View-") + key);
  for (auto it = values.begin(); it != values.end(); ++it) group.writeEntry(it.key(), it.value().toString());
  config->sync();
}

namespace {

// Adds the Authorization header for requests to the signed-in server.
class ImageNetworkAccessManager : public QNetworkAccessManager {
 public:
  using QNetworkAccessManager::QNetworkAccessManager;

 protected:
  QNetworkReply* createRequest(Operation op, const QNetworkRequest& original, QIODevice* data) override {
    QNetworkRequest request(original);
    if (auto* session = jellyfin::Session::instance()) {
      const QUrl base = session->api()->baseUrl();
      if (request.url().host() == base.host() && request.url().port() == base.port()) {
        request.setRawHeader("Authorization", session->api()->authorization());
      }
    }
    // Item images are immutable per tag, so the cached copy is always good.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    return QNetworkAccessManager::createRequest(op, request, data);
  }
};

}  // namespace

QNetworkAccessManager* ImageNetworkFactory::create(QObject* parent) {
  auto* manager = new ImageNetworkAccessManager(parent);
  auto* cache = new QNetworkDiskCache(manager);
  cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images"));
  cache->setMaximumCacheSize(qint64(500) * 1024 * 1024);
  manager->setCache(cache);
  return manager;
}

void KeyInjector::press(int key) const {
  QWindow* window = QGuiApplication::focusWindow();
  if (window == nullptr) {
    const QWindowList windows = QGuiApplication::topLevelWindows();
    if (windows.isEmpty()) return;
    window = windows.first();
  }
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &press);
  QCoreApplication::sendEvent(window, &release);
}

QString Format::spelloutDuration(double seconds) const {
  if (seconds <= 0) return {};
  return KFormat().formatSpelloutDuration(quint64(seconds * 1000));
}

QString Format::clock(double seconds) const {
  if (seconds < 0 || seconds != seconds) seconds = 0;
  const qint64 s = qint64(seconds);
  if (s >= 3600) {
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString Format::timeOfDay(double seconds_from_now) const {
  return QLocale().toString(QDateTime::currentDateTime().addSecs(qint64(seconds_from_now)).time(), QLocale::ShortFormat);
}

}  // namespace ember
