// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Plezy's linux/runner/mpv/plane_render_executor.h (GPL-3.0).

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace ember::plane {

// The video plane's render worker. mpv's render and the plane's
// eglSwapBuffers run here so an expensive frame never stalls the GUI thread,
// which also runs QML and input. Everything else about the plane (Wayland
// protocol state, timers, geometry) stays on the GUI thread.
//
// Contract:
//  - Jobs run in order on one worker thread and return a bool.
//  - Completions run on the GUI thread with the job's result. They are
//    delivered even after ShutdownAndJoin, so they must check that the state
//    they touch still exists (MpvVideo's generation counter).
//  - ShutdownAndJoin drains queued jobs first. A job stuck inside a driver
//    call can't be interrupted; past the timeout the thread is detached and
//    false is returned so the caller can leak, rather than free, what the job
//    may still touch.
class PlaneRenderExecutor {
 public:
  using Job = std::function<bool()>;
  using Completion = std::function<void(bool)>;

  PlaneRenderExecutor();
  ~PlaneRenderExecutor();

  PlaneRenderExecutor(const PlaneRenderExecutor&) = delete;
  PlaneRenderExecutor& operator=(const PlaneRenderExecutor&) = delete;

  // Queues |job|. Returns false once shutdown has begun; then neither the job
  // nor the completion runs.
  bool Post(Job job, Completion completion);

  // Stops accepting jobs, waits up to |timeout_ms| for queued jobs to drain,
  // and joins the worker. Returns false when the worker had to be abandoned.
  // Idempotent.
  bool ShutdownAndJoin(unsigned int timeout_ms);

 private:
  // Held by shared_ptr from both the executor and the worker, so an abandoned
  // worker still stands on live memory.
  struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<std::pair<Job, Completion>> jobs;
    bool running_job = false;
    bool quitting = false;
  };

  static void Run(const std::shared_ptr<Shared>& shared);

  std::shared_ptr<Shared> shared_;
  std::thread thread_;
  bool abandoned_ = false;
};

}  // namespace ember::plane
