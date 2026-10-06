// SPDX-License-Identifier: GPL-3.0-only

#include <QKeyEvent>
#include <QTest>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include "app/RemoteKeys.h"

using ember::RemoteKeys;

namespace {

// Records what reaches a window after the filter.
class KeyWindow : public QWindow {
 public:
  QList<QPair<QEvent::Type, int>> events;

 protected:
  void keyPressEvent(QKeyEvent* event) override { events.append({QEvent::KeyPress, event->key()}); }
  void keyReleaseEvent(QKeyEvent* event) override { events.append({QEvent::KeyRelease, event->key()}); }
};

QKeyEvent Raw(int key, quint32 scan_code, quint32 keysym) {
  return QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier, scan_code, keysym, 0);
}

}  // namespace

// Key codes as seen on a couchbox box (BRIX, KWin 6.7, Qt 6.11) after
// fire-blaster's remapping; see PLAN.md, "Remote keys".
class TestRemoteKeys : public QObject {
  Q_OBJECT

 private slots:
  void normalize_data() {
    QTest::addColumn<int>("key");
    QTest::addColumn<quint32>("scan");
    QTest::addColumn<quint32>("keysym");
    QTest::addColumn<int>("expected");
    QTest::newRow("Return") << int(Qt::Key_Return) << 36u << 0xff0du << int(Qt::Key_Return);
    QTest::newRow("keypad Enter") << int(Qt::Key_Enter) << 104u << 0xff8du << int(Qt::Key_Return);
    QTest::newRow("Select") << int(Qt::Key_Select) << 361u << 0x1008ffa0u << int(Qt::Key_Return);
    QTest::newRow("Escape") << int(Qt::Key_Escape) << 9u << 0xff1bu << int(Qt::Key_Back);
    QTest::newRow("Back") << int(Qt::Key_Back) << 166u << 0x1008ff26u << int(Qt::Key_Back);
    QTest::newRow("Info") << 0 << 366u << 0x10081166u << int(Qt::Key_Info);
    QTest::newRow("Channel up") << 0 << 410u << 0x10081192u << int(Qt::Key_ChannelUp);
    QTest::newRow("Channel down") << 0 << 411u << 0x10081193u << int(Qt::Key_ChannelDown);
    QTest::newRow("EPG") << 0 << 373u << 0x1008116au << int(Qt::Key_Guide);
    QTest::newRow("context menu") << 0 << 446u << 0x100811b6u << int(Qt::Key_Menu);
    QTest::newRow("physical Play/Pause") << int(Qt::Key_MediaPlay) << 172u << 0x1008ff14u
                                         << int(Qt::Key_MediaTogglePlayPause);
    QTest::newRow("Play") << int(Qt::Key_MediaPlay) << 215u << 0x1008ff14u << int(Qt::Key_MediaPlay);
    QTest::newRow("keyboard Pause") << int(Qt::Key_Pause) << 127u << 0xff13u << int(Qt::Key_MediaPause);
    QTest::newRow("numeric 1") << int(Qt::Key_1) << 521u << 0x10081201u << int(Qt::Key_1);
    QTest::newRow("Red") << int(Qt::Key_Red) << 406u << 0x1008ffa3u << int(Qt::Key_Red);
    QTest::newRow("unknown stays") << 0 << 500u << 0x100811f0u << 0;
  }

  void normalize() {
    QFETCH(int, key);
    QFETCH(quint32, scan);
    QFETCH(quint32, keysym);
    QFETCH(int, expected);
    const QKeyEvent event = Raw(key, scan, keysym);
    QCOMPARE(RemoteKeys::Normalize(&event), expected);
  }

  void heldOk() {
    RemoteKeys filter;
    filter.setHoldMilliseconds(300);
    qApp->installEventFilter(&filter);
    KeyWindow window;
    window.show();

    // A tap arrives as Return on release.
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(&window, QEvent::KeyPress,
                                                                                         Qt::Key_Return, Qt::NoModifier);
    QVERIFY(window.events.isEmpty());
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(&window, QEvent::KeyRelease,
                                                                                         Qt::Key_Return, Qt::NoModifier);
    QCOMPARE(window.events.size(), 2);
    QCOMPARE(window.events.at(0), qMakePair(QEvent::KeyPress, int(Qt::Key_Return)));
    window.events.clear();

    // A hold becomes the context-menu key, and the release is swallowed.
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(&window, QEvent::KeyPress,
                                                                                         Qt::Key_Return, Qt::NoModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.events.size(), 2, 1000);
    QCOMPARE(window.events.at(0), qMakePair(QEvent::KeyPress, int(Qt::Key_Menu)));
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(&window, QEvent::KeyRelease,
                                                                                         Qt::Key_Return, Qt::NoModifier);
    QCOMPARE(window.events.size(), 2);

    // Other keys pass straight through.
    window.events.clear();
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(&window, QEvent::KeyPress,
                                                                                         Qt::Key_Down, Qt::NoModifier);
    QCOMPARE(window.events.size(), 1);
    QCOMPARE(window.events.at(0), qMakePair(QEvent::KeyPress, int(Qt::Key_Down)));
    qApp->removeEventFilter(&filter);
  }
};

QTEST_MAIN(TestRemoteKeys)
#include "tst_remotekeys.moc"
