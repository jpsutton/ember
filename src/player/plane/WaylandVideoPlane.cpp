// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Plezy's linux/runner/mpv/wayland_video_surface.cc (GPL-3.0).

#include "WaylandVideoPlane.h"

#include <wayland-client.h>
#include <wayland-egl.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <algorithm>
#include <limits>

#include "viewporter-client-protocol.h"

Q_LOGGING_CATEGORY(lcPlane, "ember.plane")

namespace ember::plane {
namespace {

bool Fail(QString* error, const char* message) {
  if (error) *error = QString::fromLatin1(message);
  return false;
}

// Highest wl_compositor version whose wl_surface events the listener below
// handles.
constexpr uint32_t kCompositorMaxVersion = 6;

struct RegistryTarget {
  wl_compositor* compositor = nullptr;
  wl_subcompositor* subcompositor = nullptr;
  wp_viewporter* viewporter = nullptr;
};

void RegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
  auto* target = static_cast<RegistryTarget*>(data);
  if (strcmp(interface, wl_compositor_interface.name) == 0 && target->compositor == nullptr) {
    target->compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, kCompositorMaxVersion)));
  } else if (strcmp(interface, wl_subcompositor_interface.name) == 0 && target->subcompositor == nullptr) {
    target->subcompositor =
        static_cast<wl_subcompositor*>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
  } else if (strcmp(interface, wp_viewporter_interface.name) == 0 && target->viewporter == nullptr) {
    target->viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
  }
}

void RegistryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {RegistryGlobal, RegistryGlobalRemove};

}  // namespace

WaylandVideoPlane::WaylandVideoPlane() = default;

WaylandVideoPlane::~WaylandVideoPlane() { Destroy(); }

bool WaylandVideoPlane::IsSupported() {
  return qGuiApp != nullptr && qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() != nullptr;
}

bool WaylandVideoPlane::BindGlobals(QString* error) {
  // The globals are bound on a private queue through a display wrapper, so
  // the roundtrip can't dispatch Qt's own events, and then moved to the
  // default queue, which Qt dispatches on the GUI thread.
  wl_event_queue* queue = wl_display_create_queue(wl_display_);
  if (queue == nullptr) return Fail(error, "could not create a Wayland event queue");
  auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(wl_display_));
  if (wrapper == nullptr) {
    wl_event_queue_destroy(queue);
    return Fail(error, "could not wrap the Wayland display");
  }
  wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
  wl_registry* registry = wl_display_get_registry(wrapper);
  wl_proxy_wrapper_destroy(wrapper);
  if (registry == nullptr) {
    wl_event_queue_destroy(queue);
    return Fail(error, "could not get the Wayland registry");
  }

  RegistryTarget target;
  wl_registry_add_listener(registry, &kRegistryListener, &target);
  const bool round_tripped = wl_display_roundtrip_queue(wl_display_, queue) >= 0;
  wl_registry_destroy(registry);

  // Off the private queue before it goes: a proxy whose queue was destroyed
  // crashes the next time anything is dispatched for it.
  for (void* proxy : {static_cast<void*>(target.compositor), static_cast<void*>(target.subcompositor),
                      static_cast<void*>(target.viewporter)}) {
    if (proxy != nullptr) wl_proxy_set_queue(static_cast<wl_proxy*>(proxy), nullptr);
  }
  wl_event_queue_destroy(queue);

  compositor_ = target.compositor;
  subcompositor_ = target.subcompositor;
  viewporter_ = target.viewporter;
  if (!round_tripped) return Fail(error, "Wayland roundtrip failed while binding globals");
  if (compositor_ == nullptr) return Fail(error, "compositor has no wl_compositor");
  if (subcompositor_ == nullptr) return Fail(error, "compositor has no wl_subcompositor");
  if (viewporter_ == nullptr) return Fail(error, "compositor has no wp_viewporter");
  return true;
}

bool WaylandVideoPlane::InitEgl(QString* error) {
  // Shares the EGLDisplay with Qt (one per wl_display), so it is initialized
  // again but never terminated.
  egl_display_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(wl_display_));
  if (egl_display_ == EGL_NO_DISPLAY) return Fail(error, "no EGL display for the Wayland connection");
  if (!eglInitialize(egl_display_, nullptr, nullptr)) {
    egl_display_ = EGL_NO_DISPLAY;
    return Fail(error, "eglInitialize failed for the video plane");
  }
  // Video is opaque: no alpha channel.
  for (const EGLint renderable : {EGL_OPENGL_ES3_BIT, EGL_OPENGL_ES2_BIT}) {
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, renderable, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,    8,              EGL_ALPHA_SIZE,      0,          EGL_NONE,
    };
    EGLint count = 0;
    if (eglChooseConfig(egl_display_, attributes, &egl_config_, 1, &count) && count == 1) return true;
  }
  return Fail(error, "no matching EGL config for the video plane");
}

bool WaylandVideoPlane::Create(QWindow* window, std::function<void()> request_parent_commit, QString* error) {
  if (window == nullptr) return Fail(error, "the video plane needs a window");
  auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  if (wayland == nullptr) return Fail(error, "not a Wayland session");
  QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
  auto* parent = native ? static_cast<wl_surface*>(native->nativeResourceForWindow("surface", window)) : nullptr;
  if (parent == nullptr) return Fail(error, "the window has no Wayland surface yet");

  window_ = window;
  request_parent_commit_ = std::move(request_parent_commit);
  wl_display_ = wayland->display();
  parent_ = parent;
  if (!BindGlobals(error) || !InitEgl(error)) {
    Destroy();
    return false;
  }

  surface_ = wl_compositor_create_surface(compositor_);
  if (surface_ == nullptr) {
    Destroy();
    return Fail(error, "could not create the video wl_surface");
  }
  static const wl_surface_listener kSurfaceListener = {
      HandleSurfaceEnter,
      HandleSurfaceLeave,
      HandleSurfacePreferredBufferScale,
      HandleSurfacePreferredBufferTransform,
  };
  wl_surface_add_listener(surface_, &kSurfaceListener, this);

  // Input goes to the window, never to the plane.
  if (wl_region* empty = wl_compositor_create_region(compositor_)) {
    wl_surface_set_input_region(surface_, empty);
    wl_region_destroy(empty);
  }
  // Video is opaque; saying so lets the compositor skip blending it. The
  // compositor clamps the region to the surface, so one maximal region
  // outlives every resize.
  if (wl_region* opaque = wl_compositor_create_region(compositor_)) {
    wl_region_add(opaque, 0, 0, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
    wl_surface_set_opaque_region(surface_, opaque);
    wl_region_destroy(opaque);
  }

  viewport_ = wp_viewporter_get_viewport(viewporter_, surface_);
  subsurface_ = wl_subcompositor_get_subsurface(subcompositor_, surface_, parent_);
  if (viewport_ == nullptr || subsurface_ == nullptr) {
    Destroy();
    return Fail(error, "could not create the video subsurface");
  }
  wl_subsurface_place_below(subsurface_, parent_);
  wl_subsurface_set_desync(subsurface_);

  // A 1x1 window keeps EGL happy until the first SetRect().
  egl_window_ = wl_egl_window_create(surface_, 1, 1);
  if (egl_window_ == nullptr) {
    Destroy();
    return Fail(error, "could not create the video wl_egl_window");
  }
  egl_surface_ =
      eglCreateWindowSurface(egl_display_, egl_config_, reinterpret_cast<EGLNativeWindowType>(egl_window_), nullptr);
  if (egl_surface_ == EGL_NO_SURFACE) {
    Destroy();
    return Fail(error, "could not create the video EGL surface");
  }

  frame_ack_timer_ = std::make_unique<QTimer>();
  frame_ack_timer_->setSingleShot(true);
  frame_ack_timer_->setInterval(kFrameAckTimeoutMs);
  QObject::connect(frame_ack_timer_.get(), &QTimer::timeout, [this]() {
    if (!frame_pending_) return;
    // A late acknowledgement can't answer this callback any more, so it goes,
    // and rendering resumes as if it had been acknowledged.
    ClearFrameCallback();
    if (++consecutive_frame_acks_missed_ > kMaxConsecutiveFrameAckMisses) {
      if (consecutive_frame_acks_missed_ == kMaxConsecutiveFrameAckMisses + 1) {
        qCInfo(lcPlane, "compositor is not acknowledging frames; re-presenting every %d ms",
               kStalledRepresentIntervalMs);
      }
      ArmStalledRepresentTimer();
      return;
    }
    qCDebug(lcPlane, "frame not acknowledged within %d ms; re-presenting", kFrameAckTimeoutMs);
    if (on_frame_) on_frame_();
  });
  stalled_represent_timer_ = std::make_unique<QTimer>();
  stalled_represent_timer_->setSingleShot(true);
  stalled_represent_timer_->setInterval(kStalledRepresentIntervalMs);
  QObject::connect(stalled_represent_timer_.get(), &QTimer::timeout, [this]() {
    if (!visible_ || frame_pending_) return;
    if (on_frame_) on_frame_();
  });

  qCInfo(lcPlane, "video plane created");
  RequestParentCommit();
  return true;
}

void WaylandVideoPlane::Destroy() {
  frame_ack_timer_.reset();
  stalled_represent_timer_.reset();
  if (egl_surface_ != EGL_NO_SURFACE) {
    if (eglGetCurrentSurface(EGL_DRAW) == egl_surface_ || eglGetCurrentSurface(EGL_READ) == egl_surface_) {
      eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    eglDestroySurface(egl_display_, egl_surface_);
    egl_surface_ = EGL_NO_SURFACE;
  }
  if (egl_window_ != nullptr) {
    wl_egl_window_destroy(egl_window_);
    egl_window_ = nullptr;
  }
  ClearFrameCallback();
  on_frame_ = nullptr;
  on_screen_entered_ = nullptr;
  if (viewport_ != nullptr) {
    wp_viewport_destroy(viewport_);
    viewport_ = nullptr;
  }
  if (subsurface_ != nullptr) {
    wl_subsurface_destroy(subsurface_);
    subsurface_ = nullptr;
  }
  if (surface_ != nullptr) {
    wl_surface_destroy(surface_);
    surface_ = nullptr;
  }
  if (viewporter_ != nullptr) {
    wp_viewporter_destroy(viewporter_);
    viewporter_ = nullptr;
  }
  if (subcompositor_ != nullptr) {
    wl_subcompositor_destroy(subcompositor_);
    subcompositor_ = nullptr;
  }
  if (compositor_ != nullptr) {
    wl_compositor_destroy(compositor_);
    compositor_ = nullptr;
  }
  if (wl_display_ != nullptr) wl_display_flush(wl_display_);
  wl_display_ = nullptr;
  parent_ = nullptr;
  egl_config_ = nullptr;
  egl_display_ = EGL_NO_DISPLAY;
  window_ = nullptr;
  request_parent_commit_ = nullptr;
  rect_ = QRect();
  position_ = QPoint();
  buffer_width_ = 0;
  buffer_height_ = 0;
  rect_valid_ = false;
  visible_ = false;
  first_frame_presented_ = false;
  consecutive_frame_acks_missed_ = 0;
}

void WaylandVideoPlane::RequestParentCommit() {
  // Subsurface position and stacking only land when the window commits.
  if (request_parent_commit_) request_parent_commit_();
}

void WaylandVideoPlane::SetRect(const QRect& rect, qreal device_pixel_ratio) {
  const bool was_valid = rect_valid_;
  rect_valid_ = !rect.isEmpty();
  // Losing the rect must take the picture down, not just stop updating it.
  if (was_valid && !rect_valid_) DetachBuffer();

  const qreal dpr = device_pixel_ratio > 0 ? device_pixel_ratio : 1.0;
  const int buffer_width = rect_valid_ ? std::max(1, qRound(rect.width() * dpr)) : 1;
  const int buffer_height = rect_valid_ ? std::max(1, qRound(rect.height() * dpr)) : 1;
  // The window's surface includes client-side decorations, if any; the rect
  // is in content coordinates.
  const QMargins margins = window_ != nullptr ? window_->frameMargins() : QMargins();
  const QPoint position = rect.topLeft() + QPoint(margins.left(), margins.top());
  if (rect == rect_ && position == position_ && buffer_width == buffer_width_ && buffer_height == buffer_height_) {
    return;
  }

  const bool size_changed = buffer_width != buffer_width_ || buffer_height != buffer_height_;
  rect_ = rect;
  position_ = position;
  buffer_width_ = buffer_width;
  buffer_height_ = buffer_height;
  if (surface_ == nullptr || subsurface_ == nullptr || egl_window_ == nullptr) return;

  if (size_changed) wl_egl_window_resize(egl_window_, buffer_width_, buffer_height_, 0, 0);
  // Double-buffered on the plane; lands with the next swap.
  if (rect_valid_) {
    wp_viewport_set_destination(viewport_, rect_.width(), rect_.height());
  } else {
    wp_viewport_set_destination(viewport_, -1, -1);
  }
  wl_subsurface_set_position(subsurface_, position_.x(), position_.y());
  RequestParentCommit();
}

void WaylandVideoPlane::DetachBuffer() {
  // A subsurface has no visibility of its own: hidden means no buffer.
  if (surface_ == nullptr) return;
  ClearFrameCallback();
  if (stalled_represent_timer_) stalled_represent_timer_->stop();
  wl_surface_attach(surface_, nullptr, 0, 0);
  wl_surface_commit(surface_);
}

void WaylandVideoPlane::SetVisible(bool visible) {
  if (visible == visible_) return;
  visible_ = visible;
  if (surface_ == nullptr) return;
  if (!visible) DetachBuffer();
  RequestParentCommit();
}

void WaylandVideoPlane::ClearFrameCallback() {
  if (frame_ack_timer_) frame_ack_timer_->stop();
  if (frame_callback_ != nullptr) {
    wl_callback_destroy(frame_callback_);
    frame_callback_ = nullptr;
  }
  frame_pending_ = false;
}

void WaylandVideoPlane::ArmFrameAckWatchdog() {
  if (!frame_ack_timer_ || frame_ack_timer_->isActive() || !frame_pending_ || !visible_) return;
  frame_ack_timer_->start();
}

void WaylandVideoPlane::ArmStalledRepresentTimer() {
  if (stalled_represent_timer_ && !stalled_represent_timer_->isActive()) stalled_represent_timer_->start();
}

bool WaylandVideoPlane::PreparePresent() {
  if (!visible_ || egl_surface_ == EGL_NO_SURFACE || frame_pending_) return false;
  // Requested before the job is posted, so libwayland orders it ahead of the
  // worker's commit and the callback belongs to that frame.
  static const wl_callback_listener kFrameListener = {HandleFrameDone};
  frame_callback_ = wl_surface_frame(surface_);
  if (frame_callback_ != nullptr) {
    wl_callback_add_listener(frame_callback_, &kFrameListener, this);
    frame_pending_ = true;
  }
  return true;
}

bool WaylandVideoPlane::CompletePresent(bool swapped) {
  if (!swapped) {
    // The callback belongs to a commit that never happened.
    ClearFrameCallback();
    return false;
  }
  // A hide or rect loss during the swap already detached the buffer, and the
  // swap just attached one again.
  if (!visible_ || !rect_valid_) {
    DetachBuffer();
    return false;
  }
  if (!first_frame_presented_) {
    first_frame_presented_ = true;
    RequestParentCommit();
  }
  ArmFrameAckWatchdog();
  return true;
}

void WaylandVideoPlane::HandleFrameDone(void* data, wl_callback* callback, uint32_t time) {
  auto* self = static_cast<WaylandVideoPlane*>(data);
  self->last_frame_time_ms_ = time;
  // Every listener here assumes Qt dispatches the default queue on the GUI
  // thread; say so loudly if that ever changes.
  static bool warned = false;
  if (!warned && QThread::currentThread() != QCoreApplication::instance()->thread()) {
    warned = true;
    qCWarning(lcPlane, "Wayland events for the plane arrive off the GUI thread");
  }
  if (self->frame_callback_ == callback) {
    wl_callback_destroy(self->frame_callback_);
    self->frame_callback_ = nullptr;
  }
  self->frame_pending_ = false;
  self->consecutive_frame_acks_missed_ = 0;
  if (self->frame_ack_timer_) self->frame_ack_timer_->stop();
  if (self->stalled_represent_timer_) self->stalled_represent_timer_->stop();
  if (self->on_frame_) self->on_frame_();
}

void WaylandVideoPlane::HandleSurfaceEnter(void* data, wl_surface*, wl_output* output) {
  auto* self = static_cast<WaylandVideoPlane*>(data);
  if (!self->on_screen_entered_ || output == nullptr) return;
  for (QScreen* screen : QGuiApplication::screens()) {
    auto* wayland_screen = screen->nativeInterface<QNativeInterface::QWaylandScreen>();
    if (wayland_screen != nullptr && wayland_screen->output() == output) {
      self->on_screen_entered_(screen);
      return;
    }
  }
}

void WaylandVideoPlane::HandleSurfaceLeave(void*, wl_surface*, wl_output*) {}
void WaylandVideoPlane::HandleSurfacePreferredBufferScale(void*, wl_surface*, int32_t) {}
void WaylandVideoPlane::HandleSurfacePreferredBufferTransform(void*, wl_surface*, uint32_t) {}

}  // namespace ember::plane
