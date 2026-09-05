/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSTimer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <new>

namespace {
bool ValidPeriod(float msec) {
  return std::isfinite(msec) && msec > 0.0F &&
         static_cast<double>(msec) <= std::numeric_limits<std::int32_t>::max();
}
} // namespace

// Control calls are serialized by the owner. Observers may change the period,
// Stop or Start from a callback, but destruction must happen on the owner.
IOSTimer::~IOSTimer() {
  Stop();
  {
    std::lock_guard lock(mutex_);
    shutdown_ = true;
    wake_.notify_all();
  }
  if (thread_.joinable())
    thread_.join();
}

void IOSTimer::SetPeriod(float msec) {
  std::lock_guard lock(mutex_);
  periodMs_ = ValidPeriod(msec) ? msec : -1.0F;
  if (!ValidPeriod(periodMs_))
    running_ = false;
  ++generation_;
  wake_.notify_all();
}

bool IOSTimer::Start() {
  std::lock_guard lock(mutex_);
  if (stopping_ || shutdown_ || !ValidPeriod(periodMs_))
    return false;
  running_ = true;
  ++generation_;
  // Keep one idle worker between runs. Restart from an observer must never
  // assign to a joinable std::thread or try to join itself.
  if (!thread_.joinable())
    thread_ = std::thread(&IOSTimer::Run, this);
  wake_.notify_all();
  return true;
}

void IOSTimer::Stop() {
  std::unique_lock lock(mutex_);
  running_ = false;
  ++generation_;
  wake_.notify_all();
  if (thread_.get_id() == std::this_thread::get_id())
    return;
  stopping_ = true;
  wake_.wait(lock, [this] { return !callbackActive_; });
  stopping_ = false;
}

float IOSTimer::GetPeriod() {
  std::lock_guard lock(mutex_);
  return periodMs_;
}

void IOSTimer::Run() {
  std::unique_lock lock(mutex_);
  while (!shutdown_) {
    wake_.wait(lock, [this] { return shutdown_ || running_; });
    if (shutdown_)
      break;
    const auto generation = generation_;
    const auto delay = std::chrono::duration<double, std::milli>(periodMs_);
    if (wake_.wait_for(lock, delay, [this, generation] {
          return shutdown_ || !running_ || generation_ != generation;
        }))
      continue;
    callbackActive_ = true;
    lock.unlock();
    SetChanged();
    NotifyObservers();
    lock.lock();
    callbackActive_ = false;
    wake_.notify_all();
  }
}

I_Timer *IOSTimerService::CreateTimer() {
  return new (std::nothrow) IOSTimer();
}

void IOSTimerService::TriggerCallback(int msec, timerCallback callback) {
  if (callback == nullptr)
    return;
  std::thread([delay = std::max(msec, 0), callback] {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    callback();
  }).detach();
}
