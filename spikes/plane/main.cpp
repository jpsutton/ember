// SPDX-License-Identifier: GPL-3.0-only
//
// Plays one file through MpvVideo under a transparent window with a QML
// overlay. Usage: ember-plane-spike <file-or-url>
// EMBER_WINDOWED=1 opens a 1280x720 window instead of fullscreen.
// QSG_RENDER_LOOP picks Qt's render loop; this spike defaults to "basic".

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Ember_PlayerPlugin)

namespace {

// M0 spike 2: logs every key event as Qt reports it, to find out what each
// remote button looks like after fire-blaster's remapping.
class KeyLogger : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    // Each key event passes through several objects; log it at the window.
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && watched->isWindowType()) {
      auto* key = static_cast<QKeyEvent*>(event);
      if (!clock_.isValid()) clock_.start();
      qInfo("key %-7s t=%6lldms key=0x%08x scan=%u keysym=0x%x mods=0x%x repeat=%d text='%s'",
            event->type() == QEvent::KeyPress ? "press" : "release", clock_.elapsed(), key->key(),
            key->nativeScanCode(), key->nativeVirtualKey(), static_cast<unsigned>(key->modifiers()),
            key->isAutoRepeat(), qPrintable(key->text()));
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QElapsedTimer clock_;
};

}  // namespace

int main(int argc, char* argv[]) {
  // The basic loop renders and commits the window on the GUI thread, the
  // same thread that positions the video plane.
  if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "basic");
  // The video sits below the window and shows through its alpha channel.
  QQuickWindow::setDefaultAlphaBuffer(true);

  QGuiApplication app(argc, argv);
  QGuiApplication::setDesktopFileName(QStringLiteral("org.couchbox.ember"));
  KeyLogger key_logger;
  app.installEventFilter(&key_logger);

  const QStringList args = app.arguments();
  if (args.size() < 2) {
    qCritical("usage: %s <file-or-url>", qPrintable(args.value(0)));
    return 2;
  }
  const QString source = args.at(1);

  QQmlApplicationEngine engine;
  engine.setInitialProperties({
      {QStringLiteral("source"), source},
      {QStringLiteral("windowed"), qEnvironmentVariableIsSet("EMBER_WINDOWED")},
      {QStringLiteral("renderLoop"), QString::fromLocal8Bit(qgetenv("QSG_RENDER_LOOP"))},
  });
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
      Qt::QueuedConnection);
  engine.loadFromModule("EmberPlaneSpike", "Main");
  return app.exec();
}
