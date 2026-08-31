/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Adapters/wasm/audio/PcmRingBuffer.h"
#include "Services/Audio/AudioDriver.h"

#include <AudioToolbox/AudioToolbox.h>
#include <atomic>

class IOSAudioDriver final : public AudioDriver {
public:
  static constexpr std::size_t RingCapacityFrames = 16384U;
  static constexpr std::size_t TargetFillFrames = 4096U;

  explicit IOSAudioDriver(AudioSettings &settings);
  ~IOSAudioDriver() override;

  bool InitDriver() override;
  void CloseDriver() override;
  bool StartDriver() override;
  void StopDriver() override;
  bool Interlaced() override;
  int GetPlayedBufferPercentage() override;
  double GetStreamTime() override;
  void AddBuffer(short *buffer, int samplecount) override;
  void OnAudioActive(bool active) override;

  void PumpProducer() noexcept;

private:
  static OSStatus Render(void *context, AudioUnitRenderActionFlags *,
                         const AudioTimeStamp *, UInt32, UInt32 frames,
                         AudioBufferList *buffers);
  OSStatus Render(UInt32 frames, AudioBufferList *buffers) noexcept;

  PcmRingBuffer<RingCapacityFrames> ring_;
  AudioComponentInstance unit_ = nullptr;
  std::atomic<bool> started_{false};
  std::atomic<bool> active_{false};
  std::atomic<std::uint64_t> consumedFrames_{0U};
};
