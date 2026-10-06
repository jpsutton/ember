// SPDX-License-Identifier: GPL-3.0-only

#include "Session.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QUdpSocket>
#include <QUuid>

#include <QCoroTimer>

#include "ApiClient.h"
#include "EventSocket.h"

Q_LOGGING_CATEGORY(lcSession, "ember.session")

namespace ember::jellyfin {
namespace {

Session* g_instance = nullptr;

constexpr quint16 kDiscoveryPort = 7359;
constexpr int kDiscoveryWindowMs = 2000;
constexpr int kProbeTimeoutMs = 5000;
constexpr int kQuickConnectPollMs = 3000;
constexpr int kQuickConnectLifetimeMs = 5 * 60 * 1000;

// Library kinds Ember can browse. Music, live TV, books, photos and
// playlists are left out.
bool IsSupportedCollection(const QString& type) {
  static const QStringList supported = {QString(),           QStringLiteral("movies"),     QStringLiteral("tvshows"),
                                        QStringLiteral("boxsets"), QStringLiteral("homevideos"),
                                        QStringLiteral("musicvideos"), QStringLiteral("mixed"), QStringLiteral("folders")};
  return supported.contains(type);
}

// Candidate base URLs for what the user typed.
QList<QUrl> Candidates(const QString& address) {
  QString text = address.trimmed();
  while (text.endsWith(QLatin1Char('/'))) text.chop(1);
  if (text.isEmpty()) return {};
  if (text.contains(QStringLiteral("://"))) return {QUrl(text)};
  const QUrl probe(QStringLiteral("http://") + text);
  if (probe.port() != -1) return {QUrl(QStringLiteral("http://") + text), QUrl(QStringLiteral("https://") + text)};
  return {QUrl(QStringLiteral("http://%1:8096").arg(text)), QUrl(QStringLiteral("https://%1").arg(text)),
          QUrl(QStringLiteral("https://%1:8920").arg(text)), QUrl(QStringLiteral("http://%1").arg(text))};
}

}  // namespace

Session::Session(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
  g_instance = this;
  network_->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
  load();
}

Session::~Session() {
  if (g_instance == this) g_instance = nullptr;
}

Session* Session::instance() { return g_instance; }

Session* Session::create(QQmlEngine*, QJSEngine*) {
  QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
  return g_instance;
}

QString Session::statePath() const {
  return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/ember/session");
}

void Session::load() {
  KConfig config(statePath(), KConfig::SimpleConfig);
  KConfigGroup device = config.group(QStringLiteral("Device"));
  QString device_id = device.readEntry("Id", QString());
  if (device_id.isEmpty()) device_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  api_ = new ApiClient(network_, device_id, this);
  events_ = new EventSocket(api_, this);
  connect(events_, &EventSocket::userDataChanged, this,
          [this](const QString& item_id, const QJsonObject&) { emit userDataChanged(item_id); });
  connect(events_, &EventSocket::libraryChanged, this, &Session::libraryChanged);
  connect(events_, &EventSocket::playRequested, this,
          [this](const QStringList& ids, qint64 start_ticks, int start_index, const QString& command) {
            emit remotePlay(ids, start_ticks / 1e7, start_index, command);
          });
  connect(events_, &EventSocket::playstateRequested, this, [this](const QString& command, qint64 seek_ticks) {
    emit remotePlaystate(command, seek_ticks / 1e7);
  });
  connect(events_, &EventSocket::generalCommand, this, [this](const QString& name, const QJsonObject& arguments) {
    emit remoteCommand(name, arguments.toVariantMap());
  });

  KConfigGroup server = config.group(QStringLiteral("Server"));
  const QString url = server.readEntry("Url", QString());
  if (!url.isEmpty()) {
    api_->setBaseUrl(QUrl(url));
    server_id_ = server.readEntry("Id", QString());
    server_name_ = server.readEntry("Name", QString());
    server_version_ = server.readEntry("Version", QString());
    state_ = State::SignedOut;
  }
  KConfigGroup user = config.group(QStringLiteral("User"));
  const QString token = user.readEntry("Token", QString());
  if (state_ == State::SignedOut && !token.isEmpty()) {
    api_->setToken(token);
    user_id_ = user.readEntry("Id", QString());
    user_name_ = user.readEntry("Name", QString());
    state_ = State::SignedIn;
  }
  KConfigGroup libraries = config.group(QStringLiteral("Libraries"));
  libraries_chosen_ = libraries.readEntry("Chosen", false);
  hidden_library_ids_ = libraries.readEntry("Hidden", QStringList());  // hidden ids, see save()
  // Cached so the home menu can show before the server answers.
  const QStringList cached = libraries.readEntry("Cache", QStringList());
  for (const QString& entry : cached) {
    const QStringList parts = entry.split(QLatin1Char('\t'));
    if (parts.size() != 3) continue;
    libraries_.append({parts[0], parts[1], parts[2], !hidden_library_ids_.contains(parts[0])});
  }

  if (state_ == State::SignedIn) {
    events_->start();
    validateSessionTask();
    refreshLibrariesTask();
  }
}

void Session::save() const {
  const QString path = statePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  KConfig config(path, KConfig::SimpleConfig);
  config.group(QStringLiteral("Device")).writeEntry("Id", api_->deviceId());
  KConfigGroup server = config.group(QStringLiteral("Server"));
  server.writeEntry("Url", api_->baseUrl().toString());
  server.writeEntry("Id", server_id_);
  server.writeEntry("Name", server_name_);
  server.writeEntry("Version", server_version_);
  KConfigGroup user = config.group(QStringLiteral("User"));
  user.writeEntry("Token", api_->token());
  user.writeEntry("Id", user_id_);
  user.writeEntry("Name", user_name_);
  KConfigGroup libraries = config.group(QStringLiteral("Libraries"));
  libraries.writeEntry("Chosen", libraries_chosen_);
  // Hidden rather than shown, so a library added on the server later shows up.
  QStringList hidden;
  QStringList cache;
  for (const Library& library : libraries_) {
    if (!library.shown) hidden.append(library.id);
    cache.append(QStringList{library.id, library.name, library.collection_type}.join(QLatin1Char('\t')));
  }
  libraries.writeEntry("Hidden", hidden);
  libraries.writeEntry("Cache", cache);
  config.sync();
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString Session::serverUrl() const { return api_->baseUrl().toString(); }

QVariantList Session::libraries() const {
  QVariantList result;
  for (const Library& library : libraries_) {
    result.append(QVariantMap{{QStringLiteral("id"), library.id},
                              {QStringLiteral("name"), library.name},
                              {QStringLiteral("collectionType"), library.collection_type},
                              {QStringLiteral("shown"), library.shown}});
  }
  return result;
}

QVariantList Session::shownLibraries() const {
  QVariantList result;
  for (const QVariant& entry : libraries()) {
    if (entry.toMap().value(QStringLiteral("shown")).toBool()) result.append(entry);
  }
  return result;
}

void Session::setState(State state) {
  // The event stream runs exactly while someone is signed in.
  if (state == State::SignedIn) {
    events_->start();
  } else {
    events_->stop();
  }
  if (state == state_) return;
  state_ = state;
  emit stateChanged();
}

void Session::setBusy(bool busy) {
  if (busy == busy_) return;
  busy_ = busy;
  emit busyChanged();
}

void Session::setError(const QString& error) {
  if (error == error_string_) return;
  error_string_ = error;
  emit errorStringChanged();
}

void Session::discover() {
  if (discovery_socket_ != nullptr) return;
  discovered_servers_.clear();
  emit discoveredServersChanged();
  discovery_socket_ = new QUdpSocket(this);
  if (!discovery_socket_->bind(QHostAddress::AnyIPv4, 0)) {
    qCWarning(lcSession) << "discovery: could not bind:" << discovery_socket_->errorString();
  }
  connect(discovery_socket_, &QUdpSocket::readyRead, this, [this]() {
    while (discovery_socket_ && discovery_socket_->hasPendingDatagrams()) {
      const QNetworkDatagram datagram = discovery_socket_->receiveDatagram();
      const QJsonObject reply = QJsonDocument::fromJson(datagram.data()).object();
      const QString address = reply.value(QStringLiteral("Address")).toString();
      const QString id = reply.value(QStringLiteral("Id")).toString();
      if (address.isEmpty() || id.isEmpty()) continue;
      bool known = false;
      for (const QVariant& server : discovered_servers_) known |= server.toMap().value(QStringLiteral("id")) == id;
      if (known) continue;
      // The server reports its own idea of its address, which may be on a
      // network this box can't reach (it answered over a VPN, say). The
      // address the reply came from is the fallback.
      QUrl reachable(address);
      QHostAddress sender = datagram.senderAddress();
      if (sender.protocol() == QAbstractSocket::IPv6Protocol) {
        bool ok = false;
        const QHostAddress v4(sender.toIPv4Address(&ok));
        if (ok) sender = v4;
      }
      reachable.setHost(sender.toString());
      discovered_servers_.append(QVariantMap{{QStringLiteral("id"), id},
                                             {QStringLiteral("name"), reply.value(QStringLiteral("Name")).toString()},
                                             {QStringLiteral("address"), address},
                                             {QStringLiteral("fallback"), reachable.toString()}});
      qCInfo(lcSession).noquote() << "discovered" << reply.value(QStringLiteral("Name")).toString() << address;
      emit discoveredServersChanged();
    }
  });
  // Broadcast on every interface: the box may reach the server over a VPN
  // (ZeroTier) as well as the LAN.
  const QByteArray probe("who is JellyfinServer?");
  QList<QHostAddress> targets{QHostAddress::Broadcast};
  for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
    if (!(interface.flags() & QNetworkInterface::IsUp) || (interface.flags() & QNetworkInterface::IsLoopBack)) continue;
    for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
        targets.append(entry.broadcast());
      }
    }
  }
  for (const QHostAddress& target : targets) discovery_socket_->writeDatagram(probe, target, kDiscoveryPort);
  emit discoveringChanged();
  QTimer::singleShot(kDiscoveryWindowMs, this, [this]() {
    if (discovery_socket_ == nullptr) return;
    discovery_socket_->deleteLater();
    discovery_socket_ = nullptr;
    emit discoveringChanged();
  });
}

void Session::connectToServer(const QString& address) { connectToServerTask(address); }

QCoro::Task<> Session::connectToServerTask(QString address) {
  QPointer<Session> self(this);
  setBusy(true);
  setError(QString());
  // "address|fallback" from discovery: both are tried, in order.
  QList<QUrl> candidates;
  for (const QString& part : address.split(QLatin1Char('|'), Qt::SkipEmptyParts)) candidates += Candidates(part);
  address = address.section(QLatin1Char('|'), 0, 0);
  for (const QUrl& candidate : candidates) {
    const Reply reply = co_await api_->getAt(candidate, QStringLiteral("/System/Info/Public"), kProbeTimeoutMs);
    if (!self) co_return;
    const QJsonObject info = reply.object();
    if (!reply.ok() || info.value(QStringLiteral("Id")).toString().isEmpty()) continue;

    // A different server means a different user and libraries.
    if (info.value(QStringLiteral("Id")).toString() != server_id_) {
      api_->setToken(QString());
      user_id_.clear();
      user_name_.clear();
      libraries_.clear();
      libraries_chosen_ = false;
      emit userChanged();
      emit librariesChanged();
    }
    api_->setBaseUrl(candidate);
    server_id_ = info.value(QStringLiteral("Id")).toString();
    server_name_ = info.value(QStringLiteral("ServerName")).toString();
    server_version_ = info.value(QStringLiteral("Version")).toString();
    qCInfo(lcSession).noquote() << "server" << server_name_ << server_version_ << "at" << candidate.toString();
    emit serverChanged();
    const Reply enabled = co_await api_->get(QStringLiteral("/QuickConnect/Enabled"));
    if (!self) co_return;
    quick_connect_available_ = enabled.ok() && enabled.json.toVariant().toBool();
    if (!quick_connect_available_ && enabled.ok()) {
      // Some versions answer a bare JSON bool, which QJsonDocument rejects.
      quick_connect_available_ = enabled.status == 200;
    }
    emit quickConnectChanged();
    save();
    setState(api_->token().isEmpty() ? State::SignedOut : State::SignedIn);
    setBusy(false);
    co_return;
  }
  setError(tr("No Jellyfin server answered at %1.").arg(address));
  setBusy(false);
}

void Session::forgetServer() {
  cancelQuickConnect();
  api_->setToken(QString());
  api_->setBaseUrl(QUrl());
  server_id_.clear();
  server_name_.clear();
  server_version_.clear();
  user_id_.clear();
  user_name_.clear();
  libraries_.clear();
  libraries_chosen_ = false;
  save();
  emit serverChanged();
  emit userChanged();
  emit librariesChanged();
  setState(State::NoServer);
}

void Session::signIn(const QString& user, const QString& password) { signInTask(user, password); }

QCoro::Task<> Session::signInTask(QString user, QString password) {
  QPointer<Session> self(this);
  setBusy(true);
  setError(QString());
  const Reply reply = co_await api_->post(
      QStringLiteral("/Users/AuthenticateByName"),
      QJsonDocument(QJsonObject{{QStringLiteral("Username"), user}, {QStringLiteral("Pw"), password}}));
  if (!self) co_return;
  setBusy(false);
  if (!reply.ok()) {
    setError(reply.status == 401 ? tr("Wrong user name or password.") : tr("Sign-in failed: %1").arg(reply.error));
    co_return;
  }
  finishSignIn(reply.object());
}

void Session::startQuickConnect() { quickConnectTask(); }

void Session::cancelQuickConnect() {
  ++quick_connect_generation_;
  if (!quick_connect_code_.isEmpty()) {
    quick_connect_code_.clear();
    emit quickConnectChanged();
  }
}

QCoro::Task<> Session::quickConnectTask() {
  QPointer<Session> self(this);
  const quint64 generation = ++quick_connect_generation_;
  setError(QString());
  Reply initiate = co_await api_->post(QStringLiteral("/QuickConnect/Initiate"));
  if (!self || generation != quick_connect_generation_) co_return;
  if (initiate.status == 405) {
    // Older servers use GET.
    initiate = co_await api_->get(QStringLiteral("/QuickConnect/Initiate"));
    if (!self || generation != quick_connect_generation_) co_return;
  }
  if (!initiate.ok()) {
    setError(tr("Quick Connect is not available on this server."));
    co_return;
  }
  const QString secret = initiate.object().value(QStringLiteral("Secret")).toString();
  quick_connect_code_ = initiate.object().value(QStringLiteral("Code")).toString();
  emit quickConnectChanged();

  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < kQuickConnectLifetimeMs) {
    co_await QCoro::sleepFor(std::chrono::milliseconds(kQuickConnectPollMs));
    if (!self || generation != quick_connect_generation_) co_return;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("secret"), secret);
    const Reply state = co_await api_->get(QStringLiteral("/QuickConnect/Connect"), query);
    if (!self || generation != quick_connect_generation_) co_return;
    if (state.status == 404) break;  // the request expired on the server
    if (!state.object().value(QStringLiteral("Authenticated")).toBool()) continue;

    setBusy(true);
    const Reply auth = co_await api_->post(QStringLiteral("/Users/AuthenticateWithQuickConnect"),
                                           QJsonDocument(QJsonObject{{QStringLiteral("Secret"), secret}}));
    if (!self || generation != quick_connect_generation_) co_return;
    setBusy(false);
    quick_connect_code_.clear();
    emit quickConnectChanged();
    if (!auth.ok()) {
      setError(tr("Quick Connect sign-in failed: %1").arg(auth.error));
      co_return;
    }
    finishSignIn(auth.object());
    co_return;
  }
  quick_connect_code_.clear();
  emit quickConnectChanged();
  setError(tr("The Quick Connect code expired. Try again."));
}

void Session::finishSignIn(const QJsonObject& authentication) {
  const QJsonObject user = authentication.value(QStringLiteral("User")).toObject();
  api_->setToken(authentication.value(QStringLiteral("AccessToken")).toString());
  user_id_ = user.value(QStringLiteral("Id")).toString();
  user_name_ = user.value(QStringLiteral("Name")).toString();
  qCInfo(lcSession).noquote() << "signed in as" << user_name_;
  save();
  emit userChanged();
  setState(State::SignedIn);
  refreshLibrariesTask();
}

void Session::signOut() {
  if (!api_->token().isEmpty()) api_->post(QStringLiteral("/Sessions/Logout"));
  cancelQuickConnect();
  api_->setToken(QString());
  user_id_.clear();
  user_name_.clear();
  libraries_.clear();
  libraries_chosen_ = false;
  save();
  emit userChanged();
  emit librariesChanged();
  setState(State::SignedOut);
}

QCoro::Task<> Session::validateSessionTask() {
  QPointer<Session> self(this);
  const Reply reply = co_await api_->get(QStringLiteral("/Users/Me"));
  if (!self) co_return;
  if (reply.status == 401 || reply.status == 403) {
    qCWarning(lcSession) << "the server refused the saved token";
    api_->setToken(QString());
    save();
    setState(State::SignedOut);
    emit signedOutByServer();
    co_return;
  }
  if (reply.ok()) {
    const QString name = reply.object().value(QStringLiteral("Name")).toString();
    if (!name.isEmpty() && name != user_name_) {
      user_name_ = name;
      emit userChanged();
    }
  }
}

void Session::refreshLibraries() { refreshLibrariesTask(); }

QCoro::Task<> Session::refreshLibrariesTask() {
  QPointer<Session> self(this);
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), user_id_);
  const Reply reply = co_await api_->get(QStringLiteral("/UserViews"), query);
  if (!self) co_return;
  if (!reply.ok()) co_return;
  QStringList hidden;
  for (const Library& library : libraries_) {
    if (!library.shown) hidden.append(library.id);
  }
  if (libraries_.isEmpty()) hidden = hidden_library_ids_;
  // Keep the user's order; libraries new to us go at the end, in server order.
  QStringList order;
  for (const Library& library : libraries_) order.append(library.id);
  QList<Library> fresh;
  for (const QJsonValue& value : reply.object().value(QStringLiteral("Items")).toArray()) {
    const QJsonObject view = value.toObject();
    const QString type = view.value(QStringLiteral("CollectionType")).toString();
    if (!IsSupportedCollection(type)) continue;
    const QString id = view.value(QStringLiteral("Id")).toString();
    fresh.append({id, view.value(QStringLiteral("Name")).toString(), type, !hidden.contains(id)});
  }
  std::stable_sort(fresh.begin(), fresh.end(), [&order](const Library& a, const Library& b) {
    const qsizetype ia = order.indexOf(a.id);
    const qsizetype ib = order.indexOf(b.id);
    return (ia < 0 ? order.size() : ia) < (ib < 0 ? order.size() : ib);
  });
  libraries_ = fresh;
  save();
  emit librariesChanged();
}

void Session::setLibraryShown(const QString& id, bool shown) {
  for (Library& library : libraries_) {
    if (library.id == id && library.shown != shown) {
      library.shown = shown;
      emit librariesChanged();
    }
  }
}

void Session::moveLibrary(const QString& id, int delta) {
  for (qsizetype i = 0; i < libraries_.size(); ++i) {
    if (libraries_[i].id != id) continue;
    const qsizetype target = i + delta;
    if (target < 0 || target >= libraries_.size()) return;
    libraries_.move(i, target);
    save();
    emit librariesChanged();
    return;
  }
}

void Session::confirmLibraries() {
  libraries_chosen_ = true;
  save();
  emit librariesChanged();
}

}  // namespace ember::jellyfin
