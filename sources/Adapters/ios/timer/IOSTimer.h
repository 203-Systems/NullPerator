/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "System/Timer/Timer.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class IOSTimer final : public I_Timer {
public:
  ~IOSTimer() override;
  void SetPeriod(float msec) override;
  bool Start() override;
  void Stop() override;
  float GetPeriod() override;

private:
  void Run();
  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread thread_;
  float periodMs_ = -1.0F;
  std::uint64_t generation_ = 0;
  bool running_ = false;
  bool callbackActive_ = false;
  bool stopping_ = false;
  bool shutdown_ = false;
};

class IOSTimerService final : public TimerService {
public:
  I_Timer *CreateTimer() override;
  void TriggerCallback(int msec, timerCallback callback) override;
};
