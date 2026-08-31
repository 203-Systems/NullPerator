/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Services/Audio/Audio.h"

class IOSAudioDriver;

class IOSAudio final : public Audio {
public:
  explicit IOSAudio(AudioSettings &settings);
  void Init() override;
  void Close() override;
  int GetMixerVolume() override;
  void SetMixerVolume(int volume) override;

  static IOSAudioDriver *Driver() noexcept;

private:
  bool initialized_ = false;
  int volume_ = 100;
};
