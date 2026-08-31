/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSTimer.h"

#include <algorithm>
#include <chrono>
#include <new>

IOSTimer::~IOSTimer() { Stop(); }

void IOSTimer::SetPeriod(float msec) {
  std::lock_guard lock(mutex_);
  periodMs_ = msec;
  wake_.notify_all();
}

bool IOSTimer::Start() {
  Stop();
  {
    std::lock_guard lock(mutex_);
    if (periodMs_ <= 0.0F) return false;
    running_ = true;
  }
  thread_ = std::thread(&IOSTimer::Run, this);
  return true;
}

void IOSTimer::Stop() {
  {
    std::lock_guard lock(mutex_);
    running_ = false;
  }
  wake_.notify_all();
  if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
    thread_.join();
  }
}

float IOSTimer::GetPeriod() {
  std::lock_guard lock(mutex_);
  return periodMs_;
}

void IOSTimer::Run() {
  std::unique_lock lock(mutex_);
  while (running_) {
    const auto delay = std::chrono::duration<double, std::milli>(periodMs_);
    if (wake_.wait_for(lock, delay, [this] { return !running_; })) break;
    lock.unlock();
    SetChanged();
    NotifyObservers();
    lock.lock();
  }
}

I_Timer *IOSTimerService::CreateTimer() {
  return new (std::nothrow) IOSTimer();
}

void IOSTimerService::TriggerCallback(int msec, timerCallback callback) {
  if (callback == nullptr) return;
  std::thread([delay = std::max(msec, 0), callback] {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    callback();
  }).detach();
}
