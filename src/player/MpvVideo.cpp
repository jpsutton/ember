// SPDX-License-Identifier: GPL-3.0-only
// The render scheduling follows Plezy's linux/runner/mpv/mpv_plugin.cc
// (GPL-3.0).

#include "MpvVideo.h"

#include <EGL/egl.h>

#include <QLoggingCategory>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include "plane/MpvCore.h"
#include "plane/PlaneRenderExecutor.h"
#include "plane/WaylandVideoPlane.h"

Q_LOGGING_CATEGORY(lcVideo, "ember.video")

using ember::plane::MpvCore;
using ember::plane::PlaneRenderExecutor;
using ember::plane::WaylandVideoPlane;

namespace {

// Delay before retrying a render whose job failed, so a persistent failure
// doesn't spin the render worker.
constexpr int kRenderRetryDelayMs = 100;

std::vector<std::string> ToStdStrings(const QStringList& list) {
  std::vector<std::string> result;
  result.reserve(list.size());
  for (const QString& item : list) result.push_back(item.toStdString());
  return result;
}

}  // namespace

MpvVideo::MpvVideo(QQuickItem* parent) : QQuickItem(parent) {}

MpvVideo::~MpvVideo() { release(); }

void MpvVideo::itemChange(ItemChange change, const ItemChangeData& value) {
  QQuickItem::itemChange(change, value);
  switch (change) {
    case ItemSceneChange:
      attachWindow(value.window);
      break;
    case ItemVisibleHasChanged:
      if (plane_) {
        plane_->SetVisible(isVisible());
        if (isVisible()) render(true);
      }
      break;
    default:
      break;
  }
}

void MpvVideo::attachWindow(QQuickWindow* window) {
  if (window == window_) return;
  for (const auto& connection : window_connections_) disconnect(connection);
  window_connections_.clear();
  release();
  window_ = window;
  if (window == nullptr) return;

  // The first swapped frame means the window's Wayland surface exists and
  // has content. Emitted on the render thread under the threaded loop.
  window_connections_.append(connect(
      window, &QQuickWindow::frameSwapped, this,
      [this]() {
        if (!core_ && !start_failed_ && window_ && window_->isVisible()) start();
      },
      Qt::QueuedConnection));
  // Runs on the GUI thread once per frame, before rendering: the place to
  // notice that the item moved, including when an ancestor moved it.
  window_connections_.append(connect(window, &QQuickWindow::afterAnimating, this, &MpvVideo::updateRect));
  window_connections_.append(connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
    // Qt may give the window a new surface when it shows again; the plane
    // would be left under the old one.
    if (!visible) {
      release();
      start_failed_ = false;
    }
  }));
  window_connections_.append(connect(window, &QWindow::screenChanged, this, &MpvVideo::applyDisplayFps));
  window->update();
}

void MpvVideo::start() {
  if (!WaylandVideoPlane::IsSupported()) {
    start_failed_ = true;
    setError(QStringLiteral("Video needs a Wayland session."));
    return;
  }
  if (!window_->format().hasAlpha()) {
    start_failed_ = true;
    setError(QStringLiteral("The window has no alpha channel, so video under it can't be seen."));
    return;
  }

  auto core = std::make_unique<MpvCore>();
  if (!core->Initialize()) {
    start_failed_ = true;
    setError(QStringLiteral("mpv failed to start."));
    return;
  }
  auto plane = std::make_unique<WaylandVideoPlane>();
  QString error;
  QPointer<QQuickWindow> window = window_;
  if (!plane->Create(window_, [window]() {
        if (window) window->update();
      }, &error)) {
    start_failed_ = true;
    core->Dispose();
    setError(error);
    return;
  }
  if (!core->InitRenderContextForSurface(plane->egl_display(), plane->egl_config(), plane->egl_surface(),
                                         plane->display())) {
    start_failed_ = true;
    core->Dispose();
    plane->Destroy();
    setError(QStringLiteral("The GPU driver refused a render context for the video."));
    return;
  }

  ++generation_;
  core_ = std::move(core);
  plane_ = std::move(plane);
  executor_ = std::make_unique<PlaneRenderExecutor>();
  plane_->SetFrameCallback([this]() { render(false); });
  plane_->SetScreenEnteredCallback([this](QScreen* screen) { applyDisplayFps(screen); });
  core_->SetRedrawCallback([this]() { render(false); });
  core_->SetEventCallback([this](const QString& name, const QVariantMap& data) {
    if (name == QLatin1String("log-message")) {
      const QString level = data.value(QStringLiteral("level")).toString();
      const QString text = data.value(QStringLiteral("prefix")).toString() + QStringLiteral(": ") +
                           data.value(QStringLiteral("text")).toString();
      if (level == QLatin1String("error") || level == QLatin1String("fatal")) {
        qCWarning(lcVideo).noquote() << text;
      } else if (level == QLatin1String("warn")) {
        qCInfo(lcVideo).noquote() << text;
      } else {
        qCDebug(lcVideo).noquote() << text;
      }
    }
    emit mpvEvent(name, data);
  });
  core_->SetPropertyCallback(
      [this](const QString& name, const QVariant& value) { emit mpvPropertyChanged(name, value); });

  applyDisplayFps(window_->screen());
  rect_ = QRect();
  updateRect();
  plane_->SetVisible(isVisible());
  setError(QString());
  ready_ = true;
  emit readyChanged();

  auto calls = std::move(pending_calls_);
  pending_calls_.clear();
  for (auto& call : calls) call();
}

void MpvVideo::release() {
  ++generation_;
  pending_calls_.clear();
  if (executor_) {
    // Unbind the EGL context on the worker, the one thread it is current on,
    // so the teardown thread can bind it.
    const EGLDisplay display = plane_ ? plane_->egl_display() : EGL_NO_DISPLAY;
    executor_->Post(
        [display]() {
          if (display != EGL_NO_DISPLAY) eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
          return true;
        },
        nullptr);
    const bool drained = executor_->ShutdownAndJoin(5000);
    executor_.reset();
    if (!drained) {
      // A job is stuck in a driver call. Freeing the render context or the
      // surface under it would crash; leak this session instead.
      qCWarning(lcVideo, "render worker did not drain; leaking the player and plane");
      if (core_) {
        core_->SetRedrawCallback(nullptr);
        core_->SetEventCallback(nullptr);
        core_->SetPropertyCallback(nullptr);
        core_->Command({"stop"});
        (void)core_.release();
      }
      (void)plane_.release();
    }
  }
  if (core_) {
    core_->SetRedrawCallback(nullptr);
    core_->SetEventCallback(nullptr);
    core_->SetPropertyCallback(nullptr);
    core_->Dispose();
    core_.reset();
  }
  if (plane_) {
    plane_->Destroy();
    plane_.reset();
  }
  render_in_flight_ = false;
  rect_apply_deferred_ = false;
  needs_render_ = false;
  applied_display_fps_ = 0;
  if (ready_) {
    ready_ = false;
    emit readyChanged();
  }
}

void MpvVideo::updateRect() {
  if (!window_) return;
  const QRect rect = mapRectToScene(boundingRect()).toAlignedRect();
  const qreal dpr = window_->effectiveDevicePixelRatio();
  if (rect == rect_ && dpr == device_pixel_ratio_) return;
  rect_ = rect;
  device_pixel_ratio_ = dpr;
  if (!plane_) return;
  if (render_in_flight_) {
    rect_apply_deferred_ = true;
    needs_render_ = true;
    return;
  }
  applyRect();
  render(true);
}

void MpvVideo::applyRect() {
  if (plane_) plane_->SetRect(rect_, device_pixel_ratio_);
}

void MpvVideo::render(bool force) {
  if (force) needs_render_ = true;
  if (!core_ || !plane_ || !plane_->valid() || !executor_) return;
  // One job at a time; the completion runs this again.
  if (render_in_flight_) return;
  if (!plane_->visible() || !plane_->has_size()) return;
  // Never present an empty first frame. A compositor may ignore the first
  // commit of an occluded surface for good, and the frame callback armed for
  // it would then stall the plane.
  if (!plane_->first_frame_presented() && !core_->NeedsRedraw()) return;
  if (plane_->frame_pending()) return;
  // Frame callbacks come at the display's rate; only render when mpv has a
  // new frame or a redraw is owed.
  if (!needs_render_ && !core_->NeedsRedraw()) return;
  if (!plane_->PreparePresent()) return;

  MpvCore* core = core_.get();
  const EGLDisplay display = plane_->egl_display();
  const EGLSurface surface = plane_->egl_surface();
  const int width = plane_->buffer_width();
  const int height = plane_->buffer_height();
  const quint64 generation = generation_;
  render_in_flight_ = true;
  const bool posted = executor_->Post(
      [core, display, surface, width, height]() {
        if (!core->RenderToSurface(surface, width, height)) return false;
        // The swap is the plane's commit. Swap interval 0, so it never blocks
        // on the compositor.
        if (eglSwapBuffers(display, surface) != EGL_TRUE) {
          qCWarning(lcVideo, "eglSwapBuffers failed: 0x%x", eglGetError());
          return false;
        }
        return true;
      },
      [this, generation](bool swapped) {
        if (generation != generation_) return;
        render_in_flight_ = false;
        if (!plane_) return;
        if (plane_->CompletePresent(swapped)) needs_render_ = false;
        if (rect_apply_deferred_) {
          rect_apply_deferred_ = false;
          applyRect();
        }
        if (swapped) {
          render(false);
        } else {
          scheduleRenderRetry();
        }
      });
  if (!posted) {
    render_in_flight_ = false;
    plane_->CompletePresent(false);
  }
}

void MpvVideo::scheduleRenderRetry() {
  if (render_retry_pending_) return;
  render_retry_pending_ = true;
  QTimer::singleShot(kRenderRetryDelayMs, this, [this]() {
    render_retry_pending_ = false;
    render(false);
  });
}

void MpvVideo::applyDisplayFps(QScreen* screen) {
  // vo=libmpv has no window, so mpv can't learn the display's refresh rate
  // by itself; display-sync and the stats need it.
  if (!core_ || screen == nullptr) return;
  const qreal hz = screen->refreshRate();
  if (hz <= 0 || qFuzzyCompare(hz, applied_display_fps_)) return;
  applied_display_fps_ = hz;
  qCInfo(lcVideo, "display refresh rate %.3f Hz", hz);
  core_->SetPropertyAsync("display-fps-override", static_cast<double>(hz));
}

void MpvVideo::setError(const QString& error) {
  if (!error.isEmpty()) qCWarning(lcVideo).noquote() << "video unavailable:" << error;
  if (error == error_string_) return;
  error_string_ = error;
  emit errorStringChanged();
}

void MpvVideo::whenReady(std::function<void()> call) {
  if (ready_) {
    call();
  } else {
    pending_calls_.push_back(std::move(call));
  }
}

void MpvVideo::command(const QStringList& args) {
  whenReady([this, args = ToStdStrings(args)]() {
    if (core_) {
      core_->CommandAsync(args, [args](int error, const mpv_node*) {
        if (error < 0) {
          qCWarning(lcVideo, "command '%s' failed: %s", args.empty() ? "" : args.front().c_str(),
                    mpv_error_string(error));
        }
      });
    }
  });
}

void MpvVideo::setOption(const QString& name, const QVariant& value) {
  whenReady([this, name = name.toStdString(), value]() {
    if (!core_) return;
    switch (value.typeId()) {
      case QMetaType::Bool:
        core_->SetPropertyAsync(name, std::string(value.toBool() ? "yes" : "no"));
        break;
      case QMetaType::Int:
      case QMetaType::LongLong:
      case QMetaType::Double:
        core_->SetPropertyAsync(name, value.toDouble());
        break;
      default:
        core_->SetPropertyAsync(name, value.toString().toStdString());
        break;
    }
  });
}

void MpvVideo::observe(const QString& name, const QString& format) {
  whenReady([this, name = name.toStdString(), format = format.toStdString()]() {
    if (core_) core_->ObserveProperty(name, format);
  });
}
