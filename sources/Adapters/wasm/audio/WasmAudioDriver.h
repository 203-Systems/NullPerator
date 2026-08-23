/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "PcmRingBuffer.h"
#include "WasmAudioState.h"
#include "Services/Audio/AudioDriver.h"

#include <atomic>
#include <cstdint>

class WasmAudioWorkletRenderer;

class WasmAudioDriver final : public AudioDriver {
public:
  static constexpr std::size_t RingCapacityFrames = 16384U;
  static constexpr std::size_t TargetFillFrames = 4096U;

  explicit WasmAudioDriver(AudioSettings &settings);

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
  [[nodiscard]] std::size_t ReadFrames(std::span<StereoF32> output) noexcept;
  [[nodiscard]] bool IsActive() const noexcept;
  [[nodiscard]] bool IsStarted() const noexcept;
  void SetWorkletRunning(bool running) noexcept;
  void SetDestinationRate(std::uint32_t rate) noexcept;
  [[nodiscard]] WasmAudioMetrics Metrics() const noexcept;
  void RecordCallback() noexcept;

  static WasmAudioDriver *Instance() noexcept;

private:
  static std::uint32_t Saturating(std::uint64_t value) noexcept;

  PcmRingBuffer<RingCapacityFrames> ring_;
  std::atomic<bool> started_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> workletRunning_{false};
  std::atomic<std::uint32_t> destinationRate_{0U};
  std::atomic<std::uint32_t> callbackCount_{0U};
  std::atomic<std::uint32_t> renderMicros_{0U};
  std::atomic<std::uint64_t> producerFrames_{0U};
  static std::atomic<WasmAudioDriver *> instance_;
};
