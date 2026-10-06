// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <QCoroTask>

class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;
class QUdpSocket;

namespace ember::jellyfin {

class ApiClient;
class EventSocket;

// The server in use and its signed-in user, the libraries the user chose to
// show, and every other server signed in to on this box (one user each), so
// switching between them needs no new sign-in. Persisted in
// ~/.local/state/ember/session (mode 0600; it holds the tokens).
class Session : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverChanged)
  Q_PROPERTY(QString serverName READ serverName NOTIFY serverChanged)
  Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverChanged)
  Q_PROPERTY(QString userId READ userId NOTIFY userChanged)
  Q_PROPERTY(QString userName READ userName NOTIFY userChanged)
  Q_PROPERTY(QVariantList discoveredServers READ discoveredServers NOTIFY discoveredServersChanged)
  Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)
  Q_PROPERTY(bool quickConnectAvailable READ quickConnectAvailable NOTIFY quickConnectChanged)
  Q_PROPERTY(QString quickConnectCode READ quickConnectCode NOTIFY quickConnectChanged)
  // Every library (user view) on the server: {id, name, collectionType, shown}.
  Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged)
  // Only the shown ones, in server order.
  Q_PROPERTY(QVariantList shownLibraries READ shownLibraries NOTIFY librariesChanged)
  // False until the user has confirmed the library picker once.
  Q_PROPERTY(bool librariesChosen READ librariesChosen NOTIFY librariesChanged)
  // The saved servers, the one in use included, in the order they were
  // added: {serverId, name, userName, url, active, signedIn}.
  Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
  // After addServer(): the server picker can go back to the server in use.
  Q_PROPERTY(bool canCancelAddServer READ canCancelAddServer NOTIFY accountsChanged)

 public:
  enum class State {
    NoServer,    // nothing chosen yet
    SignedOut,   // a server is chosen, nobody is signed in
    SignedIn,
  };
  Q_ENUM(State)

  // No default argument on purpose: main() creates the one Session, and QML
  // must get it through create(). With a default constructor, QML built a
  // second one.
  explicit Session(QObject* parent);
  ~Session() override;

  static Session* instance();
  static Session* create(QQmlEngine*, QJSEngine*);

  ApiClient* api() const { return api_; }
  QNetworkAccessManager* network() const { return network_; }

  State state() const { return state_; }
  bool busy() const { return busy_; }
  QString errorString() const { return error_string_; }
  QString serverUrl() const;
  QString serverName() const { return server_name_; }
  QString serverVersion() const { return server_version_; }
  QString userId() const { return user_id_; }
  QString userName() const { return user_name_; }
  QVariantList discoveredServers() const { return discovered_servers_; }
  bool discovering() const { return discovery_socket_ != nullptr; }
  bool quickConnectAvailable() const { return quick_connect_available_; }
  QString quickConnectCode() const { return quick_connect_code_; }
  QVariantList libraries() const;
  QVariantList shownLibraries() const;
  bool librariesChosen() const { return libraries_chosen_; }
  QVariantList accounts() const;
  bool canCancelAddServer() const { return state_ == State::NoServer && !previous_server_id_.isEmpty(); }

  // Looks for servers on the local networks (UDP 7359 broadcast).
  Q_INVOKABLE void discover();
  // Accepts "host", "host:port" or a full URL; tries the usual schemes and
  // ports for a bare host.
  Q_INVOKABLE void connectToServer(const QString& address);
  // Drops the server in use (and its sign-in) and goes to the server picker.
  Q_INVOKABLE void forgetServer();
  // Makes a saved server the one in use, signed in as it was.
  Q_INVOKABLE void switchAccount(const QString& server_id);
  // Goes to the server picker to add another server; the saved ones stay.
  Q_INVOKABLE void addServer();
  // Back from the picker to the server in use before addServer().
  Q_INVOKABLE void cancelAddServer();
  Q_INVOKABLE void signIn(const QString& user, const QString& password);
  Q_INVOKABLE void startQuickConnect();
  Q_INVOKABLE void cancelQuickConnect();
  Q_INVOKABLE void signOut();
  Q_INVOKABLE void refreshLibraries();
  Q_INVOKABLE void setLibraryShown(const QString& id, bool shown);
  // Moves a library up (-1) or down (+1) in the home menu.
  Q_INVOKABLE void moveLibrary(const QString& id, int delta);
  // Saves the picker's choice.
  Q_INVOKABLE void confirmLibraries();
  Q_INVOKABLE void clearError() { setError(QString()); }

 signals:
  void stateChanged();
  void busyChanged();
  void errorStringChanged();
  void serverChanged();
  void userChanged();
  void discoveredServersChanged();
  void discoveringChanged();
  void quickConnectChanged();
  void librariesChanged();
  void accountsChanged();
  // Another saved server is now in use, with the same state as the last
  // one (stateChanged covers the rest): the pages should start over.
  void accountSwitched();
  // The token was refused; the user has to sign in again.
  void signedOutByServer();
  // From the server's event stream.
  void userDataChanged(const QString& itemId);
  void libraryChanged();
  // Remote control from another Jellyfin client ("Play On").
  void remotePlay(const QStringList& itemIds, double startSeconds, int startIndex, const QString& command);
  void remotePlaystate(const QString& command, double seekSeconds);
  void remoteCommand(const QString& name, const QVariantMap& arguments);

 private:
  struct Library {
    QString id;
    QString name;
    QString collection_type;
    bool shown = true;
  };

  // One saved server and its user, as stored.
  struct Account {
    QString server_id;
    QString url;
    QString name;
    QString version;
    QString token;
    QString user_id;
    QString user_name;
    bool libraries_chosen = false;
    QStringList hidden_libraries;
    QStringList library_cache;  // "id\tname\ttype" per library, in menu order
  };

  QCoro::Task<> connectToServerTask(QString address);
  QCoro::Task<> signInTask(QString user, QString password);
  QCoro::Task<> quickConnectTask();
  QCoro::Task<> refreshLibrariesTask();
  QCoro::Task<> validateSessionTask();
  void finishSignIn(const QJsonObject& authentication);
  void setState(State state);
  void setBusy(bool busy);
  void setError(const QString& error);
  void load();
  void save();
  Account currentAccount() const;
  void applyAccount(const Account& account);
  void clearCurrent();
  QString statePath() const;

  QNetworkAccessManager* network_;
  ApiClient* api_;
  EventSocket* events_ = nullptr;
  State state_ = State::NoServer;
  bool busy_ = false;
  QString error_string_;
  QString server_id_;
  QString server_name_;
  QString server_version_;
  QString user_id_;
  QString user_name_;
  QVariantList discovered_servers_;
  QUdpSocket* discovery_socket_ = nullptr;
  bool quick_connect_available_ = false;
  QString quick_connect_code_;
  quint64 quick_connect_generation_ = 0;
  QList<Library> libraries_;
  QStringList hidden_library_ids_;
  bool libraries_chosen_ = false;
  // Every saved server, the one in use included (kept current by save()).
  QList<Account> accounts_;
  // The server in use when addServer() went to the picker.
  QString previous_server_id_;
  // Bumped whenever another server comes into use, so replies meant for the
  // last one are dropped.
  quint64 account_generation_ = 0;
};

}  // namespace ember::jellyfin
