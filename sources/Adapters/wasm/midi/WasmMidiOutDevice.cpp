/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmMidiOutDevice.h"

#include "Adapters/wasm/tracing/WasmProfiler.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <chrono>
#endif

WasmMidiOutDevice::WasmMidiOutDevice(Queue &queue, NowFunction now,
                                     const char *name)
    : MidiOutDevice(name), queue_(queue),
      now_(now == nullptr ? &DefaultNowMilliseconds : now) {}

bool WasmMidiOutDevice::Init() { return true; }
void WasmMidiOutDevice::Close() { Stop(); }
bool WasmMidiOutDevice::Start() {
  running_.store(true, std::memory_order_release);
  return true;
}
void WasmMidiOutDevice::Stop() {
  running_.store(false, std::memory_order_release);
}

MidiPacket WasmMidiOutDevice::Encode(const MidiMessage &message,
                                     double timestampMilliseconds) {
  const MidiWireMessage encoded = EncodeMidiWireMessage(message);
  MidiPacket packet{encoded.bytes, encoded.length, timestampMilliseconds};
  return packet;
}

void WasmMidiOutDevice::SendMessage(MidiMessage &message) {
  (void)SendMessageAt(message, now_());
}

bool WasmMidiOutDevice::SendMessageAt(MidiMessage &message,
                                      double timestampMilliseconds) {
  if (!running_.load(std::memory_order_acquire)) return false;
  WasmProfiler::Emit(WasmTraceCategory::Midi, WasmTraceName::MidiOutput,
                     WasmTracePhase::Instant);
  const MidiLatencyTrace::Ticket trace =
      MidiLatencyTrace::CaptureOutput(now_);
  MidiPacket packet = Encode(message, timestampMilliseconds);
  packet.enqueuedMilliseconds = trace.timestampMilliseconds;
  packet.traceGeneration = trace.generation;
  packet.traceCorrelation = trace.correlation;
  if (!queue_.Push(packet)) return false;
  MidiLatencyTrace::PublishOutputQueued(trace);
  return true;
}

double WasmMidiOutDevice::DefaultNowMilliseconds() {
#ifdef __EMSCRIPTEN__
  // Epoch time is shared by the application pthread and browser main. The JS
  // bridge converts it to the page's DOMHighResTimeStamp origin before send().
  return emscripten_get_now();
#else
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(
             Clock::now().time_since_epoch())
      .count();
#endif
}
