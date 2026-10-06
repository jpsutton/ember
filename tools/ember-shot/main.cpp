// SPDX-License-Identifier: GPL-3.0-only
//
// Development tool: saves a screenshot of the active screen through KWin's
// ScreenShot2 D-Bus interface, video planes included.
//
// KWin only answers programs whose desktop file lists the interface, so
// install ember-shot.desktop (Exec= this binary's absolute path) under
// ~/.local/share/applications and run kbuildsycoca6 once.
//
// Usage: ember-shot OUTPUT.png

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QImage>

#include <poll.h>
#include <unistd.h>

#include <cstdio>
#include <thread>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    std::fprintf(stderr, "usage: ember-shot OUTPUT.png\n");
    return 2;
  }

  int fds[2];
  if (pipe(fds) != 0) return 1;
  QByteArray data;
  std::thread reader([&data, fd = fds[0]]() {
    char buffer[65536];
    for (;;) {
      // Give up if KWin goes quiet, rather than hang.
      pollfd poll_fd{fd, POLLIN, 0};
      if (poll(&poll_fd, 1, 15000) <= 0) break;
      const ssize_t n = read(fd, buffer, sizeof buffer);
      if (n <= 0) break;
      data.append(buffer, n);
    }
    close(fd);
  });

  QDBusMessage reply;
  {
    // QDBusUnixFileDescriptor holds a duplicate of the write end; it has to
    // be gone before the reader can see end-of-file.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/ScreenShot2"),
        QStringLiteral("org.kde.KWin.ScreenShot2"), QStringLiteral("CaptureActiveScreen"));
    call << QVariantMap{{QStringLiteral("native-resolution"), true}}
         << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    close(fds[1]);
    reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 10000);
  }
  if (qEnvironmentVariableIsSet("EMBER_SHOT_DEBUG")) {
    std::fprintf(stderr, "reply type %d: %s\n", int(reply.type()), qPrintable(reply.errorMessage()));
  }
  reader.join();
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
    std::fprintf(stderr, "screenshot failed: %s\n", qPrintable(reply.errorMessage()));
    return 1;
  }

  const QVariantMap info = qdbus_cast<QVariantMap>(reply.arguments().first());
  const int width = info.value(QStringLiteral("width")).toInt();
  const int height = info.value(QStringLiteral("height")).toInt();
  const int stride = info.value(QStringLiteral("stride")).toInt();
  const auto format = static_cast<QImage::Format>(info.value(QStringLiteral("format")).toInt());
  if (width <= 0 || height <= 0 || data.size() < qsizetype(stride) * height) {
    std::fprintf(stderr, "screenshot data incomplete (%dx%d, %lld bytes)\n", width, height,
                 static_cast<long long>(data.size()));
    return 1;
  }
  const QImage image(reinterpret_cast<const uchar*>(data.constData()), width, height, stride, format);
  if (!image.save(QString::fromLocal8Bit(argv[1]))) return 1;
  return 0;
}
