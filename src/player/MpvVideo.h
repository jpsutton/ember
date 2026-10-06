// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QRect>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <vector>

class QQuickWindow;
class QScreen;

namespace ember::plane {
class MpvCore;
class PlaneRenderExecutor;
class WaylandVideoPlane;
}  // namespace ember::plane

// A QML item that shows mpv's video in its own area of the window.
//
// The item paints nothing. The video goes to a Wayland subsurface stacked
// below the window, placed under the item's rectangle, so the window must be
// transparent there (Window { color: "transparent" } and nothing opaque drawn
// over the item). QML drawn above the item shows over the video.
//
// The plane and the mpv core start after the window's first frame (its
// Wayland surface exists by then) and are torn down when the window hides,
// since Qt may give the window a new surface when it shows again.
class MpvVideo : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

 public:
  explicit MpvVideo(QQuickItem* parent = nullptr);
  ~MpvVideo() override;

  bool ready() const { return ready_; }
  QString errorString() const { return error_string_; }

  // Calls made before the item is ready are queued and run in order once it
  // is.
  Q_INVOKABLE void command(const QStringList& args);
  Q_INVOKABLE void setOption(const QString& name, const QVariant& value);
  // |format| is one of "string", "flag", "int64", "double", "node".
  Q_INVOKABLE void observe(const QString& name, const QString& format);

 signals:
  void readyChanged();
  void errorStringChanged();
  void mpvEvent(const QString& name, const QVariantMap& data);
  void mpvPropertyChanged(const QString& name, const QVariant& value);

 protected:
  void itemChange(ItemChange change, const ItemChangeData& value) override;

 private:
  void attachWindow(QQuickWindow* window);
  void start();
  void release();
  void updateRect();
  void applyRect();
  void render(bool force);
  void scheduleRenderRetry();
  void applyDisplayFps(QScreen* screen);
  void setError(const QString& error);
  void whenReady(std::function<void()> call);

  QPointer<QQuickWindow> window_;
  QList<QMetaObject::Connection> window_connections_;

  std::unique_ptr<ember::plane::MpvCore> core_;
  std::unique_ptr<ember::plane::WaylandVideoPlane> plane_;
  std::unique_ptr<ember::plane::PlaneRenderExecutor> executor_;
  std::vector<std::function<void()>> pending_calls_;

  bool ready_ = false;
  bool start_failed_ = false;
  QString error_string_;
  // Bumped on every teardown, so a render completion that outlives its plane
  // can tell.
  quint64 generation_ = 0;
  // A redraw is owed even if mpv has no new frame (after a resize, or after
  // becoming visible). Sticky until a present succeeds.
  bool needs_render_ = false;
  // A render job is between PreparePresent() and CompletePresent().
  bool render_in_flight_ = false;
  // A rect arrived during a render; applied at completion, because
  // wl_egl_window_resize must not race the swap.
  bool rect_apply_deferred_ = false;
  bool render_retry_pending_ = false;
  QRect rect_;
  qreal device_pixel_ratio_ = 1.0;
  qreal applied_display_fps_ = 0;
};
