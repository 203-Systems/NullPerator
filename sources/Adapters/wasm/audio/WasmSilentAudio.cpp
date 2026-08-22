/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmSilentAudio.h"

#include "Adapters/picoTracker/audio/record.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/AudioOutDriver.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

#include <algorithm>
#include <cstdint>
#include <new>

namespace {
class WasmSilentDriver final : public AudioDriver {
public:
  explicit WasmSilentDriver(AudioSettings &settings) : AudioDriver(settings) {}

  bool InitDriver() override { return true; }
  void CloseDriver() override {}
  bool StartDriver() override { return true; }
  void StopDriver() override {}
  bool Interlaced() override { return true; }
  int GetPlayedBufferPercentage() override { return 0; }
  double GetStreamTime() override {
    System *system = System::GetInstance();
    return system == nullptr ? 0.0
                             : static_cast<double>(system->Millis()) / 1000.0;
  }
  void AddBuffer(short *, int) override {}
};
} // namespace

WasmSilentAudio::WasmSilentAudio(AudioSettings &settings) : Audio(settings) {
  settings_ = settings;
}

void WasmSilentAudio::Init() {
  if (initialized_) {
    return;
  }
  AudioSettings settings{};
  settings.audioAPI_ = "silent";
  settings.audioDevice_ = "disconnected";
  settings.bufferSize_ = GetAudioBufferSize();
  settings.preBufferCount_ = GetAudioPreBufferCount();

  alignas(WasmSilentDriver)
      static unsigned char driverStorage[sizeof(WasmSilentDriver)];
  auto *driver = new (driverStorage) WasmSilentDriver(settings);
  alignas(AudioOutDriver)
      static unsigned char outputStorage[sizeof(AudioOutDriver)];
  auto *output = new (outputStorage) AudioOutDriver(*driver);
  AddOutput(*output);
  initialized_ = true;
  Trace::Log("WASM_AUDIO", "silent audio adapter installed");
}

void WasmSilentAudio::Close() {
  for (AudioOut *output : Outputs()) {
    if (output != nullptr) {
      output->Close();
    }
  }
}

int WasmSilentAudio::GetMixerVolume() { return volume_; }

void WasmSilentAudio::SetMixerVolume(int volume) {
  volume_ = std::clamp(volume, 0, 100);
}

void Record(void *) {}
bool StartRecording(const char *, std::uint8_t, std::uint32_t) { return false; }
void StopRecording() {}
void RequestStopRecording() {}
bool WaitForRecordingStop(std::uint32_t) { return true; }
void FinishStopRecording() {}
void StartMonitoring() {}
void StopMonitoring() {}
void SetInputSource(RecordSource) {}
void SetLineInGain(std::uint8_t) {}
void SetMicGain(std::uint8_t) {}
bool IsRecordingActive() { return false; }
bool IsSavingRecording() { return false; }
std::uint8_t GetSavingProgressPercent() { return 0; }
bool DidLastRecordingCaptureAudio() { return false; }
