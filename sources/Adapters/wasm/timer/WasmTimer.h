/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_TIMER_H
#define PICOTRACKER_WASM_TIMER_H

#include "System/Timer/Timer.h"

#include <atomic>
#include <cstdint>
#include <functional>

class WasmTimer final : public I_Timer {
public:
  using TimerId = std::uint32_t;
  using ScheduleFunction =
      std::function<TimerId(double, std::function<void()>)>;
  using CancelFunction = std::function<void(TimerId)>;

  WasmTimer();
  WasmTimer(ScheduleFunction schedule, CancelFunction cancel);
  ~WasmTimer() override;

  void SetPeriod(float msec) override;
  bool Start() override;
  void Stop() override;
  float GetPeriod() override;

private:
  bool ScheduleNext(std::uint64_t generation);
  void OnScheduledTick(std::uint64_t generation);

  float period_ = -1.0F;
  ScheduleFunction schedule_;
  CancelFunction cancel_;
  TimerId timerId_ = 0;
  std::atomic<std::uint64_t> generation_{0};
  std::atomic<bool> running_{false};
};

class WasmTimerService final : public TimerService {
public:
  I_Timer *CreateTimer() override;
  void TriggerCallback(int msec, timerCallback callback) override;
};

#endif
