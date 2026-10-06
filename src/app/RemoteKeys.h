// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class QKeyEvent;
class QWindow;

namespace ember {

// Normalizes key events from TV remotes before QML sees them, so QML only
// handles plain Qt keys:
//
//   Info, Channel +/-, Guide and the context-menu key arrive as Key_unknown
//   (no XKB name), and are recognized by their keysym (0x10081000 + evdev
//   code). Enter and Select become Return; Escape becomes Back. The physical
//   Play/Pause key arrives as Key_MediaPlay (XKB maps KEY_PLAYPAUSE to
//   XF86AudioPlay) and becomes Key_MediaTogglePlayPause.
//
// It also turns a held OK into the context-menu key: Return acts on release
// when released within the hold time, and a hold sends Key_Menu instead.
//
// EMBER_KEYS=1 logs every raw key event.
class RemoteKeys : public QObject {
  Q_OBJECT

 public:
  explicit RemoteKeys(QObject* parent = nullptr);

  void setHoldMilliseconds(int ms) { hold_timer_.setInterval(ms); }

  // The Qt key QML will see for |event|.
  static int Normalize(const QKeyEvent* event);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void send(QWindow* window, int key, const QString& text = {});

  QTimer hold_timer_;
  QPointer<QWindow> ok_window_;
  bool ok_down_ = false;
  bool ok_held_ = false;
  bool log_ = false;
};

}  // namespace ember
