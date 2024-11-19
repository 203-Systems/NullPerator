#include "picoTrackerTimer.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/System/System.h"

// Callback function for periodic timer
void picoTrackerTimerCallback(void *param) {
  picoTrackerTimer *timer = static_cast<picoTrackerTimer *>(param);
  timer->OnTimerTick();
}

// Callback function for one-shot triggers
void picoTrackerTriggerCallback(void *param) {
  timerCallback tc = reinterpret_cast<timerCallback>(param);
  (*tc)();
}

picoTrackerTimer::picoTrackerTimer() {
  period_ = -1;
  timer_ = nullptr;
  running_ = false;
}

picoTrackerTimer::~picoTrackerTimer() {
  Stop();
}

void picoTrackerTimer::SetPeriod(float msec) {
  period_ = msec;
  offset_ = 0;
}

bool picoTrackerTimer::Start() {
  if (period_ > 0) {
    Stop(); // Ensure previous timer is stopped

    esp_timer_create_args_t timer_args = {
        .callback = &picoTrackerTimerCallback,
        .arg = this,
        .name = "picoTrackerTimer"
    };

    esp_timer_create(&timer_args, &timer_);
    esp_timer_start_periodic(timer_, static_cast<int64_t>(period_ * 1000)); // Convert ms to us
    lastTick_ = System::GetInstance()->GetClock();
    running_ = true;
  }
  return (timer_ != nullptr);
}

void picoTrackerTimer::Stop() {
  if (timer_) {
    esp_timer_stop(timer_);
    esp_timer_delete(timer_);
    timer_ = nullptr;
  }
  running_ = false;
}

float picoTrackerTimer::GetPeriod() {
  return period_;
}

int64_t picoTrackerTimer::OnTimerTick() {
  if (running_) {
    SetChanged();
    NotifyObservers();
    offset_ += period_;
    return static_cast<int64_t>(offset_ * 1000); // Return next period in microseconds
  }
  return 0;
}

I_Timer *picoTrackerTimerService::CreateTimer() {
  return new picoTrackerTimer();
}

void picoTrackerTimerService::TriggerCallback(int msec, timerCallback cb) {
  esp_timer_create_args_t trigger_args = {
      .callback = &picoTrackerTriggerCallback,
      .arg = reinterpret_cast<void *>(cb),
      .name = "picoTrackerTrigger"
  };

  esp_timer_handle_t trigger_timer;
  esp_timer_create(&trigger_args, &trigger_timer);
  esp_timer_start_once(trigger_timer, static_cast<int64_t>(msec * 1000)); // Convert ms to us
}
