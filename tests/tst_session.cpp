// SPDX-License-Identifier: GPL-3.0-only

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "jellyfin/Session.h"

using ember::jellyfin::Session;

// The saved servers in ~/.local/state/ember/session. The servers point at a
// closed local port, so the checks Session starts in the background fail
// at once and change nothing.
class TestSession : public QObject {
  Q_OBJECT

 private:
  QString path() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/ember/session");
  }

  void writeOldLayout() {
    QDir().mkpath(QFileInfo(path()).absolutePath());
    KConfig config(path(), KConfig::SimpleConfig);
    config.group(QStringLiteral("Device")).writeEntry("Id", "device-1");
    KConfigGroup server = config.group(QStringLiteral("Server"));
    server.writeEntry("Url", "http://127.0.0.1:9");
    server.writeEntry("Id", "server-a");
    server.writeEntry("Name", "Home");
    KConfigGroup user = config.group(QStringLiteral("User"));
    user.writeEntry("Token", "token-a");
    user.writeEntry("Id", "user-a");
    user.writeEntry("Name", "jared");
    KConfigGroup libraries = config.group(QStringLiteral("Libraries"));
    libraries.writeEntry("Chosen", true);
    libraries.writeEntry("Hidden", QStringList{"lib-2"});
    libraries.writeEntry("Cache", QStringList{"lib-1\tShows\ttvshows", "lib-2\tMovies\tmovies"});
    config.sync();
  }

  void addSecondServer() {
    KConfig config(path(), KConfig::SimpleConfig);
    KConfigGroup accounts = config.group(QStringLiteral("Accounts"));
    accounts.writeEntry("Order", QStringList{"server-a", "server-b"});
    KConfigGroup b = config.group(QStringLiteral("Account server-b"));
    b.writeEntry("Url", "http://127.0.0.1:9/b");
    b.writeEntry("Name", "Test");
    b.writeEntry("Token", "token-b");
    b.writeEntry("UserName", "ember");
    b.writeEntry("LibrariesChosen", true);
    b.writeEntry("LibraryCache", QStringList{"lib-9\tFilms\tmovies"});
    config.sync();
  }

 private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
  void init() { QFile::remove(path()); }

  void migratesTheOldLayout() {
    writeOldLayout();
    {
      Session session(nullptr);
      QCOMPARE(session.state(), Session::State::SignedIn);
      QCOMPARE(session.serverName(), QStringLiteral("Home"));
      QCOMPARE(session.userName(), QStringLiteral("jared"));
      QCOMPARE(session.shownLibraries().size(), 1);
      QCOMPARE(session.accounts().size(), 1);
      session.addServer();  // saves
      session.cancelAddServer();
    }
    KConfig config(path(), KConfig::SimpleConfig);
    QVERIFY(!config.hasGroup(QStringLiteral("Server")));
    QVERIFY(!config.hasGroup(QStringLiteral("User")));
    const KConfigGroup a = config.group(QStringLiteral("Account server-a"));
    QCOMPARE(a.readEntry("Token", QString()), QStringLiteral("token-a"));
    QCOMPARE(a.readEntry("HiddenLibraries", QStringList()), QStringList{"lib-2"});
    QCOMPARE(config.group(QStringLiteral("Accounts")).readEntry("Active", QString()), QStringLiteral("server-a"));
  }

  void switchesBetweenServers() {
    writeOldLayout();
    { Session(nullptr).addServer(); }  // into the new layout
    addSecondServer();
    {
      Session session(nullptr);
      QSignalSpy switched(&session, &Session::accountSwitched);
      QCOMPARE(session.accounts().size(), 2);
      session.switchAccount(QStringLiteral("server-b"));
      QCOMPARE(switched.count(), 1);
      QCOMPARE(session.state(), Session::State::SignedIn);
      QCOMPARE(session.serverName(), QStringLiteral("Test"));
      QCOMPARE(session.userName(), QStringLiteral("ember"));
      QCOMPARE(session.shownLibraries().first().toMap().value("name").toString(), QStringLiteral("Films"));
      // The first server keeps its sign-in.
      session.switchAccount(QStringLiteral("server-a"));
      QCOMPARE(session.userName(), QStringLiteral("jared"));
      QCOMPARE(session.shownLibraries().first().toMap().value("name").toString(), QStringLiteral("Shows"));
    }
    // The last server used is the one in use after a restart.
    Session session(nullptr);
    QCOMPARE(session.serverName(), QStringLiteral("Home"));
  }

  void addingAServerCanBeCancelled() {
    writeOldLayout();
    Session session(nullptr);
    session.addServer();
    QCOMPARE(session.state(), Session::State::NoServer);
    QVERIFY(session.canCancelAddServer());
    QCOMPARE(session.accounts().size(), 1);
    session.cancelAddServer();
    QCOMPARE(session.state(), Session::State::SignedIn);
    QCOMPARE(session.serverName(), QStringLiteral("Home"));
  }

  void forgettingDropsOnlyThatServer() {
    writeOldLayout();
    { Session(nullptr).addServer(); }
    addSecondServer();
    Session session(nullptr);
    session.forgetServer();
    QCOMPARE(session.state(), Session::State::NoServer);
    QCOMPARE(session.accounts().size(), 1);
    QCOMPARE(session.accounts().first().toMap().value("serverId").toString(), QStringLiteral("server-b"));
    QVERIFY(session.canCancelAddServer());
  }
};

QTEST_MAIN(TestSession)
#include "tst_session.moc"
