/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "AudioOutDriver.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <algorithm>

fixed AudioOutDriver::primarySoundBuffer_[MIX_BUFFER_SIZE];

AudioOutDriver::AudioOutDriver(AudioDriver &driver) {
  driver_ = &driver;
  driver.AddObserver(*this);
}

AudioOutDriver::~AudioOutDriver() {
  driver_->RemoveObserver(*this);
  // Audio drivers are owned by the platform adapter. Embedded adapters place
  // them in static storage, so deleting the borrowed pointer is invalid.
};

bool AudioOutDriver::Init() { return driver_->Init(); };

void AudioOutDriver::Close() { driver_->Close(); }

bool AudioOutDriver::Start() {
  sampleCount_ = 0;
  return driver_->Start();
}

void AudioOutDriver::Stop() {
  SetAudioActive(false);
  driver_->Stop();
}

void AudioOutDriver::SetAudioActive(bool active) {
  driver_->OnAudioActive(active);
}

void AudioOutDriver::Trigger() {
  prepareMixBuffers();
  const auto output = driver_->GetOutputBuffer();
  if (output.data() == nullptr ||
      output.size() < static_cast<std::size_t>(sampleCount_) * 2U)
    return;
  hasSound_ = AudioMixer::Render(primarySoundBuffer_, sampleCount_) > 0;
  clipToMix(output.data());
  driver_->AddBuffer(output.data(), sampleCount_);
}

void AudioOutDriver::Update(Observable &o, I_ObservableData *d) {
  SetChanged();
  NotifyObservers(d);
}

void AudioOutDriver::prepareMixBuffers() {
  sampleCount_ = getPlaySampleCount();
  if (sampleCount_ > MAX_SAMPLE_COUNT) {
    static bool loggedOversizeBuffer = false;
    if (!loggedOversizeBuffer) {
      Trace::Error("AUDIO_OUT", "Sample count %d exceeds max %d, clamping",
                   sampleCount_, MAX_SAMPLE_COUNT);
      loggedOversizeBuffer = true;
    }
    sampleCount_ = MAX_SAMPLE_COUNT;
  }
  if (sampleCount_ < 0) {
    sampleCount_ = 0;
  }
};

void AudioOutDriver::clipToMix(short *destination) {
  if (!hasSound_) {
    memset(destination, 0, sampleCount_ * 2 * sizeof(short));
    return;
  }

  fixed *src = primarySoundBuffer_;

  if (driver_->Interlaced()) {
    short *dst = destination;
    int i = 0;
    for (; i + 4 <= sampleCount_; i += 4) {
      int l0 = fp2i(src[0]);
      int r0 = fp2i(src[1]);
      int l1 = fp2i(src[2]);
      int r1 = fp2i(src[3]);
      int l2 = fp2i(src[4]);
      int r2 = fp2i(src[5]);
      int l3 = fp2i(src[6]);
      int r3 = fp2i(src[7]);

      dst[0] = static_cast<short>(std::clamp(l0, -32768, 32767));
      dst[1] = static_cast<short>(std::clamp(r0, -32768, 32767));
      dst[2] = static_cast<short>(std::clamp(l1, -32768, 32767));
      dst[3] = static_cast<short>(std::clamp(r1, -32768, 32767));
      dst[4] = static_cast<short>(std::clamp(l2, -32768, 32767));
      dst[5] = static_cast<short>(std::clamp(r2, -32768, 32767));
      dst[6] = static_cast<short>(std::clamp(l3, -32768, 32767));
      dst[7] = static_cast<short>(std::clamp(r3, -32768, 32767));

      src += 8;
      dst += 8;
    }
    for (; i < sampleCount_; ++i) {
      int l = fp2i(src[0]);
      int r = fp2i(src[1]);
      dst[0] = static_cast<short>(std::clamp(l, -32768, 32767));
      dst[1] = static_cast<short>(std::clamp(r, -32768, 32767));
      src += 2;
      dst += 2;
    }
  } else {
    short *left = destination;
    short *right = left + sampleCount_;
    for (int i = 0; i < sampleCount_; ++i) {
      int l = fp2i(*src++);
      int r = fp2i(*src++);
      *left++ = static_cast<short>(std::clamp(l, -32768, 32767));
      *right++ = static_cast<short>(std::clamp(r, -32768, 32767));
    }
  }
};

int AudioOutDriver::GetPlayedBufferPercentage() {
  return driver_->GetPlayedBufferPercentage();
};

AudioDriver *AudioOutDriver::GetDriver() { return driver_; };

etl::string<STRING_AUDIO_API_MAX> AudioOutDriver::GetAudioAPI() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.audioAPI_;
};

etl::string<STRING_AUDIO_DEVICE_MAX> AudioOutDriver::GetAudioDevice() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.audioDevice_;
};
int AudioOutDriver::GetAudioBufferSize() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.bufferSize_;
};

int AudioOutDriver::GetAudioRequestedBufferSize() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.bufferSize_;
}

int AudioOutDriver::GetAudioPreBufferCount() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.preBufferCount_;
};
double AudioOutDriver::GetStreamTime() { return driver_->GetStreamTime(); };
