/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmAudioDriver.h"

#include "Adapters/wasm/tracing/WasmProfiler.h"
#include "System/System/System.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <span>

namespace {
std::uint32_t QuantizeCallbackMicros(double callbackMilliseconds) noexcept {
  const double micros = callbackMilliseconds * 1000.0;
  // The negated comparison also rejects NaN without pulling non-realtime
  // error handling into the worklet.
  if (!(micros > 0.0)) {
    return 0U;
  }
  if (micros >=
      static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  // Metrics are integral microseconds, but overrun classification below uses
  // the original double so this display quantization cannot hide a miss.
  return static_cast<std::uint32_t>(micros + 0.999999);
}
} // namespace

std::atomic<WasmAudioDriver *> WasmAudioDriver::instance_{nullptr};

WasmAudioDriver::WasmAudioDriver(AudioSettings &settings) : AudioDriver(settings) {
  instance_.store(this, std::memory_order_release);
}

bool WasmAudioDriver::InitDriver() { return true; }

void WasmAudioDriver::CloseDriver() {
  StopDriver();
  WasmAudioDriver *expected = this;
  instance_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

bool WasmAudioDriver::StartDriver() {
  started_.store(true, std::memory_order_release);
  return true;
}

void WasmAudioDriver::StopDriver() {
  active_.store(false, std::memory_order_release);
  started_.store(false, std::memory_order_release);
  workletRunning_.store(false, std::memory_order_release);
}

bool WasmAudioDriver::Interlaced() { return true; }

int WasmAudioDriver::GetPlayedBufferPercentage() {
  return static_cast<int>((ring_.FillFrames() * 100U) / RingCapacityFrames);
}

double WasmAudioDriver::GetStreamTime() {
  const auto frames = producerFrames_.load(std::memory_order_acquire);
  return static_cast<double>(frames) / 44100.0;
}

void WasmAudioDriver::AddBuffer(short *buffer, int samplecount) {
  if (buffer == nullptr || samplecount <= 0 || !IsStarted()) {
    return;
  }
  const std::size_t accepted = ring_.WriteInterleaved(
      std::span<const short>(buffer, static_cast<std::size_t>(samplecount) * 2U));
  producerFrames_.fetch_add(accepted, std::memory_order_relaxed);
}

void WasmAudioDriver::OnAudioActive(bool active) {
  active_.store(active, std::memory_order_release);
}

void WasmAudioDriver::PumpProducer() noexcept {
  if (!IsStarted() || !IsActive() ||
      !workletRunning_.load(std::memory_order_acquire)) {
    return;
  }
  WASM_TRACE_SCOPE(WasmTraceCategory::Audio,
                   WasmTraceName::AudioProducer);
  // Bound rendering work per browser frame. The callback only consumes the
  // queue; observer notification and tracker rendering stay on this thread.
  const std::size_t target = targetFillFrames_.load(std::memory_order_relaxed);
  for (int request = 0; request < 3 && ring_.FillFrames() < target;
       ++request) {
    onAudioBufferTick();
    const auto renderStart = std::chrono::steady_clock::now();
    OnNewBufferNeeded();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - renderStart)
                             .count();
    renderMicros_.store(
        Saturating(static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, elapsed))),
        std::memory_order_relaxed);
  }
}

std::size_t WasmAudioDriver::ReadFrames(std::span<StereoF32> output) noexcept {
  return ring_.Read(output);
}

bool WasmAudioDriver::IsActive() const noexcept {
  return active_.load(std::memory_order_acquire);
}

bool WasmAudioDriver::IsStarted() const noexcept {
  return started_.load(std::memory_order_acquire);
}

void WasmAudioDriver::SetWorkletRunning(bool running) noexcept {
  workletRunning_.store(running, std::memory_order_release);
}

void WasmAudioDriver::SetDestinationRate(std::uint32_t rate) noexcept {
  destinationRate_.store(rate, std::memory_order_release);
}

void WasmAudioDriver::Configure(std::uint32_t targetFillFrames,
                                std::uint32_t outputGainQ16) noexcept {
  targetFillFrames_.store(
      std::clamp(targetFillFrames, MinimumTargetFillFrames,
                 MaximumTargetFillFrames),
      std::memory_order_release);
  SetHostOutputGainQ16(outputGainQ16);
}

void WasmAudioDriver::SetMixerVolume(int volume) noexcept {
  const std::uint32_t clamped =
      static_cast<std::uint32_t>(std::clamp(volume, 0, 100));
  std::uint32_t current = outputGainState_.load(std::memory_order_relaxed);
  std::uint32_t desired = 0U;
  do {
    desired = (current & HostOutputGainMask) | (clamped << MixerVolumeShift);
  } while (!outputGainState_.compare_exchange_weak(
      current, desired, std::memory_order_release, std::memory_order_relaxed));
}

std::uint32_t WasmAudioDriver::TargetFillFramesConfigured() const noexcept {
  return targetFillFrames_.load(std::memory_order_acquire);
}

std::uint32_t WasmAudioDriver::OutputGainQ16() const noexcept {
  const std::uint32_t state =
      outputGainState_.load(std::memory_order_acquire);
  const std::uint32_t hostGain = state & HostOutputGainMask;
  const std::uint32_t mixerVolume = state >> MixerVolumeShift;
  // Compose the exact host Q16 value with the integer Device percentage and
  // round half up once. The clamped maximum numerator is only 6,553,650.
  return (hostGain * mixerVolume + 50U) / 100U;
}

WasmAudioMetrics WasmAudioDriver::Metrics() const noexcept {
  WasmAudioMetrics metrics{};
  metrics.ringFillFrames = static_cast<std::uint32_t>(ring_.FillFrames());
  metrics.ringCapacityFrames = RingCapacityFrames;
  metrics.callbackCount = callbackCount_.load(std::memory_order_relaxed);
  metrics.renderMicros = renderMicros_.load(std::memory_order_relaxed);
  metrics.callbackMicros = callbackMicros_.load(std::memory_order_relaxed);
  metrics.callbackMaxMicros =
      callbackMaxMicros_.load(std::memory_order_relaxed);
  metrics.callbackDeadlineMicros =
      callbackDeadlineMicros_.load(std::memory_order_relaxed);
  metrics.callbackDeadlineMisses =
      callbackDeadlineMisses_.load(std::memory_order_relaxed);
  metrics.underrunFrames = Saturating(ring_.Underruns());
  metrics.overrunFrames = Saturating(ring_.Overruns());
  metrics.destinationRate = destinationRate_.load(std::memory_order_acquire);
  return metrics;
}

void WasmAudioDriver::RecordCallback(double callbackMilliseconds,
                                    std::size_t frames) noexcept {
  const std::uint32_t callbackMicros =
      QuantizeCallbackMicros(callbackMilliseconds);
  callbackCount_.fetch_add(1U, std::memory_order_relaxed);
  callbackMicros_.store(callbackMicros, std::memory_order_relaxed);

  std::uint32_t maximum = callbackMaxMicros_.load(std::memory_order_relaxed);
  while (maximum < callbackMicros &&
         !callbackMaxMicros_.compare_exchange_weak(
             maximum, callbackMicros, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }

  const std::uint32_t rate =
      destinationRate_.load(std::memory_order_relaxed);
  std::uint32_t deadlineMicros = 0U;
  if (rate != 0U && frames != 0U) {
    const std::uint64_t numerator =
        static_cast<std::uint64_t>(frames) * 1'000'000U + rate - 1U;
    deadlineMicros = Saturating(numerator / rate);
  }
  callbackDeadlineMicros_.store(deadlineMicros, std::memory_order_relaxed);
  const bool processingDeadlineMissed =
      rate != 0U && frames != 0U && callbackMilliseconds > 0.0 &&
      callbackMilliseconds >
          (static_cast<double>(frames) * 1000.0 / static_cast<double>(rate));
  if (!processingDeadlineMissed) {
    return;
  }

  std::uint32_t misses =
      callbackDeadlineMisses_.load(std::memory_order_relaxed);
  while (misses != std::numeric_limits<std::uint32_t>::max() &&
         !callbackDeadlineMisses_.compare_exchange_weak(
             misses, misses + 1U, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

WasmAudioDriver *WasmAudioDriver::Instance() noexcept {
  return instance_.load(std::memory_order_acquire);
}

void WasmAudioDriver::SetHostOutputGainQ16(std::uint32_t gain) noexcept {
  const std::uint32_t clamped = std::min(gain, UnityGainQ16);
  std::uint32_t current = outputGainState_.load(std::memory_order_relaxed);
  std::uint32_t desired = 0U;
  do {
    desired = (current & ~HostOutputGainMask) | clamped;
  } while (!outputGainState_.compare_exchange_weak(
      current, desired, std::memory_order_release, std::memory_order_relaxed));
}

std::uint32_t WasmAudioDriver::Saturating(std::uint64_t value) noexcept {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      value, std::numeric_limits<std::uint32_t>::max()));
}
