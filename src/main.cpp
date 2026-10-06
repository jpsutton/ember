// SPDX-License-Identifier: GPL-3.0-only

#include <KDBusService>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include "EmberSettings.h"
#include "app/RemoteKeys.h"
#include "app/SystemIntegration.h"
#include "jellyfin/Session.h"

Q_IMPORT_QML_PLUGIN(EmberPlugin)
Q_IMPORT_QML_PLUGIN(Ember_PlayerPlugin)

int main(int argc, char* argv[]) {
  // The basic render loop commits the window on the GUI thread, the same
  // thread that positions the video plane under it.
  if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "basic");
  // The video plane sits below the window and shows through its alpha.
  QQuickWindow::setDefaultAlphaBuffer(true);

  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("ember"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("Ember"));
  QGuiApplication::setOrganizationDomain(QStringLiteral("couchbox.org"));
  QGuiApplication::setApplicationVersion(QStringLiteral(EMBER_VERSION));
  // Must equal the desktop file's name: Bigscreen raises a running app only
  // when the Wayland app ID matches the tile.
  QGuiApplication::setDesktopFileName(QStringLiteral("org.couchbox.ember"));

  // A second launch (another tile press) activates this one and exits.
  KDBusService service(KDBusService::Unique);

  ember::RemoteKeys remote_keys;
  remote_keys.setHoldMilliseconds(ember::EmberSettings::self()->holdMilliseconds());
  app.installEventFilter(&remote_keys);

  ember::jellyfin::Session session;
  ember::ImageNetworkFactory image_network;

  QQmlApplicationEngine engine;
  engine.setNetworkAccessManagerFactory(&image_network);
  engine.setInitialProperties({{QStringLiteral("windowed"), qEnvironmentVariableIsSet("EMBER_WINDOWED")}});
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
      Qt::QueuedConnection);
  engine.loadFromModule("Ember", "Main");

  QObject::connect(&service, &KDBusService::activateRequested, &engine, [&engine]() {
    for (QObject* object : engine.rootObjects()) {
      if (auto* window = qobject_cast<QWindow*>(object)) {
        window->show();
        window->raise();
        window->requestActivate();
      }
    }
  });

  return app.exec();
}
