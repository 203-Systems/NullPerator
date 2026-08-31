/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "AudioDriver.h"

AudioDriver::AudioDriver(AudioSettings &settings) { settings_ = settings; }

AudioDriver::~AudioDriver() {}

bool AudioDriver::Init() { return InitDriver(); }

void AudioDriver::Close() { CloseDriver(); };

bool AudioDriver::Start() { return StartDriver(); };

void AudioDriver::Stop() { StopDriver(); }

void AudioDriver::OnNewBufferNeeded() {
  SetChanged();
  Event event(Event::ADET_BUFFERNEEDED);
  NotifyObservers(&event);
};

void AudioDriver::onAudioBufferTick() {
  SetChanged();
  Event event(Event::ADET_DRIVERTICK);
  NotifyObservers(&event);
}

AudioSettings AudioDriver::GetAudioSettings() { return settings_; };
