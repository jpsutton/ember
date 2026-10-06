// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Plezy's linux/runner/mpv/plane_render_executor.cc (GPL-3.0).

#include "PlaneRenderExecutor.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <chrono>

namespace ember::plane {

PlaneRenderExecutor::PlaneRenderExecutor() : shared_(std::make_shared<Shared>()) {
  thread_ = std::thread(&PlaneRenderExecutor::Run, shared_);
}

PlaneRenderExecutor::~PlaneRenderExecutor() { ShutdownAndJoin(5000); }

bool PlaneRenderExecutor::Post(Job job, Completion completion) {
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->quitting) return false;
    shared_->jobs.emplace_back(std::move(job), std::move(completion));
  }
  shared_->wake.notify_one();
  return true;
}

bool PlaneRenderExecutor::ShutdownAndJoin(unsigned int timeout_ms) {
  if (!thread_.joinable()) return !abandoned_;
  {
    std::unique_lock<std::mutex> lock(shared_->mutex);
    shared_->quitting = true;
    shared_->wake.notify_all();
    if (!shared_->idle.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
          return shared_->jobs.empty() && !shared_->running_job;
        })) {
      abandoned_ = true;
    }
  }
  if (abandoned_) {
    thread_.detach();
    return false;
  }
  thread_.join();
  return true;
}

void PlaneRenderExecutor::Run(const std::shared_ptr<Shared>& shared) {
  for (;;) {
    Job job;
    Completion completion;
    {
      std::unique_lock<std::mutex> lock(shared->mutex);
      shared->wake.wait(lock, [&shared] { return !shared->jobs.empty() || shared->quitting; });
      // Quitting drains: queued jobs still run, so a shutdown-time job (the
      // EGL unbind) can be posted and then waited for.
      if (shared->jobs.empty()) return;
      job = std::move(shared->jobs.front().first);
      completion = std::move(shared->jobs.front().second);
      shared->jobs.pop_front();
      shared->running_job = true;
    }

    const bool result = job ? job() : false;
    if (completion) {
      // The application object lives for the whole process, so the queued
      // call always has a receiver on the GUI thread.
      QMetaObject::invokeMethod(
          QCoreApplication::instance(), [completion = std::move(completion), result]() { completion(result); },
          Qt::QueuedConnection);
    }

    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      shared->running_job = false;
    }
    shared->idle.notify_all();
  }
}

}  // namespace ember::plane
