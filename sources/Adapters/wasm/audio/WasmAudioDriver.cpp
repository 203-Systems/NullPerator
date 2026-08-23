/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmAudioDriver.h"

#include "System/System/System.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <span>

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
  const auto start = std::chrono::steady_clock::now();
  const std::size_t accepted = ring_.WriteInterleaved(
      std::span<const short>(buffer, static_cast<std::size_t>(samplecount) * 2U));
  producerFrames_.fetch_add(accepted, std::memory_order_relaxed);
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  renderMicros_.store(Saturating(static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed))),
                      std::memory_order_relaxed);
}

void WasmAudioDriver::OnAudioActive(bool active) {
  active_.store(active, std::memory_order_release);
}

void WasmAudioDriver::PumpProducer() noexcept {
  if (!IsStarted() || !IsActive() ||
      !workletRunning_.load(std::memory_order_acquire)) {
    return;
  }
  // Bound rendering work per browser frame. The callback only consumes the
  // queue; observer notification and tracker rendering stay on this thread.
  for (int request = 0; request < 3 && ring_.FillFrames() < TargetFillFrames;
       ++request) {
    onAudioBufferTick();
    OnNewBufferNeeded();
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

WasmAudioMetrics WasmAudioDriver::Metrics() const noexcept {
  WasmAudioMetrics metrics{};
  metrics.ringFillFrames = static_cast<std::uint32_t>(ring_.FillFrames());
  metrics.ringCapacityFrames = RingCapacityFrames;
  metrics.callbackCount = callbackCount_.load(std::memory_order_relaxed);
  metrics.renderMicros = renderMicros_.load(std::memory_order_relaxed);
  metrics.underrunFrames = Saturating(ring_.Underruns());
  metrics.overrunFrames = Saturating(ring_.Overruns());
  metrics.destinationRate = destinationRate_.load(std::memory_order_acquire);
  return metrics;
}

void WasmAudioDriver::RecordCallback() noexcept {
  callbackCount_.fetch_add(1U, std::memory_order_relaxed);
}

WasmAudioDriver *WasmAudioDriver::Instance() noexcept {
  return instance_.load(std::memory_order_acquire);
}

std::uint32_t WasmAudioDriver::Saturating(std::uint64_t value) noexcept {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      value, std::numeric_limits<std::uint32_t>::max()));
}
