/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "AudioOut.h"
#include <cmath>

AudioOut::AudioOut() : AudioMixer("AudioOut"), sampleOffset_(0) {};

AudioOut::~AudioOut() {};

int AudioOut::getPlaySampleCount() {
  const float frames = frameClock_ ? frameClock_() : 0.0F;
  if (!std::isfinite(frames) || frames <= 0.0F || frames > MAX_SAMPLE_COUNT)
    return 0;
  sampleOffset_ += frames;
  int count = int(sampleOffset_);
  sampleOffset_ -= count;
  return count;
};