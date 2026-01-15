#ifndef _NODETIMER_H_
#define _NODETIMER_H_

#include "System/Timer/Timer.h"
#include "esp_timer.h"

class NodeTimer : public I_Timer {
public:
  NodeTimer();
  virtual ~NodeTimer();
  virtual void SetPeriod(float msec);
  virtual bool Start();
  virtual void Stop();
  virtual float GetPeriod();
  int64_t OnTimerTick();

private:
  float period_;
  float offset_;      // Float offset taking into account
                      // period is an int
  esp_timer_handle_t timer_; // Timer handle (ESP-IDF equivalent to alarm_id_t)
  long lastTick_;
  bool running_;
};

class NodeTimerService : public TimerService {
public:
  virtual I_Timer *CreateTimer(); // Returns a timer
  virtual void TriggerCallback(int msec, timerCallback cb);
};

#endif
