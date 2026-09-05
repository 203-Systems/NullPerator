/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Foundation/Concurrency/WorkerGate.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/PcmRingBuffer.h"

#include <AudioToolbox/AudioToolbox.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>

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
  std::span<short> GetOutputBuffer() override { return outputBuffer_; }
  void AddBuffer(short *buffer, int samplecount) override;
  void OnAudioActive(bool active) override;

  void PumpProducer() noexcept;

  [[nodiscard]] bool InputAvailable() const noexcept;
  void SetInputMonitoring(bool enabled) noexcept;
  [[nodiscard]] bool IsInputMonitoring() const noexcept;
  [[nodiscard]] bool
  BeginInputCapture(std::span<std::int16_t> destination) noexcept;
  void EndInputCapture() noexcept;
  [[nodiscard]] bool IsInputCapturing() const noexcept;
  [[nodiscard]] std::size_t CapturedInputFrames() const noexcept;
  [[nodiscard]] std::uint16_t InputPeak() const noexcept;

private:
  static OSStatus Render(void *context, AudioUnitRenderActionFlags *flags,
                         const AudioTimeStamp *timestamp, UInt32, UInt32 frames,
                         AudioBufferList *buffers);
  OSStatus Render(AudioUnitRenderActionFlags *flags,
                  const AudioTimeStamp *timestamp, UInt32 frames,
                  AudioBufferList *buffers) noexcept;
  void PullInput(AudioUnitRenderActionFlags *flags,
                 const AudioTimeStamp *timestamp, UInt32 frames) noexcept;

  PcmRingBuffer<RingCapacityFrames> ring_;
  std::array<short, MAX_SAMPLE_COUNT * 2U> outputBuffer_;
  AudioComponentInstance unit_ = nullptr;
  std::atomic<bool> started_{false};
  std::atomic<bool> active_{false};
  std::atomic<std::uint64_t> consumedFrames_{0U};
  std::array<float, 4096> inputScratch_{};
  std::array<std::int16_t, 4096> inputPcmScratch_{};
  std::atomic<bool> inputAvailable_{false};
  std::atomic<bool> inputMonitoring_{false};
  WorkerGate<1> inputCaptureGate_;
  std::atomic<std::int16_t *> inputDestination_{nullptr};
  std::size_t inputCapacityFrames_ = 0U;
  std::atomic<std::size_t> inputCapturedFrames_{0U};
  std::atomic<std::uint16_t> inputPeak_{0U};
};
