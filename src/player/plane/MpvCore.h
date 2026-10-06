// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Plezy's linux/runner/mpv/mpv_player.{h,cc} (GPL-3.0), without
// the HDR output path and the audio-only core.

#pragma once

#include <EGL/egl.h>
#include <mpv/client.h>
#include <mpv/render.h>

#include <QString>
#include <QVariant>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mpv_player_common.h"

class QTimer;

namespace ember::plane {

// Wraps one libmpv core rendering into the video plane's EGL surface.
//
// Threads:
//  - The GUI thread owns the object: setup, commands, property access and
//    event processing all run there.
//  - The plane render worker calls RenderToSurface only.
//  - mpv's own threads call the wakeup and render-update callbacks, which do
//    nothing but queue work onto the GUI thread.
class MpvCore {
 public:
  // Events and observed property changes, delivered on the GUI thread.
  // Events: "log-message" {prefix, level, text}, "start-file", "file-loaded",
  // "playback-restart", "end-file" {reason, error?, message?, cause?}.
  using EventCallback = std::function<void(const QString& name, const QVariantMap& data)>;
  using PropertyCallback = std::function<void(const QString& name, const QVariant& value)>;
  // Called on the GUI thread when mpv has a new frame to render.
  using RedrawCallback = std::function<void()>;

  using StatusCallback = plezy::mpv_common::StatusCallback;
  using CommandCallback = plezy::mpv_common::CommandCallback;
  using GetPropertyCallback = plezy::mpv_common::GetPropertyCallback;

  MpvCore();
  ~MpvCore();

  MpvCore(const MpvCore&) = delete;
  MpvCore& operator=(const MpvCore&) = delete;

  // Creates and initializes the mpv core. The render context comes later,
  // once the plane's EGL surface exists.
  bool Initialize();

  // Creates mpv's render context on a private EGL context bound to the
  // plane's surface. |wl_display| is handed to mpv for VA-API interop.
  bool InitRenderContextForSurface(EGLDisplay display, EGLConfig config, EGLSurface surface, void* wl_display);

  // Renders one frame into |surface|'s default framebuffer. Runs on the plane
  // render worker. The caller presents it with eglSwapBuffers.
  bool RenderToSurface(EGLSurface surface, int width, int height);
  // After the swap of a frame RenderToSurface drew: tells mpv when it went
  // out, which display-sync times frames by. Render worker only.
  void ReportSwap();

  // Stops mpv and hands the core, render context and EGL context to the
  // teardown thread. The caller must have drained the render worker first.
  void Dispose();

  bool IsInitialized() const;
  bool IsDisposed() const { return disposed_.load(); }

  void Command(const std::vector<std::string>& args) { CommandAsync(args, nullptr); }
  void CommandAsync(const std::vector<std::string>& args, CommandCallback callback);
  void SetPropertyAsync(const std::string& name, const std::string& value, StatusCallback callback = nullptr);
  void SetPropertyAsync(const std::string& name, double value, StatusCallback callback = nullptr);
  void GetPropertyAsync(const std::string& name, GetPropertyCallback callback);
  // |format| is one of "string", "flag", "int64", "double", "node".
  void ObserveProperty(const std::string& name, const std::string& format);
  void SetLogLevel(const std::string& level);

  void SetEventCallback(EventCallback callback);
  void SetPropertyCallback(PropertyCallback callback);
  void SetRedrawCallback(RedrawCallback callback);

  bool NeedsRedraw() const { return needs_redraw_.load(); }

 private:
  // Lets mpv's threads reach the player only while it is alive. Dispose()
  // detaches it and waits out any callback already holding a lease.
  class CallbackContext {
   public:
    class Lease {
     public:
      Lease() = default;
      Lease(const Lease&) = delete;
      Lease& operator=(const Lease&) = delete;
      Lease(Lease&& other) noexcept;
      Lease& operator=(Lease&& other) noexcept;
      ~Lease();

      explicit operator bool() const { return player_ != nullptr; }
      MpvCore* player() const { return player_; }

     private:
      friend class CallbackContext;
      Lease(CallbackContext* context, MpvCore* player);
      void Release();

      CallbackContext* context_ = nullptr;
      MpvCore* player_ = nullptr;
    };

    explicit CallbackContext(MpvCore* player) : player_(player) {}

    Lease Acquire();
    void DetachAndWait();

   private:
    void ReleaseLease();

    std::mutex mutex_;
    std::condition_variable quiescent_;
    MpvCore* player_;
    size_t in_flight_ = 0;
  };

  // What ProcessEvents delivers, held back in order while an error END_FILE
  // drains (see ErrorEndFileHold in mpv_player_common.h).
  struct Message {
    bool is_property = false;
    QString name;
    QVariantMap data;
    QVariant value;
  };

  static void OnMpvWakeup(void* ctx);
  static void OnMpvRenderUpdate(void* ctx);

  void ProcessEvents();
  void HandleMpvEvent(mpv_event* event);
  void SendEvent(const QString& name, QVariantMap data = {});
  void Deliver(Message message, bool is_log_message);
  void ReleaseHeldMessages();

  void LogRecovery(const QString& text);
  void MaybeRunAudioRecovery();
  void EnsureAudioRecoveryTimer();

  mpv_handle* mpv_ = nullptr;
  mpv_render_context* mpv_gl_ = nullptr;
  EGLDisplay egl_display_ = EGL_NO_DISPLAY;
  EGLContext egl_context_ = EGL_NO_CONTEXT;
  mutable std::mutex native_mutex_;

  std::atomic<bool> needs_redraw_{false};
  std::atomic<bool> wakeup_pending_{false};
  std::atomic<bool> disposed_{false};

  std::mutex callback_mutex_;
  EventCallback event_callback_;
  PropertyCallback property_callback_;
  RedrawCallback redraw_callback_;

  plezy::mpv_common::AsyncRequestRegistry pending_requests_;
  plezy::mpv_common::PropertyObservationRegistry observed_properties_;
  plezy::mpv_common::ErrorEndFileHold<Message> held_messages_;
  plezy::mpv_common::AudioRecoveryState audio_recovery_;
  // Set when audio recovery gave up and stopped playback itself; the END_FILE
  // that follows is reported as the audio failure, not as a user stop.
  bool audio_output_failed_ = false;
  std::unique_ptr<QTimer> recovery_timer_;

  std::shared_ptr<CallbackContext> callback_context_;
};

}  // namespace ember::plane
