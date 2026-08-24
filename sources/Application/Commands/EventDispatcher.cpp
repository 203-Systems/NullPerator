/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "EventDispatcher.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"

int EventDispatcher::keyRepeat_ = 30;
int EventDispatcher::keyDelay_ = 500;

EventDispatcher::EventDispatcher() {
  sink_ = nullptr;
  eventMask_ = 0;

  // Read config file key repeat

  Config *config = Config::GetInstance();
  keyDelay_ = config->GetValue("KEYDELAY");
  keyRepeat_ = config->GetValue("KEYREPEAT");

  repeatMask_ = 0;
  repeatMask_ |= TrackerActionBit(TrackerAction::Left);
  repeatMask_ |= TrackerActionBit(TrackerAction::Right);
  repeatMask_ |= TrackerActionBit(TrackerAction::Up);
  repeatMask_ |= TrackerActionBit(TrackerAction::Down);

  timer_ = TimerService::GetInstance()->CreateTimer();
  timer_->AddObserver(*this);
};

EventDispatcher::~EventDispatcher() {
  timer_->RemoveObserver(*this);
  timer_->Stop();
  timer_ = nullptr;
};

void EventDispatcher::Execute(FourCC id, float value) {

  if (sink_) {
    TrackerAction mapping = TrackerAction::Count;
    switch (id) {
    case FourCC::TrigEventEnter:
      mapping = TrackerAction::Enter;
      break;
    case FourCC::TrigEventEdit:
      mapping = TrackerAction::Edit;
      break;
    case FourCC::TrigEventLeft:
      mapping = TrackerAction::Left;
      break;
    case FourCC::TrigEventRight:
      mapping = TrackerAction::Right;
      break;
    case FourCC::TrigEventUp:
      mapping = TrackerAction::Up;
      break;
    case FourCC::TrigEventDown:
      mapping = TrackerAction::Down;
      break;
    case FourCC::TrigEventAlt:
      mapping = TrackerAction::Alt;
      break;
    case FourCC::TrigEventNav:
      mapping = TrackerAction::Nav;
      break;
    case FourCC::TrigEventPlay:
      mapping = TrackerAction::Play;
      break;
    default:
      return;
    }

    // Compute mask and repeat if needed
    const unsigned int bit = TrackerActionBit(mapping);
    if (value > 0.5) {
      eventMask_ |= bit;
    } else {
      eventMask_ &= ~bit;
    }

    sink_->DispatchTrackerAction(mapping, value > 0.5);

    if (eventMask_ & repeatMask_) {
      timer_->SetPeriod(float(keyDelay_));
      timer_->Start();
    } else {
      timer_->Stop();
    }
  };
};

void EventDispatcher::SetSink(ITrackerInputSink *sink) { sink_ = sink; }

unsigned int EventDispatcher::OnTimerTick() {

  unsigned sendMask = (eventMask_ & repeatMask_);
  if (sendMask) {
    int current = 0;
    while (sendMask) {
      if (sendMask & 1) {
        sink_->DispatchTrackerAction(static_cast<TrackerAction>(current), true);
      }
      sendMask >>= 1;
      current++;
    }
    return keyRepeat_;
  }
  return 0;
};

void EventDispatcher::Update(Observable &o, I_ObservableData *d) {
  unsigned int tick = OnTimerTick();
  if (tick) {
    timer_->SetPeriod(float(tick));
  } else {
    timer_->Stop();
  };
};
