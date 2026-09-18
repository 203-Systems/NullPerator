/* SPDX-License-Identifier: BSD-3-Clause */

#include "AndroidAudio.h"

#include "AndroidAudioDriver.h"
#include "Services/Audio/AudioOutDriver.h"

#include <algorithm>
#include <new>

namespace {
alignas(AndroidAudioDriver) unsigned char driverStorage[sizeof(AndroidAudioDriver)];
alignas(AudioOutDriver) unsigned char outputStorage[sizeof(AudioOutDriver)];
AndroidAudioDriver *driver = nullptr;
AudioOutDriver *output = nullptr;
} // namespace

AndroidAudio::AndroidAudio(AudioSettings &settings) : Audio(settings) {
  settings_ = settings;
}

void AndroidAudio::Init() {
  if (initialized_)
    return;
  driver = new (driverStorage) AndroidAudioDriver(settings_);
  output = new (outputStorage) AudioOutDriver(*driver);
  AddOutput(*output);
  initialized_ = true;
}

void AndroidAudio::Close() {
  if (!initialized_)
    return;
  if (output != nullptr)
    output->Close();
}

int AndroidAudio::GetMixerVolume() { return volume_; }

void AndroidAudio::SetMixerVolume(int volume) {
  volume_ = std::clamp(volume, 0, 100);
}

AndroidAudioDriver *AndroidAudio::Driver() noexcept { return driver; }
