/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Services/Audio/Audio.h"

class AndroidAudioDriver;

class AndroidAudio final : public Audio {
public:
  explicit AndroidAudio(AudioSettings &settings);
  void Init() override;
  void Close() override;
  int GetMixerVolume() override;
  void SetMixerVolume(int volume) override;

  static AndroidAudioDriver *Driver() noexcept;

private:
  bool initialized_ = false;
  int volume_ = 100;
};
