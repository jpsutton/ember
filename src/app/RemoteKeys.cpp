// SPDX-License-Identifier: GPL-3.0-only

#include "RemoteKeys.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QWindow>

Q_LOGGING_CATEGORY(lcKeys, "ember.keys")

namespace ember {
namespace {

// XKB keysyms for evdev codes without a name: 0x10081000 + code.
constexpr quint32 kEvdevKeysymBase = 0x10081000;
constexpr quint32 kKeyInfo = 0x166;
constexpr quint32 kKeyProgram = 0x16a;
constexpr quint32 kKeyEpg = 0x16d;
constexpr quint32 kKeyChannelUp = 0x192;
constexpr quint32 kKeyChannelDown = 0x193;
constexpr quint32 kKeyContextMenu = 0x1b6;
// KEY_PLAYPAUSE (164) as an XKB keycode (evdev + 8).
constexpr quint32 kPlayPauseScanCode = 172;

constexpr int kDefaultHoldMs = 600;

}  // namespace

RemoteKeys::RemoteKeys(QObject* parent) : QObject(parent), log_(qEnvironmentVariableIsSet("EMBER_KEYS")) {
  hold_timer_.setSingleShot(true);
  hold_timer_.setInterval(kDefaultHoldMs);
  connect(&hold_timer_, &QTimer::timeout, this, [this]() {
    if (!ok_down_) return;
    ok_held_ = true;
    if (ok_window_) send(ok_window_, Qt::Key_Menu);
  });
}

int RemoteKeys::Normalize(const QKeyEvent* event) {
  const int key = event->key();
  if (key == 0 || key == Qt::Key_unknown) {
    const quint32 keysym = event->nativeVirtualKey();
    if (keysym > kEvdevKeysymBase) {
      switch (keysym - kEvdevKeysymBase) {
        case kKeyInfo: return Qt::Key_Info;
        case kKeyChannelUp: return Qt::Key_ChannelUp;
        case kKeyChannelDown: return Qt::Key_ChannelDown;
        case kKeyProgram:
        case kKeyEpg: return Qt::Key_Guide;
        case kKeyContextMenu: return Qt::Key_Menu;
        default: break;
      }
    }
    return key;
  }
  switch (key) {
    case Qt::Key_Enter:
    case Qt::Key_Select: return Qt::Key_Return;
    case Qt::Key_Escape: return Qt::Key_Back;
    case Qt::Key_MediaPlay:
      return event->nativeScanCode() == kPlayPauseScanCode ? Qt::Key_MediaTogglePlayPause : key;
    case Qt::Key_Pause: return Qt::Key_MediaPause;
    default: return key;
  }
}

void RemoteKeys::send(QWindow* window, int key, const QString& text) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
  QCoreApplication::sendEvent(window, &press);
  QCoreApplication::sendEvent(window, &release);
}

bool RemoteKeys::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) return false;
  // Only raw events at the window: our own (sent below) aren't spontaneous,
  // and each event also passes through every item on its way.
  if (!event->spontaneous() || !watched->isWindowType()) return false;
  auto* key_event = static_cast<QKeyEvent*>(event);
  auto* window = static_cast<QWindow*>(watched);
  const bool press = event->type() == QEvent::KeyPress;
  if (log_) {
    qCInfo(lcKeys, "%s key=0x%08x scan=%u keysym=0x%x repeat=%d", press ? "press" : "release", key_event->key(),
           key_event->nativeScanCode(), key_event->nativeVirtualKey(), key_event->isAutoRepeat());
  }

  const int key = Normalize(key_event);
  if (key == Qt::Key_Return && key_event->modifiers() == Qt::NoModifier) {
    // Held OK: the release decides between OK and the context menu.
    if (press) {
      if (!key_event->isAutoRepeat() && !ok_down_) {
        ok_down_ = true;
        ok_held_ = false;
        ok_window_ = window;
        hold_timer_.start();
      }
    } else if (!key_event->isAutoRepeat() && ok_down_) {
      ok_down_ = false;
      hold_timer_.stop();
      if (!ok_held_) send(window, Qt::Key_Return);
    }
    return true;
  }
  if (key == key_event->key()) return false;
  QKeyEvent replacement(event->type(), key, key_event->modifiers(), key_event->text(), key_event->isAutoRepeat());
  QCoreApplication::sendEvent(window, &replacement);
  return true;
}

}  // namespace ember
