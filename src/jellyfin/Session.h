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

// The signed-in server and user, and the libraries the user chose to show.
// Persisted in ~/.local/state/ember/session (mode 0600; it holds the token).
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

  // Looks for servers on the local networks (UDP 7359 broadcast).
  Q_INVOKABLE void discover();
  // Accepts "host", "host:port" or a full URL; tries the usual schemes and
  // ports for a bare host.
  Q_INVOKABLE void connectToServer(const QString& address);
  Q_INVOKABLE void forgetServer();
  Q_INVOKABLE void signIn(const QString& user, const QString& password);
  Q_INVOKABLE void startQuickConnect();
  Q_INVOKABLE void cancelQuickConnect();
  Q_INVOKABLE void signOut();
  Q_INVOKABLE void refreshLibraries();
  Q_INVOKABLE void setLibraryShown(const QString& id, bool shown);
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
  // The token was refused; the user has to sign in again.
  void signedOutByServer();
  // From the server's event stream.
  void userDataChanged(const QString& itemId);
  void libraryChanged();

 private:
  struct Library {
    QString id;
    QString name;
    QString collection_type;
    bool shown = true;
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
  void save() const;
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
};

}  // namespace ember::jellyfin
