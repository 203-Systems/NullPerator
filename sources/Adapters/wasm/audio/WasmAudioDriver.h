/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/PcmRingBuffer.h"
#include "WasmAudioState.h"

#include <array>
#include <atomic>
#include <cstdint>

class WasmAudioWorkletRenderer;

class WasmAudioDriver final : public AudioDriver {
public:
  static constexpr std::size_t RingCapacityFrames = 16384U;
  static constexpr std::size_t TargetFillFrames = 4096U;
  static constexpr std::uint32_t MinimumTargetFillFrames = 512U;
  static constexpr std::uint32_t MaximumTargetFillFrames = 8192U;
  static constexpr std::uint32_t UnityGainQ16 = 1U << 16;

  explicit WasmAudioDriver(AudioSettings &settings);

  bool InitDriver() override;
  void CloseDriver() override;
  bool StartDriver() override;
  void StopDriver() override;
  bool Interlaced() override;
  int GetPlayedBufferPercentage() override;
  double GetStreamTime() override;
  std::span<short> GetOutputBuffer() override { return outputBuffer_; }
  void AddBuffer(short *buffer, int samplecount) override;
  void OnAudioActive(bool active) override;

  void PumpProducer() noexcept;
  [[nodiscard]] std::size_t ReadFrames(std::span<StereoF32> output) noexcept;
  [[nodiscard]] bool IsActive() const noexcept;
  [[nodiscard]] bool IsStarted() const noexcept;
  // Browser graph teardown is irreversible for this driver instance. Stop
  // producer ticks immediately while the application-thread shutdown follows.
  void DisableProducerForTeardown() noexcept;
  void SetDestinationRate(std::uint32_t rate) noexcept;
  void Configure(std::uint32_t targetFillFrames,
                 std::uint32_t outputGainQ16) noexcept;
  void SetMixerVolume(int volume) noexcept;
  [[nodiscard]] std::uint32_t TargetFillFramesConfigured() const noexcept;
  [[nodiscard]] std::uint32_t OutputGainQ16() const noexcept;
  [[nodiscard]] WasmAudioMetrics Metrics() const noexcept;
  // Realtime-safe callback instrumentation: fixed lock-free atomics only.
  // callbackMilliseconds is measured around the successful AudioWorklet
  // workload; frames selects the exact browser quantum deadline for this
  // callback.
  void RecordCallback(double callbackMilliseconds, std::size_t frames) noexcept;

  static WasmAudioDriver *Instance() noexcept;

private:
  static constexpr std::uint32_t HostOutputGainBits = 17U;
  static constexpr std::uint32_t HostOutputGainMask =
      (1U << HostOutputGainBits) - 1U;
  static constexpr std::uint32_t MixerVolumeShift = HostOutputGainBits;
  static constexpr std::uint32_t InitialOutputGainState =
      UnityGainQ16 | (100U << MixerVolumeShift);

  static std::uint32_t Saturating(std::uint64_t value) noexcept;
  void SetHostOutputGainQ16(std::uint32_t gain) noexcept;

  PcmRingBuffer<RingCapacityFrames> ring_;
  std::array<short, MAX_SAMPLE_COUNT * 2U> outputBuffer_;
  std::atomic<bool> started_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> producerEnabled_{true};
  std::atomic<std::uint32_t> destinationRate_{0U};
  std::atomic<std::uint32_t> targetFillFrames_{TargetFillFrames};
  // The browser-host gain and tracker Device volume share one atomic word.
  // This keeps concurrent updates independent without letting either writer
  // overwrite the other writer's component. OutputGainQ16 derives the
  // effective gain from one coherent snapshot.
  std::atomic<std::uint32_t> outputGainState_{InitialOutputGainState};
  std::atomic<std::uint32_t> callbackCount_{0U};
  std::atomic<std::uint32_t> renderMicros_{0U};
  std::atomic<std::uint32_t> callbackMicros_{0U};
  std::atomic<std::uint32_t> callbackMaxMicros_{0U};
  std::atomic<std::uint32_t> callbackDeadlineMicros_{0U};
  std::atomic<std::uint32_t> callbackDeadlineMisses_{0U};
  std::atomic<std::uint64_t> producerFrames_{0U};
  static std::atomic<WasmAudioDriver *> instance_;

  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "AudioWorklet state requires lock-free 32-bit atomics");
};
