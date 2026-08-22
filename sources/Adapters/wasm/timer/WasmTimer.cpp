/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmTimer.h"

#include "System/Console/Trace.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#endif

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>

namespace {
#ifdef __EMSCRIPTEN__
struct OneShotContext {
  timerCallback callback;
};

struct BrowserTimerContext {
  WasmTimer::TimerId timerId = 0;
  std::function<void()> callback;
};

std::mutex browserTimersMutex;
std::unordered_map<WasmTimer::TimerId, std::unique_ptr<BrowserTimerContext>>
    browserTimers;

void RunOneShot(void *argument) {
  auto *context = static_cast<OneShotContext *>(argument);
  timerCallback callback = context->callback;
  delete context;
  callback();
}

void RunBrowserTimer(void *argument) {
  const auto *scheduled = static_cast<BrowserTimerContext *>(argument);
  std::unique_ptr<BrowserTimerContext> context;
  {
    std::lock_guard<std::mutex> lock(browserTimersMutex);
    const auto iterator = std::find_if(
        browserTimers.begin(), browserTimers.end(),
        [scheduled](const auto &entry) {
          return entry.second.get() == scheduled;
        });
    if (iterator == browserTimers.end()) {
      return;
    }
    context = std::move(iterator->second);
    browserTimers.erase(iterator);
  }
  context->callback();
}

WasmTimer::TimerId ScheduleBrowserTimer(double delay,
                                        std::function<void()> callback) {
  auto context = std::make_unique<BrowserTimerContext>(
      BrowserTimerContext{0, std::move(callback)});
  BrowserTimerContext *argument = context.get();
  const int timerId = emscripten_set_timeout(RunBrowserTimer, delay, argument);
  if (timerId <= 0) {
    return 0;
  }
  context->timerId = static_cast<WasmTimer::TimerId>(timerId);
  {
    std::lock_guard<std::mutex> lock(browserTimersMutex);
    browserTimers.emplace(context->timerId, std::move(context));
  }
  return static_cast<WasmTimer::TimerId>(timerId);
}

void CancelBrowserTimer(WasmTimer::TimerId timerId) {
  emscripten_clear_timeout(static_cast<int>(timerId));
  std::lock_guard<std::mutex> lock(browserTimersMutex);
  browserTimers.erase(timerId);
}
#else
WasmTimer::TimerId ScheduleBrowserTimer(double, std::function<void()>) {
  return 0;
}

void CancelBrowserTimer(WasmTimer::TimerId) {}
#endif
} // namespace

WasmTimer::WasmTimer() : WasmTimer(ScheduleBrowserTimer, CancelBrowserTimer) {}

WasmTimer::WasmTimer(ScheduleFunction schedule, CancelFunction cancel)
    : schedule_(std::move(schedule)), cancel_(std::move(cancel)) {}

WasmTimer::~WasmTimer() { Stop(); }

void WasmTimer::SetPeriod(float msec) {
  const bool restart = running_.load(std::memory_order_acquire);
  if (restart) {
    Stop();
  }
  period_ = msec;
  if (restart && period_ > 0.0F) {
    (void)Start();
  }
}

bool WasmTimer::Start() {
  Stop();
  if (period_ <= 0.0F) {
    return false;
  }

  running_.store(true, std::memory_order_release);
  const std::uint64_t generation =
      generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  return ScheduleNext(generation);
}

void WasmTimer::Stop() {
  running_.store(false, std::memory_order_release);
  generation_.fetch_add(1, std::memory_order_acq_rel);
  const TimerId timerId = std::exchange(timerId_, 0);
  if (timerId == 0) {
    return;
  }
  if (cancel_) {
    cancel_(timerId);
    return;
  }
}

float WasmTimer::GetPeriod() { return period_; }

bool WasmTimer::ScheduleNext(std::uint64_t generation) {
  if (!schedule_) {
    running_.store(false, std::memory_order_release);
    return false;
  }
  timerId_ = schedule_(period_, [this, generation] {
    OnScheduledTick(generation);
  });
  if (timerId_ == 0) {
    running_.store(false, std::memory_order_release);
    return false;
  }
  return true;
}

void WasmTimer::OnScheduledTick(std::uint64_t generation) {
  if (!running_.load(std::memory_order_acquire) ||
      generation != generation_.load(std::memory_order_acquire)) {
    return;
  }
  timerId_ = 0;
  SetChanged();
  NotifyObservers();
  if (running_.load(std::memory_order_acquire) &&
      generation == generation_.load(std::memory_order_acquire) &&
      timerId_ == 0) {
    (void)ScheduleNext(generation);
  }
}

I_Timer *WasmTimerService::CreateTimer() {
  return new (std::nothrow) WasmTimer();
}

void WasmTimerService::TriggerCallback(int msec, timerCallback callback) {
  if (callback == nullptr) {
    Trace::Error("WASM_TIMER", "one-shot callback is null");
    return;
  }
#ifdef __EMSCRIPTEN__
  auto *context = new (std::nothrow) OneShotContext{callback};
  if (context == nullptr) {
    Trace::Error("WASM_TIMER", "failed to allocate one-shot callback");
    return;
  }
  emscripten_async_call(RunOneShot, context,
                        static_cast<int>(std::max(msec, 0)));
#else
  (void)msec;
  Trace::Error("WASM_TIMER", "one-shot callback requires Emscripten");
#endif
}
