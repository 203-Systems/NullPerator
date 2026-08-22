/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_SILENT_AUDIO_H
#define PICOTRACKER_WASM_SILENT_AUDIO_H

#include "Services/Audio/Audio.h"

class WasmSilentAudio final : public Audio {
public:
  explicit WasmSilentAudio(AudioSettings &settings);

  void Init() override;
  void Close() override;
  int GetMixerVolume() override;
  void SetMixerVolume(int volume) override;

private:
  bool initialized_ = false;
  int volume_ = 100;
};

#endif
