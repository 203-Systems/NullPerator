/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/PcmRingBuffer.h"
#include <aaudio/AAudio.h>
#include <array>
#include <chrono>
#include <atomic>
#include <span>

// The owner thread opens/closes streams and produces PCM. Callbacks only copy
// preallocated buffers; they never call the tracker, allocate, or acquire locks.
class AndroidAudioDriver final : public AudioDriver {
public:
  explicit AndroidAudioDriver(AudioSettings &s) : AudioDriver(s) {}
  ~AndroidAudioDriver() override { CloseDriver(); }
  bool InitDriver() override;
  void CloseDriver() override;
  bool StartDriver() override;
  void StopDriver() override;
  bool Interlaced() override { return true; }
  int GetPlayedBufferPercentage() override;
  double GetStreamTime() override;
  std::span<short> GetOutputBuffer() override { return outputBuffer_; }
  void AddBuffer(short *, int) override;
  void OnAudioActive(bool) override {}
  void PumpProducer() noexcept;
  void SetSuspended(bool);
  bool InputAvailable() const noexcept { return true; }
  void SetInputMonitoring(bool) noexcept {}
  bool IsInputMonitoring() const noexcept { return false; }
  bool BeginInputCapture(std::span<std::int16_t>) noexcept;
  void EndInputCapture() noexcept;
  bool IsInputCapturing() const noexcept { return capturing_.load(); }
  std::size_t CapturedInputFrames() const noexcept { return captured_.load(); }
  std::uint16_t InputPeak() const noexcept { return peak_.load(); }
  void LogInputCaptureStats() const;
  void FormatInputCaptureStats(std::span<char>) const;
private:
  bool OpenOutput();
  static aaudio_data_callback_result_t Output(AAudioStream *, void *, void *, int32_t);
  static aaudio_data_callback_result_t Input(AAudioStream *, void *, void *, int32_t);
  static void OutputError(AAudioStream *, void *, aaudio_result_t);
  static void InputError(AAudioStream *, void *, aaudio_result_t);
  PcmRingBuffer<16384> ring_;
  std::array<short, MAX_SAMPLE_COUNT * 2> outputBuffer_{};
  AAudioStream *output_ = nullptr, *input_ = nullptr;
  bool started_ = false, suspended_ = false;
  std::chrono::steady_clock::time_point nextOutputAttempt_{};
  std::atomic<bool> outputFailed_{false}, capturing_{false};
  std::atomic<std::uint64_t> consumed_{0};
  std::span<std::int16_t> destination_;
  std::atomic<std::size_t> captured_{0};
  std::atomic<std::uint16_t> peak_{0};
};
