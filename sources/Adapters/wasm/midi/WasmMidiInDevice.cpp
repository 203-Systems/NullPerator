/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmMidiInDevice.h"

#include "Adapters/wasm/tracing/WasmProfiler.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <chrono>
#endif

#include <cstring>

namespace {
constexpr std::size_t MaxBytesPerFrame = 512U;

double DefaultNowMilliseconds() {
#ifdef __EMSCRIPTEN__
  return emscripten_get_now();
#else
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(
             Clock::now().time_since_epoch())
      .count();
#endif
}
}

WasmMidiInDevice::WasmMidiInDevice(MidiLatencyTrace::NowFunction now,
                                   const char *name)
    : MidiInDevice(name),
      now_(now == nullptr ? &DefaultNowMilliseconds : now) {}

bool WasmMidiInDevice::Start() { return MidiInDevice::Start(); }

void WasmMidiInDevice::Stop() { MidiInDevice::Stop(); }

bool WasmMidiInDevice::Submit(std::span<const std::uint8_t> bytes,
                              double timestampMilliseconds,
                              const MidiLatencyTrace::Ticket &trace) noexcept {
  return queue_.Push(bytes, timestampMilliseconds, trace.timestampMilliseconds,
                     trace.generation, trace.correlation);
}

void WasmMidiInDevice::RequestDisconnect() noexcept {
  disconnectRequested_.store(true, std::memory_order_release);
}

std::uint64_t WasmMidiInDevice::ProcessedBytes() const noexcept {
  return processedBytes_.load(std::memory_order_acquire);
}

std::uint8_t WasmMidiInDevice::LastProcessedByte() const noexcept {
  return static_cast<std::uint8_t>(
      lastProcessedByte_.load(std::memory_order_relaxed));
}

double WasmMidiInDevice::LastProcessedTimestamp() const noexcept {
  const std::uint64_t bits =
      lastProcessedTimestampBits_.load(std::memory_order_relaxed);
  double timestamp = 0.0;
  std::memcpy(&timestamp, &bits, sizeof(timestamp));
  return timestamp;
}

std::uint32_t WasmMidiInDevice::ResetGeneration() const noexcept {
  return resetGeneration_.load(std::memory_order_acquire);
}

void WasmMidiInDevice::ResetParserOnApplicationThread() {
  queue_.DiscardConsumer();
  if (IsRunning()) {
    MidiInDevice::Stop();
    (void)MidiInDevice::Start();
  }
  resetGeneration_.fetch_add(1U, std::memory_order_release);
}

void WasmMidiInDevice::poll() {
  if (!IsRunning()) return;
  if (disconnectRequested_.exchange(false, std::memory_order_acq_rel)) {
    ResetParserOnApplicationThread();
  }
  MidiByteRecord record{};
  std::size_t count = 0;
  for (; count < MaxBytesPerFrame && queue_.TryPop(record); ++count) {
    processMidiData(record.byte);
    // The latency endpoint is after the first byte has actually entered the
    // parser, rather than merely when the application poll observed a batch.
    MidiLatencyTrace::InputProcessed(record, now_);
    std::uint64_t timestampBits = 0U;
    std::memcpy(&timestampBits, &record.timestampMilliseconds,
                sizeof(timestampBits));
    lastProcessedByte_.store(record.byte, std::memory_order_relaxed);
    lastProcessedTimestampBits_.store(timestampBits,
                                      std::memory_order_relaxed);
    processedBytes_.fetch_add(1U, std::memory_order_release);
  }
  if (count != 0U) {
    WasmProfiler::Emit(WasmTraceCategory::Midi, WasmTraceName::MidiInput,
                       WasmTracePhase::Counter,
                       static_cast<std::uint32_t>(count));
  }
}

bool WasmMidiInDevice::initDriver() { return true; }
void WasmMidiInDevice::closeDriver() {}
bool WasmMidiInDevice::startDriver() { return true; }
void WasmMidiInDevice::stopDriver() {}
