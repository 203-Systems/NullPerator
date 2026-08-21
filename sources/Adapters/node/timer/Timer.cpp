#include "Timer.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/System/System.h"
#include "esp_log.h"
#include <new>

namespace {
constexpr const char *kTimerTag = "NODE_TIMER";

struct TriggerContext {
  timerCallback callback;
  esp_timer_handle_t timer;
};
} // namespace

// Callback function for periodic timer
void NodeTimerCallback(void *param) {
  NodeTimer *timer = static_cast<NodeTimer *>(param);
  timer->OnTimerTick();
}

// Callback function for one-shot triggers
void NodeTriggerCallback(void *param) {
  TriggerContext *context = static_cast<TriggerContext *>(param);
  timerCallback callback = context->callback;
  esp_timer_handle_t timer = context->timer;
  callback();
  const esp_err_t err = esp_timer_delete(timer);
  if (err != ESP_OK) {
    ESP_LOGE(kTimerTag, "Failed to delete one-shot timer: %s",
             esp_err_to_name(err));
  }
  delete context;
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
  if (cb == nullptr) {
    ESP_LOGE(kTimerTag, "Cannot create one-shot timer without a callback");
    return;
  }

  TriggerContext *context = new (std::nothrow) TriggerContext{cb, nullptr};
  if (context == nullptr) {
    ESP_LOGE(kTimerTag, "Failed to allocate one-shot timer context");
    return;
  }

  esp_timer_create_args_t trigger_args = {
      .callback = &NodeTriggerCallback,
      .arg = context,
      .name = "NodeTrigger"
  };

  esp_err_t err = esp_timer_create(&trigger_args, &context->timer);
  if (err != ESP_OK) {
    ESP_LOGE(kTimerTag, "Failed to create one-shot timer: %s",
             esp_err_to_name(err));
    delete context;
    return;
  }

  err = esp_timer_start_once(context->timer, static_cast<int64_t>(msec) * 1000);
  if (err != ESP_OK) {
    ESP_LOGE(kTimerTag, "Failed to start one-shot timer: %s",
             esp_err_to_name(err));
    esp_timer_delete(context->timer);
    delete context;
  }
}
