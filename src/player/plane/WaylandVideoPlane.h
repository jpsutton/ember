// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Plezy's linux/runner/mpv/wayland_video_surface.{h,cc}
// (GPL-3.0), without HDR description and with wp_viewporter for scaling.

#pragma once

#include <EGL/egl.h>

#include <QRect>
#include <QString>

#include <functional>
#include <memory>

class QScreen;
class QTimer;
class QWindow;

struct wl_callback;
struct wl_compositor;
struct wl_display;
struct wl_egl_window;
struct wl_output;
struct wl_subcompositor;
struct wl_subsurface;
struct wl_surface;
struct wp_viewport;
struct wp_viewporter;

namespace ember::plane {

// A wl_subsurface stacked below a transparent Qt window, carrying its own EGL
// window surface that mpv renders into. The window's own content blends over
// it, so video never passes through Qt's scene graph.
//
// Everything here runs on the GUI thread except the commit itself:
// PreparePresent() and CompletePresent() bracket the eglSwapBuffers that the
// render worker performs. The subsurface is desynchronized, so its commits
// don't wait for the window's. Its position is parent state and only lands on
// a window commit, which is why SetRect() asks for a window update.
//
// Sizing: the buffer is allocated in device pixels and wp_viewporter scales
// it to the rect's logical size, so fractional scale factors work without
// wl_surface.set_buffer_scale.
class WaylandVideoPlane {
 public:
  WaylandVideoPlane();
  ~WaylandVideoPlane();

  WaylandVideoPlane(const WaylandVideoPlane&) = delete;
  WaylandVideoPlane& operator=(const WaylandVideoPlane&) = delete;

  // True when the application runs on a Wayland display.
  static bool IsSupported();

  // Creates the subsurface under |window|'s surface and an EGL surface on it.
  // |request_parent_commit| must make the window commit soon (for a
  // QQuickWindow, update()). Returns false and fills |error| on failure.
  bool Create(QWindow* window, std::function<void()> request_parent_commit, QString* error);

  // Releases everything. Idempotent.
  void Destroy();

  bool valid() const { return egl_surface_ != EGL_NO_SURFACE; }
  wl_display* display() const { return wl_display_; }
  EGLDisplay egl_display() const { return egl_display_; }
  EGLConfig egl_config() const { return egl_config_; }
  EGLSurface egl_surface() const { return egl_surface_; }

  // Buffer size in device pixels.
  int buffer_width() const { return buffer_width_; }
  int buffer_height() const { return buffer_height_; }
  // Whether a non-empty rect has been set.
  bool has_size() const { return rect_valid_; }

  // Places the plane at |rect| (logical pixels, in the window's content
  // coordinates) with |device_pixel_ratio| device pixels per logical pixel.
  // An empty rect takes the picture down.
  void SetRect(const QRect& rect, qreal device_pixel_ratio);

  // Hiding detaches the buffer; the next present shows the plane again.
  void SetVisible(bool visible);
  bool visible() const { return visible_; }

  // True while a committed frame awaits the compositor's acknowledgement.
  // Rendering meanwhile would queue frames an occluded surface never takes.
  bool frame_pending() const { return frame_pending_; }
  bool first_frame_presented() const { return first_frame_presented_; }

  // Called when the compositor acknowledges a frame (or the watchdog gives
  // up on one). Rendering resumes from here.
  void SetFrameCallback(std::function<void()> callback) { on_frame_ = std::move(callback); }
  // Called when the plane enters an output, with Qt's screen for it.
  void SetScreenEnteredCallback(std::function<void(QScreen*)> callback) { on_screen_entered_ = std::move(callback); }

  // First half of a present: requests the frame callback that belongs to the
  // commit eglSwapBuffers is about to make. Returns false when the plane must
  // not present (hidden, no surface, a frame still unacknowledged).
  bool PreparePresent();
  // Second half, with the eglSwapBuffers result. Returns whether the frame
  // was really presented.
  bool CompletePresent(bool swapped);

 private:
  static void HandleFrameDone(void* data, wl_callback* callback, uint32_t time);
  static void HandleSurfaceEnter(void* data, wl_surface* surface, wl_output* output);
  static void HandleSurfaceLeave(void* data, wl_surface* surface, wl_output* output);
  static void HandleSurfacePreferredBufferScale(void* data, wl_surface* surface, int32_t factor);
  static void HandleSurfacePreferredBufferTransform(void* data, wl_surface* surface, uint32_t transform);

  bool BindGlobals(QString* error);
  bool InitEgl(QString* error);
  void RequestParentCommit();
  void ClearFrameCallback();
  void DetachBuffer();
  void ArmFrameAckWatchdog();
  void ArmStalledRepresentTimer();

  // A compositor may stop acknowledging frames for a hidden or minimized
  // surface (KWin does). Without a bound, one missed callback would freeze
  // the plane on its last buffer. After a few misses in a row the plane backs
  // off to re-presenting once a second until frames are acknowledged again.
  static constexpr int kFrameAckTimeoutMs = 500;
  static constexpr int kMaxConsecutiveFrameAckMisses = 5;
  static constexpr int kStalledRepresentIntervalMs = 1000;

  QWindow* window_ = nullptr;
  std::function<void()> request_parent_commit_;

  wl_display* wl_display_ = nullptr;          // owned by Qt
  wl_surface* parent_ = nullptr;              // owned by Qt
  wl_compositor* compositor_ = nullptr;       // bound by us
  wl_subcompositor* subcompositor_ = nullptr;  // bound by us
  wp_viewporter* viewporter_ = nullptr;       // bound by us
  wl_surface* surface_ = nullptr;
  wl_subsurface* subsurface_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wl_egl_window* egl_window_ = nullptr;

  EGLDisplay egl_display_ = EGL_NO_DISPLAY;
  EGLConfig egl_config_ = nullptr;
  EGLSurface egl_surface_ = EGL_NO_SURFACE;

  QRect rect_;
  QPoint position_;
  int buffer_width_ = 0;
  int buffer_height_ = 0;
  bool rect_valid_ = false;
  bool visible_ = false;
  bool first_frame_presented_ = false;
  bool frame_pending_ = false;
  wl_callback* frame_callback_ = nullptr;
  int consecutive_frame_acks_missed_ = 0;
  std::unique_ptr<QTimer> frame_ack_timer_;
  std::unique_ptr<QTimer> stalled_represent_timer_;

  std::function<void()> on_frame_;
  std::function<void(QScreen*)> on_screen_entered_;
};

}  // namespace ember::plane
