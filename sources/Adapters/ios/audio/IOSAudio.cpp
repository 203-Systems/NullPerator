/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSAudio.h"

#include "IOSAudioDriver.h"
#include "Services/Audio/AudioOutDriver.h"

#include <algorithm>
#include <new>

namespace {
alignas(IOSAudioDriver) unsigned char driverStorage[sizeof(IOSAudioDriver)];
alignas(AudioOutDriver) unsigned char outputStorage[sizeof(AudioOutDriver)];
IOSAudioDriver *driver = nullptr;
AudioOutDriver *output = nullptr;
} // namespace

IOSAudio::IOSAudio(AudioSettings &settings) : Audio(settings) {
  settings_ = settings;
}

void IOSAudio::Init() {
  if (initialized_) return;
  driver = new (driverStorage) IOSAudioDriver(settings_);
  output = new (outputStorage) AudioOutDriver(*driver);
  AddOutput(*output);
  initialized_ = true;
}

void IOSAudio::Close() {
  if (!initialized_) return;
  if (output != nullptr) output->Close();
}

int IOSAudio::GetMixerVolume() { return volume_; }

void IOSAudio::SetMixerVolume(int volume) {
  volume_ = std::clamp(volume, 0, 100);
}

IOSAudioDriver *IOSAudio::Driver() noexcept { return driver; }
