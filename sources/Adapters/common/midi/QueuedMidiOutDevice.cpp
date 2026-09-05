/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "QueuedMidiOutDevice.h"

#include "System/Console/Profiler.h"

#include <chrono>

QueuedMidiOutDevice::QueuedMidiOutDevice(Queue &queue, NowFunction now,
                                         const char *name)
    : MidiOutDevice(name), queue_(queue),
      now_(now == nullptr ? &DefaultNowMilliseconds : now) {}

bool QueuedMidiOutDevice::Init() { return true; }
void QueuedMidiOutDevice::Close() { Stop(); }
bool QueuedMidiOutDevice::Start() {
  running_.store(true, std::memory_order_release);
  return true;
}
void QueuedMidiOutDevice::Stop() {
  running_.store(false, std::memory_order_release);
}

MidiPacket QueuedMidiOutDevice::Encode(const MidiMessage &message,
                                       double timestampMilliseconds) {
  const MidiWireMessage encoded = EncodeMidiWireMessage(message);
  MidiPacket packet{encoded.bytes, encoded.length, timestampMilliseconds};
  return packet;
}

void QueuedMidiOutDevice::SendMessage(MidiMessage &message) {
  (void)SendMessageAt(message, now_());
}

bool QueuedMidiOutDevice::SendMessageAt(MidiMessage &message,
                                        double timestampMilliseconds) {
  if (!running_.load(std::memory_order_acquire))
    return false;
  Profiler::Emit(TraceCategory::Midi, TraceName::MidiOutput,
                 TracePhase::Instant);
  const MidiLatencyTrace::Ticket trace = MidiLatencyTrace::CaptureOutput(now_);
  MidiPacket packet = Encode(message, timestampMilliseconds);
  packet.enqueuedMilliseconds = trace.timestampMilliseconds;
  packet.traceGeneration = trace.generation;
  packet.traceCorrelation = trace.correlation;
  if (!queue_.Push(packet))
    return false;
  MidiLatencyTrace::PublishOutputQueued(trace);
  return true;
}

double QueuedMidiOutDevice::DefaultNowMilliseconds() {
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(
             Clock::now().time_since_epoch())
      .count();
}
