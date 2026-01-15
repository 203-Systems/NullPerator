#include "Timer.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/System/System.h"

// Callback function for periodic timer
void NodeTimerCallback(void *param) {
  NodeTimer *timer = static_cast<NodeTimer *>(param);
  timer->OnTimerTick();
}

// Callback function for one-shot triggers
void NodeTriggerCallback(void *param) {
  timerCallback tc = reinterpret_cast<timerCallback>(param);
  (*tc)();
}

NodeTimer::NodeTimer() {
  period_ = -1;
  timer_ = nullptr;
  running_ = false;
}

NodeTimer::~NodeTimer() {
  Stop();
}

void NodeTimer::SetPeriod(float msec) {
  period_ = msec;
  offset_ = 0;
}

bool NodeTimer::Start() {
  if (period_ > 0) {
    Stop(); // Ensure previous timer is stopped

    esp_timer_create_args_t timer_args = {
        .callback = &NodeTimerCallback,
        .arg = this,
        .name = "NodeTimer"
    };

    esp_timer_create(&timer_args, &timer_);
    esp_timer_start_periodic(timer_, static_cast<int64_t>(period_ * 1000)); // Convert ms to us
    lastTick_ = System::GetInstance()->GetClock();
    running_ = true;
  }
  return (timer_ != nullptr);
}

void NodeTimer::Stop() {
  if (timer_) {
    esp_timer_stop(timer_);
    esp_timer_delete(timer_);
    timer_ = nullptr;
  }
  running_ = false;
}

float NodeTimer::GetPeriod() {
  return period_;
}

int64_t NodeTimer::OnTimerTick() {
  if (running_) {
    SetChanged();
    NotifyObservers();
    offset_ += period_;
    return static_cast<int64_t>(offset_ * 1000); // Return next period in microseconds
  }
  return 0;
}

I_Timer *NodeTimerService::CreateTimer() {
  return new NodeTimer();
}

void NodeTimerService::TriggerCallback(int msec, timerCallback cb) {
  esp_timer_create_args_t trigger_args = {
      .callback = &NodeTriggerCallback,
      .arg = reinterpret_cast<void *>(cb),
      .name = "NodeTrigger"
  };

  esp_timer_handle_t trigger_timer;
  esp_timer_create(&trigger_args, &trigger_timer);
  esp_timer_start_once(trigger_timer, static_cast<int64_t>(msec * 1000)); // Convert ms to us
}
