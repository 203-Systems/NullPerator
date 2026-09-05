/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/common/midi/MidiLatencyTrace.h"

#include "System/Console/Profiler.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace {
std::atomic<std::uint32_t> nextInputCorrelation{0U};
std::atomic<std::uint32_t> nextOutputCorrelation{0U};

std::uint16_t NextCorrelation(std::atomic<std::uint32_t> &counter) noexcept {
  for (;;) {
    const auto correlation = static_cast<std::uint16_t>(
        counter.fetch_add(1U, std::memory_order_relaxed) + 1U);
    if (correlation != 0U)
      return correlation;
  }
}

MidiLatencyTrace::Ticket Capture(MidiLatencyTrace::NowFunction now,
                                 std::atomic<std::uint32_t> &counter) noexcept {
  if (!Profiler::CategoryEnabled(TraceCategory::Midi))
    return {};
  return MidiLatencyTrace::Ticket{now == nullptr ? 0.0 : now(),
                                  Profiler::Generation(),
                                  NextCorrelation(counter)};
}

bool Active(std::uint32_t generation, std::uint16_t correlation) noexcept {
  return correlation != 0U && generation == Profiler::Generation() &&
         Profiler::CategoryEnabled(TraceCategory::Midi);
}
} // namespace

MidiLatencyTrace::Ticket
MidiLatencyTrace::CaptureInput(NowFunction now) noexcept {
  return Capture(now, nextInputCorrelation);
}

MidiLatencyTrace::Ticket
MidiLatencyTrace::CaptureOutput(NowFunction now) noexcept {
  return Capture(now, nextOutputCorrelation);
}

std::uint64_t
MidiLatencyTrace::TimestampMicroseconds(double timestampMilliseconds) noexcept {
  if (!std::isfinite(timestampMilliseconds) || timestampMilliseconds <= 0.0) {
    return 0U;
  }
  const double microseconds = timestampMilliseconds * 1000.0;
  if (!std::isfinite(microseconds) ||
      microseconds >=
          static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(microseconds);
}

std::uint32_t
MidiLatencyTrace::LatencyMicroseconds(double startMilliseconds,
                                      double endMilliseconds) noexcept {
  if (!std::isfinite(startMilliseconds) || !std::isfinite(endMilliseconds) ||
      startMilliseconds < 0.0 || endMilliseconds <= startMilliseconds) {
    return 0U;
  }
  const double microseconds = (endMilliseconds - startMilliseconds) * 1000.0;
  if (!std::isfinite(microseconds) ||
      microseconds >=
          static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(microseconds);
}

void MidiLatencyTrace::PublishInputAccepted(const Ticket &ticket) noexcept {
  if (ticket.correlation == 0U)
    return;
  Profiler::EmitAt(TimestampMicroseconds(ticket.timestampMilliseconds),
                   TraceCategory::Midi, TraceName::MidiInputAccepted,
                   TracePhase::Instant, ticket.correlation,
                   TraceThread::Browser, ticket.generation);
}

void MidiLatencyTrace::PublishOutputQueued(const Ticket &ticket) noexcept {
  if (ticket.correlation == 0U)
    return;
  Profiler::EmitAt(TimestampMicroseconds(ticket.timestampMilliseconds),
                   TraceCategory::Midi, TraceName::MidiOutputQueued,
                   TracePhase::Instant, ticket.correlation,
                   TraceThread::Application, ticket.generation);
}

void MidiLatencyTrace::InputProcessed(const MidiByteRecord &record,
                                      NowFunction now) noexcept {
  if (!record.batchStart ||
      !Active(record.traceGeneration, record.traceCorrelation)) {
    return;
  }
  const double processedMilliseconds = now == nullptr ? 0.0 : now();
  Profiler::EmitAt(
      TimestampMicroseconds(processedMilliseconds), TraceCategory::Midi,
      TraceName::MidiInputLatencyUs, TracePhase::Counter,
      LatencyMicroseconds(record.acceptedMilliseconds, processedMilliseconds),
      TraceThread::Application, record.traceGeneration,
      record.traceCorrelation);
}

void MidiLatencyTrace::OutputDrained(const MidiPacket &packet,
                                     NowFunction now) noexcept {
  if (!Active(packet.traceGeneration, packet.traceCorrelation))
    return;
  const double drainedMilliseconds = now == nullptr ? 0.0 : now();
  Profiler::EmitAt(
      TimestampMicroseconds(drainedMilliseconds), TraceCategory::Midi,
      TraceName::MidiOutputLatencyUs, TracePhase::Counter,
      LatencyMicroseconds(packet.enqueuedMilliseconds, drainedMilliseconds),
      TraceThread::Browser, packet.traceGeneration, packet.traceCorrelation);
}

void MidiLatencyTrace::ResetCorrelationsForTesting(
    std::uint32_t inputSeed, std::uint32_t outputSeed) noexcept {
  nextInputCorrelation.store(inputSeed, std::memory_order_relaxed);
  nextOutputCorrelation.store(outputSeed, std::memory_order_relaxed);
}
