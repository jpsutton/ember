// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Plezy's linux/runner/mpv/mpv_player.cc (GPL-3.0).

#include "MpvCore.h"

#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <mpv/render_gl.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QTimer>

#include <chrono>
#include <clocale>
#include <cstring>
#include <thread>

#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION EGL_CONTEXT_CLIENT_VERSION
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif

Q_LOGGING_CATEGORY(lcMpv, "ember.mpv")

namespace ember::plane {
namespace {

// libmpv parses numeric options on its own threads, so the C numeric locale
// has to be set process-wide, once, before the first core starts.
bool EnsureProcessNumericLocale() {
  static const bool configured = std::setlocale(LC_NUMERIC, "C") != nullptr;
  return configured;
}

// The runner's own observation of the decode path mpv took. A silent fallback
// to software decoding has no other symptom, so every change is logged.
constexpr uint64_t kHwdecCurrentUserdata = UINT64_MAX - 1;

void* GetProcAddress(void* ctx, const char* name) {
  (void)ctx;
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

const char* GlString(GLenum name) {
  using GetString = const GLubyte* (*)(GLenum);
  static auto get_string = reinterpret_cast<GetString>(eglGetProcAddress("glGetString"));
  if (get_string == nullptr) return nullptr;
  return reinterpret_cast<const char*>(get_string(name));
}

// Mesa's software rasterizers. On those, handing mpv the Wayland display for
// VA-API is useless and, on some compositors, crashes inside vaInitialize.
bool IsSoftwareGlRenderer(const char* renderer) {
  if (renderer == nullptr) return false;
  return strstr(renderer, "llvmpipe") != nullptr || strstr(renderer, "softpipe") != nullptr ||
         strstr(renderer, "swrast") != nullptr || strstr(renderer, "Software Rasterizer") != nullptr;
}

std::string SanitizeUtf8(const char* value) { return QString::fromUtf8(value).toStdString(); }

// Builds QVariants from mpv nodes for plezy::mpv_common::ConvertNode.
// QString::fromUtf8 replaces invalid UTF-8, which mpv strings may contain.
struct VariantNodeBuilder {
  using Value = QVariant;
  using ListBuilder = QVariantList;
  using MapBuilder = QVariantMap;

  static Value Null() { return {}; }
  static Value Boolean(bool value) { return value; }
  static Value Int(int64_t value) { return QVariant::fromValue<qlonglong>(value); }
  static Value Double(double value) { return value; }
  static Value String(const char* value, size_t length) {
    return QString::fromUtf8(value, static_cast<qsizetype>(length));
  }
  static ListBuilder NewList() { return {}; }
  static void Append(ListBuilder& list, Value value) { list.append(std::move(value)); }
  static Value FinishList(ListBuilder list) { return list; }
  static MapBuilder NewMap() { return {}; }
  static void Insert(MapBuilder& map, const char* key, size_t key_length, Value value) {
    map.insert(QString::fromUtf8(key, static_cast<qsizetype>(key_length)), std::move(value));
  }
  static Value FinishMap(MapBuilder map) { return map; }
  static void AbandonMap(MapBuilder& map) { map.clear(); }
};

// Releases render contexts, their EGL contexts and mpv cores on a thread of
// its own. A render context may only be freed with its EGL context current,
// and mpv_terminate_destroy can block on a stuck stream; neither belongs on
// the GUI thread. The queue lives until the process exits.
class TeardownQueue {
 public:
  struct Batch {
    mpv_render_context* render = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    mpv_handle* handle = nullptr;
    std::shared_ptr<void> keep_alive;
  };

  static TeardownQueue& Instance() {
    static TeardownQueue* const queue = new TeardownQueue();
    return *queue;
  }

  void Enqueue(Batch batch) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      batches_.push_back(std::move(batch));
    }
    condition_.notify_one();
  }

 private:
  TeardownQueue() : worker_([this]() { Run(); }) {}

  // Returns false when the EGL context could not be made current; the batch
  // is then retried, and its mpv core is never terminated before the render
  // context is gone.
  static bool Release(Batch& batch) {
    if (batch.context != EGL_NO_CONTEXT) {
      if (!eglBindAPI(EGL_OPENGL_ES_API) ||
          !eglMakeCurrent(batch.display, EGL_NO_SURFACE, EGL_NO_SURFACE, batch.context)) {
        qCWarning(lcMpv, "teardown: could not make the EGL context current: 0x%x", eglGetError());
        return false;
      }
      if (batch.render != nullptr) {
        mpv_render_context_free(batch.render);
        batch.render = nullptr;
      }
      eglMakeCurrent(batch.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(batch.display, batch.context);
      batch.context = EGL_NO_CONTEXT;
    }
    if (batch.handle != nullptr) {
      mpv_terminate_destroy(batch.handle);
      batch.handle = nullptr;
    }
    batch.keep_alive.reset();
    return true;
  }

  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      condition_.wait(lock, [this]() { return !batches_.empty(); });
      std::vector<Batch> work = std::move(batches_);
      batches_.clear();
      lock.unlock();
      std::vector<Batch> retry;
      for (auto& batch : work) {
        if (!Release(batch)) retry.push_back(std::move(batch));
      }
      lock.lock();
      for (auto& batch : retry) batches_.push_back(std::move(batch));
      if (!batches_.empty()) condition_.wait_for(lock, std::chrono::milliseconds(100));
    }
  }

  std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<Batch> batches_;
  std::thread worker_;
};

}  // namespace

MpvCore::CallbackContext::Lease::Lease(CallbackContext* context, MpvCore* player)
    : context_(context), player_(player) {}

MpvCore::CallbackContext::Lease::Lease(Lease&& other) noexcept : context_(other.context_), player_(other.player_) {
  other.context_ = nullptr;
  other.player_ = nullptr;
}

MpvCore::CallbackContext::Lease& MpvCore::CallbackContext::Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    Release();
    context_ = other.context_;
    player_ = other.player_;
    other.context_ = nullptr;
    other.player_ = nullptr;
  }
  return *this;
}

MpvCore::CallbackContext::Lease::~Lease() { Release(); }

void MpvCore::CallbackContext::Lease::Release() {
  if (context_ == nullptr) return;
  context_->ReleaseLease();
  context_ = nullptr;
  player_ = nullptr;
}

MpvCore::CallbackContext::Lease MpvCore::CallbackContext::Acquire() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (player_ == nullptr) return Lease();
  ++in_flight_;
  return Lease(this, player_);
}

void MpvCore::CallbackContext::DetachAndWait() {
  std::unique_lock<std::mutex> lock(mutex_);
  player_ = nullptr;
  quiescent_.wait(lock, [this]() { return in_flight_ == 0; });
}

void MpvCore::CallbackContext::ReleaseLease() {
  std::lock_guard<std::mutex> lock(mutex_);
  --in_flight_;
  if (in_flight_ == 0) quiescent_.notify_all();
}

MpvCore::MpvCore() : callback_context_(std::make_shared<CallbackContext>(this)) {}

MpvCore::~MpvCore() { Dispose(); }

bool MpvCore::IsInitialized() const {
  std::lock_guard<std::mutex> lock(native_mutex_);
  return mpv_ != nullptr && mpv_gl_ != nullptr;
}

bool MpvCore::Initialize() {
  std::lock_guard<std::mutex> lock(native_mutex_);
  if (disposed_) return false;
  if (mpv_ != nullptr) return true;
  if (!EnsureProcessNumericLocale()) {
    qCWarning(lcMpv, "could not set the C numeric locale");
    return false;
  }

  mpv_ = mpv_create();
  if (mpv_ == nullptr) {
    qCWarning(lcMpv, "mpv_create() failed");
    return false;
  }
  plezy::mpv_common::ApplyCommonStartupOptions(mpv_, false);
  mpv_set_option_string(mpv_, "terminal", "no");
  mpv_set_option_string(mpv_, "vo", "libmpv");
  mpv_set_option_string(mpv_, "hwdec", "auto-safe");
  // Info level keeps mpv's hwdec probe and "Using software decoding" lines,
  // the only evidence a silent software fallback leaves.
  mpv_request_log_messages(mpv_, "info");
  // For experiments: extra mpv options as "name=value,name=value".
  for (const QByteArray& option : qgetenv("EMBER_MPV_OPTIONS").split(',')) {
    const qsizetype equals = option.indexOf('=');
    if (equals <= 0) continue;
    const QByteArray name = option.left(equals).trimmed();
    const QByteArray value = option.mid(equals + 1).trimmed();
    const int set = mpv_set_option_string(mpv_, name.constData(), value.constData());
    qCInfo(lcMpv, "EMBER_MPV_OPTIONS: %s=%s: %s", name.constData(), value.constData(), mpv_error_string(set));
  }

  const int error = mpv_initialize(mpv_);
  if (error < 0) {
    qCWarning(lcMpv, "mpv_initialize() failed: %s", mpv_error_string(error));
    mpv_destroy(mpv_);
    mpv_ = nullptr;
    return false;
  }

  mpv_set_wakeup_callback(mpv_, OnMpvWakeup, callback_context_.get());
  // Userdata 0 marks the audio recovery's own observations.
  mpv_observe_property(mpv_, 0, "current-ao", MPV_FORMAT_STRING);
  mpv_observe_property(mpv_, 0, "audio-device-list", MPV_FORMAT_NONE);
  mpv_observe_property(mpv_, kHwdecCurrentUserdata, "hwdec-current", MPV_FORMAT_STRING);
  qCInfo(lcMpv, "core initialized; render context deferred");
  return true;
}

bool MpvCore::InitRenderContextForSurface(EGLDisplay display, EGLConfig config, EGLSurface surface, void* wl_display) {
  std::lock_guard<std::mutex> lock(native_mutex_);
  if (disposed_ || mpv_ == nullptr) return false;
  if (mpv_gl_ != nullptr) return true;
  if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return false;
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    qCWarning(lcMpv, "eglBindAPI(GLES) failed: 0x%x", eglGetError());
    return false;
  }

  // The plane's context shares nothing with Qt's, so take the newest GLES the
  // driver offers; 3.x gives mpv float render targets.
  struct EsVersion {
    EGLint major;
    EGLint minor;
  };
  static constexpr EsVersion kVersions[] = {{3, 2}, {3, 1}, {3, 0}, {2, 0}};
  EGLContext context = EGL_NO_CONTEXT;
  for (const EsVersion& version : kVersions) {
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, version.major, EGL_CONTEXT_MINOR_VERSION, version.minor,
                                 EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
    if (context != EGL_NO_CONTEXT) break;
  }
  if (context == EGL_NO_CONTEXT) {
    qCWarning(lcMpv, "could not create the video plane's EGL context: 0x%x", eglGetError());
    return false;
  }
  auto destroy_context = [&]() {
    if (eglGetCurrentContext() == context) eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
  };
  if (!eglMakeCurrent(display, surface, surface, context)) {
    qCWarning(lcMpv, "could not make the video plane's EGL context current: 0x%x", eglGetError());
    destroy_context();
    return false;
  }

  const char* gl_version = GlString(GL_VERSION);
  const char* gl_renderer = GlString(GL_RENDERER);
  const bool software_renderer = IsSoftwareGlRenderer(gl_renderer);
  qCInfo(lcMpv, "video plane GL: '%s' on '%s'", gl_version ? gl_version : "?", gl_renderer ? gl_renderer : "?");

  // mpv's VA-API interop probe is lazy and never fails context creation, so a
  // missing prerequisite would only show as silent software decoding.
  const char* egl_extensions = eglQueryString(display, EGL_EXTENSIONS);
  const char* gl_extensions = GlString(GL_EXTENSIONS);
  const bool has_dma_buf = egl_extensions && strstr(egl_extensions, "EGL_EXT_image_dma_buf_import");
  const bool has_image_base = egl_extensions && strstr(egl_extensions, "EGL_KHR_image_base");
  const bool has_oes_egl_image = gl_extensions && strstr(gl_extensions, "GL_OES_EGL_image");
  if (!has_dma_buf || !has_image_base || !has_oes_egl_image) {
    qCWarning(lcMpv,
              "VA-API interop prerequisites missing (dma_buf_import=%d image_base=%d OES_EGL_image=%d); "
              "decoding may fall back to software",
              has_dma_buf, has_image_base, has_oes_egl_image);
  }

  // The plane paces itself with its own frame callbacks, so eglSwapBuffers
  // must never block on the compositor (an occluded surface gets no frames).
  if (!eglSwapInterval(display, 0)) {
    qCWarning(lcMpv, "could not disable swap throttling on the video plane: 0x%x", eglGetError());
  }

  mpv_opengl_init_params gl_init_params{};
  gl_init_params.get_proc_address = GetProcAddress;
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
      {MPV_RENDER_PARAM_INVALID, nullptr},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  if (wl_display != nullptr && !software_renderer) {
    params[2].type = MPV_RENDER_PARAM_WL_DISPLAY;
    params[2].data = wl_display;
  }

  mpv_render_context* render = nullptr;
  const int error = mpv_render_context_create(&render, mpv_, params);
  if (error < 0 || render == nullptr) {
    qCWarning(lcMpv, "mpv_render_context_create() failed: %s", mpv_error_string(error));
    if (render != nullptr) mpv_render_context_free(render);
    destroy_context();
    return false;
  }

  egl_display_ = display;
  egl_context_ = context;
  mpv_gl_ = render;
  mpv_render_context_set_update_callback(mpv_gl_, OnMpvRenderUpdate, callback_context_.get());
  // From here on only the render worker makes the context current, and an
  // EGL context can be current on one thread at a time.
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  qCInfo(lcMpv, "render context created on the video plane");
  return true;
}

bool MpvCore::RenderToSurface(EGLSurface surface, int width, int height) {
  // No lock across the render: the caller drains the render worker before
  // Dispose(), so mpv_gl_ and the EGL context outlive every job.
  mpv_render_context* render = nullptr;
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  {
    std::lock_guard<std::mutex> lock(native_mutex_);
    if (disposed_ || mpv_gl_ == nullptr || egl_context_ == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE) return false;
    if (width < 1 || height < 1) return false;
    render = mpv_gl_;
    display = egl_display_;
    context = egl_context_;
  }
  if (!eglBindAPI(EGL_OPENGL_ES_API) || !eglMakeCurrent(display, surface, surface, context)) {
    qCWarning(lcMpv, "could not make the EGL context current for render: 0x%x", eglGetError());
    return false;
  }
  // Consumed before rendering: OnMpvRenderUpdate ignores updates until then.
  needs_redraw_.store(false);

  mpv_opengl_fbo fbo{};
  fbo.fbo = 0;  // the window surface's default framebuffer
  fbo.w = width;
  fbo.h = height;
  int flip_y = 1;
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
      {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context_render(render, params);
  return true;
}

void MpvCore::ReportSwap() {
  mpv_render_context* render = nullptr;
  {
    std::lock_guard<std::mutex> lock(native_mutex_);
    if (disposed_ || mpv_gl_ == nullptr) return;
    render = mpv_gl_;
  }
  mpv_render_context_report_swap(render);
}

void MpvCore::Dispose() {
  if (disposed_.exchange(true)) return;

  {
    std::lock_guard<std::mutex> lock(native_mutex_);
    if (mpv_ != nullptr) {
      const char* stop[] = {"stop", nullptr};
      mpv_command_async(mpv_, 0, stop);
      mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    }
    if (mpv_gl_ != nullptr) mpv_render_context_set_update_callback(mpv_gl_, nullptr, nullptr);
  }
  callback_context_->DetachAndWait();

  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = nullptr;
    property_callback_ = nullptr;
    redraw_callback_ = nullptr;
  }
  auto cancelled = pending_requests_.CancelAll();
  for (auto& callback : cancelled.status) callback(MPV_ERROR_UNINITIALIZED);
  for (auto& callback : cancelled.commands) callback(MPV_ERROR_UNINITIALIZED, nullptr);
  for (auto& callback : cancelled.properties) callback(MPV_ERROR_UNINITIALIZED, "");
  if (recovery_timer_) recovery_timer_->stop();

  // Normally the render worker already unbound the context; this covers a
  // context created but never rendered with.
  if (egl_context_ != EGL_NO_CONTEXT && eglGetCurrentContext() == egl_context_) {
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  }

  TeardownQueue::Batch batch;
  {
    std::lock_guard<std::mutex> lock(native_mutex_);
    batch.render = mpv_gl_;
    batch.display = egl_display_;
    batch.context = egl_context_;
    batch.handle = mpv_;
    batch.keep_alive = callback_context_;
    mpv_gl_ = nullptr;
    mpv_ = nullptr;
    egl_display_ = EGL_NO_DISPLAY;
    egl_context_ = EGL_NO_CONTEXT;
  }
  if (batch.handle != nullptr || batch.context != EGL_NO_CONTEXT) TeardownQueue::Instance().Enqueue(std::move(batch));
  observed_properties_.Clear();
}

void MpvCore::CommandAsync(const std::vector<std::string>& args, CommandCallback callback) {
  if (disposed_ || mpv_ == nullptr) {
    if (callback) callback(MPV_ERROR_UNINITIALIZED, nullptr);
    return;
  }
  plezy::mpv_common::SubmitCommandAsync(mpv_, pending_requests_, args, std::move(callback));
}

void MpvCore::SetPropertyAsync(const std::string& name, const std::string& value, StatusCallback callback) {
  if (disposed_ || mpv_ == nullptr) {
    if (callback) callback(MPV_ERROR_UNINITIALIZED);
    return;
  }
  plezy::mpv_common::SubmitSetPropertyAsync(
      mpv_, pending_requests_, name, value, [this, name, cb = std::move(callback)](int error) {
        if (error < 0 && !disposed_) {
          qCWarning(lcMpv, "set property '%s' failed: %s", name.c_str(), mpv_error_string(error));
        }
        if (cb) cb(error);
      });
}

void MpvCore::SetPropertyAsync(const std::string& name, double value, StatusCallback callback) {
  if (disposed_ || mpv_ == nullptr) {
    if (callback) callback(MPV_ERROR_UNINITIALIZED);
    return;
  }
  plezy::mpv_common::SubmitSetPropertyAsync(
      mpv_, pending_requests_, name, value, [this, name, value, cb = std::move(callback)](int error) {
        if (error < 0 && !disposed_) {
          qCWarning(lcMpv, "set property '%s'=%g failed: %s", name.c_str(), value, mpv_error_string(error));
        }
        if (cb) cb(error);
      });
}

void MpvCore::GetPropertyAsync(const std::string& name, GetPropertyCallback callback) {
  if (disposed_ || mpv_ == nullptr) {
    if (callback) callback(MPV_ERROR_UNINITIALIZED, "");
    return;
  }
  plezy::mpv_common::SubmitGetPropertyAsync(mpv_, pending_requests_, name, std::move(callback));
}

void MpvCore::ObserveProperty(const std::string& name, const std::string& format) {
  if (disposed_ || mpv_ == nullptr) return;
  const auto request = observed_properties_.Register(name, format, 0);
  if (!request.added) return;
  mpv_observe_property(mpv_, request.userdata, name.c_str(), request.format);
}

void MpvCore::SetLogLevel(const std::string& level) {
  if (disposed_ || mpv_ == nullptr) return;
  mpv_request_log_messages(mpv_, level.c_str());
}

void MpvCore::SetEventCallback(EventCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  event_callback_ = std::move(callback);
}

void MpvCore::SetPropertyCallback(PropertyCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  property_callback_ = std::move(callback);
}

void MpvCore::SetRedrawCallback(RedrawCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  redraw_callback_ = std::move(callback);
}

void MpvCore::OnMpvWakeup(void* ctx) {
  auto* context = static_cast<CallbackContext*>(ctx);
  auto lease = context->Acquire();
  if (!lease) return;
  MpvCore* player = lease.player();
  if (player->disposed_) return;
  bool expected = false;
  if (!player->wakeup_pending_.compare_exchange_strong(expected, true)) return;
  // The queued call holds the context, not the player: after Dispose() the
  // lease comes back empty and the call does nothing.
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [context = player->callback_context_]() {
        auto lease = context->Acquire();
        if (!lease) return;
        MpvCore* player = lease.player();
        player->wakeup_pending_.store(false);
        if (!player->disposed_) player->ProcessEvents();
      },
      Qt::QueuedConnection);
}

void MpvCore::OnMpvRenderUpdate(void* ctx) {
  auto* context = static_cast<CallbackContext*>(ctx);
  auto lease = context->Acquire();
  if (!lease) return;
  MpvCore* player = lease.player();
  if (player->disposed_) return;
  // Only the false-to-true edge schedules a redraw; the render clears it.
  bool expected = false;
  if (!player->needs_redraw_.compare_exchange_strong(expected, true)) return;
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [context = player->callback_context_]() {
        auto lease = context->Acquire();
        if (!lease) return;
        MpvCore* player = lease.player();
        if (player->disposed_) return;
        RedrawCallback callback;
        {
          std::lock_guard<std::mutex> lock(player->callback_mutex_);
          callback = player->redraw_callback_;
        }
        if (callback) callback();
      },
      Qt::QueuedConnection);
}

void MpvCore::ProcessEvents() {
  if (disposed_ || mpv_ == nullptr) return;
  for (;;) {
    mpv_event* event = mpv_wait_event(mpv_, 0);
    if (event->event_id == MPV_EVENT_NONE || event->event_id == MPV_EVENT_SHUTDOWN) break;
    const bool began_hold = plezy::mpv_common::IsErrorEndFile(event) && held_messages_.Begin();
    HandleMpvEvent(event);
    if (!began_hold && held_messages_.CountDequeued()) ReleaseHeldMessages();
    if (disposed_) return;
  }
  ReleaseHeldMessages();
}

void MpvCore::HandleMpvEvent(mpv_event* event) {
  if (plezy::mpv_common::DispatchReplyEvent(pending_requests_, event, SanitizeUtf8)) return;

  switch (event->event_id) {
    case MPV_EVENT_LOG_MESSAGE: {
      auto* message = static_cast<mpv_event_log_message*>(event->data);
      if (message == nullptr) break;
      SendEvent(QStringLiteral("log-message"), {{QStringLiteral("prefix"), QString::fromUtf8(message->prefix)},
                                                {QStringLiteral("level"), QString::fromUtf8(message->level)},
                                                {QStringLiteral("text"), QString::fromUtf8(message->text).trimmed()}});
      break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
      auto* property = static_cast<mpv_event_property*>(event->data);
      if (property == nullptr || property->name == nullptr) break;
      mpv_node node = plezy::mpv_common::ExtractPropertyNode(property);
      if (event->reply_userdata == kHwdecCurrentUserdata) {
        const char* value = node.format == MPV_FORMAT_STRING ? node.u.string : nullptr;
        qCInfo(lcMpv, "hwdec-current=%s", value && value[0] != '\0' ? value : "(none)");
        break;
      }
      const auto notice = plezy::mpv_common::ObserveAudioRecoveryProperty(audio_recovery_, event, property);
      if (notice.message) LogRecovery(QString::fromLatin1(notice.message));
      if (notice.scheduled_work) EnsureAudioRecoveryTimer();
      if (event->reply_userdata == 0) break;  // the recovery's own observation

      int unused_id = 0;
      if (!observed_properties_.LookupId(property->name, &unused_id)) break;
      Message message;
      message.is_property = true;
      message.name = QString::fromUtf8(property->name);
      message.value = plezy::mpv_common::ConvertNode<VariantNodeBuilder>(&node);
      Deliver(std::move(message), false);
      break;
    }
    case MPV_EVENT_START_FILE:
      SendEvent(QStringLiteral("start-file"));
      break;
    case MPV_EVENT_FILE_LOADED:
      audio_recovery_.SetFileLoaded(true);
      EnsureAudioRecoveryTimer();
      SendEvent(QStringLiteral("file-loaded"));
      break;
    case MPV_EVENT_PLAYBACK_RESTART:
      SendEvent(QStringLiteral("playback-restart"));
      break;
    case MPV_EVENT_END_FILE: {
      audio_recovery_.SetFileLoaded(false);
      const bool audio_output_failed = audio_output_failed_;
      audio_output_failed_ = false;
      auto* end = static_cast<mpv_event_end_file*>(event->data);
      if (end == nullptr) break;
      const int reason = audio_output_failed ? MPV_END_FILE_REASON_ERROR : static_cast<int>(end->reason);
      const int error = audio_output_failed ? MPV_ERROR_AO_INIT_FAILED : end->error;
      QVariantMap data{{QStringLiteral("reason"), reason}};
      if (reason == MPV_END_FILE_REASON_ERROR) {
        data.insert(QStringLiteral("error"), error);
        data.insert(QStringLiteral("message"), QString::fromUtf8(mpv_error_string(error)));
        if (audio_output_failed) {
          data.insert(QStringLiteral("cause"), QString::fromLatin1(plezy::mpv_common::kAudioOutputFailedCause));
        }
      }
      SendEvent(QStringLiteral("end-file"), std::move(data));
      break;
    }
    default:
      break;
  }
}

void MpvCore::SendEvent(const QString& name, QVariantMap data) {
  Message message;
  message.name = name;
  message.data = std::move(data);
  Deliver(std::move(message), name == QLatin1String("log-message"));
}

void MpvCore::Deliver(Message message, bool is_log_message) {
  if (held_messages_.ShouldHold(is_log_message)) {
    held_messages_.Hold(std::move(message));
    return;
  }
  EventCallback event_callback;
  PropertyCallback property_callback;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback = event_callback_;
    property_callback = property_callback_;
  }
  if (message.is_property) {
    if (property_callback) property_callback(message.name, message.value);
  } else if (event_callback) {
    event_callback(message.name, message.data);
  }
}

void MpvCore::ReleaseHeldMessages() {
  for (Message& message : held_messages_.Release()) Deliver(std::move(message), false);
}

void MpvCore::LogRecovery(const QString& text) {
  qCWarning(lcMpv, "audio recovery: %s", qPrintable(text));
  SendEvent(QStringLiteral("log-message"), {{QStringLiteral("prefix"), QStringLiteral("audio-recovery")},
                                            {QStringLiteral("level"), QStringLiteral("warn")},
                                            {QStringLiteral("text"), text}});
}

void MpvCore::MaybeRunAudioRecovery() {
  using plezy::mpv_common::AudioReloadReason;
  const auto action = audio_recovery_.NextReload(plezy::mpv_common::AudioRecoveryState::Clock::now());
  if (action.reason == AudioReloadReason::kNone) return;
  if (action.reason == AudioReloadReason::kGiveUp) {
    // audio-fallback-to-null means mpv never ends a file over a dead device
    // itself, so the outcome is produced here.
    LogRecovery(QStringLiteral("audio output failed after %1 reloads; ending playback").arg(action.attempt));
    audio_output_failed_ = true;
    Command({"stop"});
    return;
  }
  const QString reason =
      action.reason == AudioReloadReason::kResume ? QStringLiteral("resume") : QStringLiteral("null-fallback");
  LogRecovery(QStringLiteral("issuing ao-reload (reason=%1, attempt %2)").arg(reason).arg(action.attempt));
  const uint64_t generation = action.request_generation;
  CommandAsync({"ao-reload"}, [context = callback_context_, generation](int, const mpv_node*) {
    auto lease = context->Acquire();
    if (lease) lease.player()->audio_recovery_.CompleteReload(generation);
  });
}

void MpvCore::EnsureAudioRecoveryTimer() {
  if (!audio_recovery_.HasPendingWork()) return;
  if (!recovery_timer_) {
    recovery_timer_ = std::make_unique<QTimer>();
    recovery_timer_->setInterval(100);
    QObject::connect(recovery_timer_.get(), &QTimer::timeout, [this]() {
      if (disposed_) {
        recovery_timer_->stop();
        return;
      }
      MaybeRunAudioRecovery();
      if (!audio_recovery_.HasPendingWork()) recovery_timer_->stop();
    });
  }
  if (!recovery_timer_->isActive()) recovery_timer_->start();
}

}  // namespace ember::plane
