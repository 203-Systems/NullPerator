#include "Adapters/wasm/midi/MidiByteQueue.h"
#include "Adapters/wasm/midi/MidiLatencyTrace.h"
#include "Adapters/wasm/midi/WasmMidiOutDevice.h"
#include "Adapters/wasm/tracing/WasmProfiler.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace {
double midiTimeMilliseconds = 0.0;
std::atomic<std::uint32_t> midiClockReads{0U};

double MidiNow() {
  midiClockReads.fetch_add(1U, std::memory_order_relaxed);
  return midiTimeMilliseconds;
}

struct DecodedMidiTrace {
  std::uint64_t timestampUs = 0U;
  std::uint32_t value = 0U;
  WasmTraceName name = WasmTraceName::Frame;
  WasmTracePhase phase = WasmTracePhase::Instant;
  WasmTraceThread thread = WasmTraceThread::Application;
  std::uint16_t flags = 0U;
};

std::vector<DecodedMidiTrace> DrainMidiTrace() {
  std::vector<DecodedMidiTrace> output;
  for (;;) {
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(
        WasmProfiler::Drain());
    std::uint32_t count = 0U;
    std::memcpy(&count, bytes + 12U, sizeof(count));
    for (std::uint32_t index = 0U; index < count; ++index) {
      const std::uint8_t *record = bytes + WasmProfiler::DrainHeaderBytes +
                                   index * WasmProfiler::DrainRecordBytes;
      DecodedMidiTrace decoded{};
      std::uint16_t name = 0U;
      std::memcpy(&decoded.timestampUs, record + 8U,
                  sizeof(decoded.timestampUs));
      std::memcpy(&decoded.value, record + 16U, sizeof(decoded.value));
      std::memcpy(&name, record + 26U, sizeof(name));
      std::memcpy(&decoded.flags, record + 30U, sizeof(decoded.flags));
      decoded.name = static_cast<WasmTraceName>(name);
      decoded.phase = static_cast<WasmTracePhase>(record[28]);
      decoded.thread = static_cast<WasmTraceThread>(record[29]);
      output.push_back(decoded);
    }
    if (count < WasmProfiler::DrainCapacity) return output;
  }
}
} // namespace

TEST_CASE("WASM MIDI byte queue preserves realtime bytes interleaved with channel data") {
  MidiByteQueue<16> queue;
  const std::array<std::uint8_t, 4> bytes{0x90, 60, 0xF8, 100};

  CHECK(queue.Push(bytes, 42.5));
  std::array<MidiByteRecord, 4> output{};
  CHECK(queue.Pop(output) == output.size());
  for (std::size_t index = 0; index < output.size(); ++index) {
    CHECK(output[index].byte == bytes[index]);
    CHECK(output[index].timestampMilliseconds == doctest::Approx(42.5));
  }
}

TEST_CASE("WASM MIDI byte queue accepts or rejects each browser batch atomically") {
  MidiByteQueue<4> queue;
  const std::array<std::uint8_t, 3> first{0x90, 60, 100};
  const std::array<std::uint8_t, 2> overflow{0x80, 60};

  CHECK(queue.Push(first, 1.0));
  CHECK_FALSE(queue.Push(overflow, 2.0));
  CHECK(queue.Size() == 3);
  CHECK(queue.Dropped() == 2);

  std::array<MidiByteRecord, 4> output{};
  CHECK(queue.Pop(output) == 3);
  CHECK(output[0].byte == 0x90);
  CHECK(output[1].byte == 60);
  CHECK(output[2].byte == 100);
}

TEST_CASE("WASM MIDI byte queue remains ordered under SPSC contention") {
  constexpr std::size_t eventCount = 32'768;
  MidiByteQueue<64> queue;
  std::atomic<bool> ordered{true};

  std::thread producer([&] {
    for (std::size_t index = 0; index < eventCount; ++index) {
      const std::uint8_t byte = static_cast<std::uint8_t>(index);
      const auto correlation =
          static_cast<std::uint16_t>((index % 65'535U) + 1U);
      while (!queue.Push(std::span<const std::uint8_t>(&byte, 1),
                         static_cast<double>(index),
                         static_cast<double>(index) + 0.5,
                         static_cast<std::uint32_t>(index), correlation)) {
        std::this_thread::yield();
      }
    }
  });
  std::thread consumer([&] {
    for (std::size_t index = 0; index < eventCount; ++index) {
      MidiByteRecord record{};
      while (!queue.TryPop(record)) std::this_thread::yield();
      const auto correlation =
          static_cast<std::uint16_t>((index % 65'535U) + 1U);
      if (record.byte != static_cast<std::uint8_t>(index) ||
          record.timestampMilliseconds != static_cast<double>(index) ||
          record.acceptedMilliseconds != static_cast<double>(index) + 0.5 ||
          record.traceGeneration != static_cast<std::uint32_t>(index) ||
          record.traceCorrelation != correlation || !record.batchStart) {
        ordered.store(false, std::memory_order_relaxed);
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK(ordered.load(std::memory_order_relaxed));
  CHECK(queue.Size() == 0);
}

TEST_CASE("WASM MIDI packet queue reserves realtime capacity and drains by sequence") {
  MidiPacketQueue<2, 2> queue;
  CHECK(queue.Push({{0x90, 60, 100}, 3, 10.0}));
  CHECK(queue.Push({{0x80, 60, 0}, 3, 11.0}));
  CHECK_FALSE(queue.Push({{0xB0, 7, 100}, 3, 12.0}));
  CHECK(queue.Push({{0xF8, 0, 0}, 1, 13.0}));

  std::array<MidiPacket, 3> output{};
  CHECK(queue.Pop(output) == output.size());
  CHECK(output[0].bytes[0] == 0x90);
  CHECK(output[1].bytes[0] == 0x80);
  CHECK(output[2].bytes[0] == 0xF8);
  CHECK(output[2].timestampMilliseconds == doctest::Approx(13.0));
  CHECK(queue.DroppedNormal() == 1);
  CHECK(queue.DroppedRealtime() == 0);
}

TEST_CASE("WASM MIDI output encodes channel, short, and realtime packet lengths") {
  const MidiPacket note = WasmMidiOutDevice::Encode(
      MidiMessage(MidiMessage::MIDI_NOTE_ON | 2U, 64U, 127U), 5.0);
  CHECK(note.length == 3);
  CHECK((note.bytes == std::array<std::uint8_t, 3>{0x92, 64, 127}));
  CHECK(note.timestampMilliseconds == doctest::Approx(5.0));

  const MidiPacket program = WasmMidiOutDevice::Encode(
      MidiMessage(MidiMessage::MIDI_PROGRAM_CHANGE, 7U, 99U), 6.0);
  CHECK(program.length == 2);
  CHECK((program.bytes == std::array<std::uint8_t, 3>{0xC0, 7, 0}));

  const MidiPacket clock = WasmMidiOutDevice::Encode(
      MidiMessage(MidiMessage::MIDI_CLOCK, 99U, 99U), 7.0);
  CHECK(clock.length == 1);
  CHECK((clock.bytes == std::array<std::uint8_t, 3>{0xF8, 0, 0}));
}

TEST_CASE("WASM MIDI input traces one accepted-to-processed latency per browser batch") {
  WasmProfiler::Stop();
  MidiLatencyTrace::ResetCorrelationsForTesting();
  midiClockReads.store(0U, std::memory_order_relaxed);
  midiTimeMilliseconds = 10.0;
  const std::uint32_t generation = WasmProfiler::Start(
      static_cast<std::uint32_t>(WasmTraceCategory::Midi));

  MidiByteQueue<16> queue;
  const std::array<std::uint8_t, 3> bytes{0x90, 60, 100};
  const MidiLatencyTrace::Ticket trace =
      MidiLatencyTrace::CaptureInput(&MidiNow);
  REQUIRE(trace.generation == generation);
  REQUIRE(trace.correlation != 0U);
  REQUIRE(queue.Push(bytes, 1'234.5, trace.timestampMilliseconds,
                     trace.generation, trace.correlation));
  MidiLatencyTrace::PublishInputAccepted(trace);

  midiTimeMilliseconds = 12.25;
  std::array<MidiByteRecord, 3> records{};
  REQUIRE(queue.Pop(records) == records.size());
  for (std::size_t index = 0; index < records.size(); ++index) {
    CHECK(records[index].timestampMilliseconds == doctest::Approx(1'234.5));
    CHECK(records[index].acceptedMilliseconds == doctest::Approx(10.0));
    CHECK(records[index].traceCorrelation == trace.correlation);
    CHECK(records[index].batchStart == (index == 0U));
    // Represents the production call immediately after processMidiData().
    MidiLatencyTrace::InputProcessed(records[index], &MidiNow);
  }
  WasmProfiler::Stop();

  const auto traced = DrainMidiTrace();
  REQUIRE(traced.size() == 2U);
  CHECK(traced[0].name == WasmTraceName::MidiInputAccepted);
  CHECK(traced[0].timestampUs == 10'000U);
  CHECK(traced[0].value == trace.correlation);
  CHECK(traced[0].thread == WasmTraceThread::Browser);
  CHECK(traced[1].name == WasmTraceName::MidiInputLatencyUs);
  CHECK(traced[1].phase == WasmTracePhase::Counter);
  CHECK(traced[1].thread == WasmTraceThread::Application);
  CHECK(traced[1].value == 2'250U);
  CHECK(traced[1].flags == trace.correlation);
  // One acceptance read and one processing read, never one read per byte.
  CHECK(midiClockReads.load(std::memory_order_relaxed) == 2U);
}

TEST_CASE("WASM MIDI input batch crossing the 512-byte poll budget settles only once") {
  WasmProfiler::Stop();
  MidiLatencyTrace::ResetCorrelationsForTesting();
  midiTimeMilliseconds = 20.0;
  WasmProfiler::Start(static_cast<std::uint32_t>(WasmTraceCategory::Midi));

  MidiByteQueue<1024> queue;
  std::array<std::uint8_t, 700> bytes{};
  const MidiLatencyTrace::Ticket trace =
      MidiLatencyTrace::CaptureInput(&MidiNow);
  REQUIRE(queue.Push(bytes, 77.0, trace.timestampMilliseconds,
                     trace.generation, trace.correlation));
  MidiLatencyTrace::PublishInputAccepted(trace);

  MidiByteRecord record{};
  midiTimeMilliseconds = 21.0;
  for (std::size_t index = 0U; index < 512U; ++index) {
    REQUIRE(queue.TryPop(record));
    MidiLatencyTrace::InputProcessed(record, &MidiNow);
  }
  midiTimeMilliseconds = 29.0;
  while (queue.TryPop(record)) {
    MidiLatencyTrace::InputProcessed(record, &MidiNow);
  }
  WasmProfiler::Stop();

  const auto traced = DrainMidiTrace();
  REQUIRE(traced.size() == 2U);
  CHECK(traced[0].name == WasmTraceName::MidiInputAccepted);
  CHECK(traced[1].name == WasmTraceName::MidiInputLatencyUs);
  CHECK(traced[1].value == 1'000U);
  CHECK(traced[1].flags == trace.correlation);
}

TEST_CASE("WASM MIDI output queue latency excludes the future scheduled send delay") {
  WasmProfiler::Stop();
  MidiLatencyTrace::ResetCorrelationsForTesting();
  midiTimeMilliseconds = 100.0;
  WasmProfiler::Start(static_cast<std::uint32_t>(WasmTraceCategory::Midi));

  WasmMidiOutDevice::Queue queue;
  WasmMidiOutDevice output(queue, &MidiNow);
  REQUIRE(output.Start());
  MidiMessage message(MidiMessage::MIDI_NOTE_ON, 64U, 100U);
  REQUIRE(output.SendMessageAt(message, 10'100.0));

  MidiPacket packet{};
  REQUIRE(queue.TryPop(packet));
  CHECK(packet.timestampMilliseconds == doctest::Approx(10'100.0));
  CHECK(packet.enqueuedMilliseconds == doctest::Approx(100.0));
  REQUIRE(packet.traceCorrelation != 0U);
  midiTimeMilliseconds = 105.0;
  MidiLatencyTrace::OutputDrained(packet, &MidiNow);
  WasmProfiler::Stop();

  const auto traced = DrainMidiTrace();
  const auto queued = std::find_if(traced.begin(), traced.end(), [](const auto &record) {
    return record.name == WasmTraceName::MidiOutputQueued;
  });
  const auto latency = std::find_if(traced.begin(), traced.end(), [](const auto &record) {
    return record.name == WasmTraceName::MidiOutputLatencyUs;
  });
  REQUIRE(queued != traced.end());
  REQUIRE(latency != traced.end());
  CHECK(queued->timestampUs == 100'000U);
  CHECK(queued->value == packet.traceCorrelation);
  CHECK(latency->timestampUs == 105'000U);
  CHECK(latency->value == 5'000U);
  CHECK(latency->flags == packet.traceCorrelation);
  CHECK(latency->value < 10'000'000U);
}

TEST_CASE("WASM MIDI trace correlations wrap independently without publishing zero") {
  WasmProfiler::Stop();
  MidiLatencyTrace::ResetCorrelationsForTesting(0xFFFFU, 0xFFFFU);
  midiTimeMilliseconds = 1.0;
  WasmProfiler::Start(static_cast<std::uint32_t>(WasmTraceCategory::Midi));
  const auto inputOne = MidiLatencyTrace::CaptureInput(&MidiNow);
  const auto outputOne = MidiLatencyTrace::CaptureOutput(&MidiNow);
  const auto inputTwo = MidiLatencyTrace::CaptureInput(&MidiNow);
  const auto outputTwo = MidiLatencyTrace::CaptureOutput(&MidiNow);
  WasmProfiler::Stop();

  CHECK(inputOne.correlation == 1U);
  CHECK(outputOne.correlation == 1U);
  CHECK(inputTwo.correlation == 2U);
  CHECK(outputTwo.correlation == 2U);
}

TEST_CASE("WASM MIDI trace timestamps and latency saturate abnormal clocks safely") {
  const double infinity = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(MidiLatencyTrace::TimestampMicroseconds(-1.0) == 0U);
  CHECK(MidiLatencyTrace::TimestampMicroseconds(nan) == 0U);
  CHECK(MidiLatencyTrace::TimestampMicroseconds(infinity) == 0U);
  CHECK(MidiLatencyTrace::TimestampMicroseconds(
            std::numeric_limits<double>::max()) ==
        std::numeric_limits<std::uint64_t>::max());
  CHECK(MidiLatencyTrace::LatencyMicroseconds(10.0, 9.0) == 0U);
  CHECK(MidiLatencyTrace::LatencyMicroseconds(nan, 20.0) == 0U);
  CHECK(MidiLatencyTrace::LatencyMicroseconds(10.0, infinity) == 0U);
  CHECK(MidiLatencyTrace::LatencyMicroseconds(
            0.0, std::numeric_limits<double>::max()) ==
        std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("WASM MIDI tracing disabled reads no clock and leaves queue behavior unchanged") {
  WasmProfiler::Stop();
  midiClockReads.store(0U, std::memory_order_relaxed);
  const auto input = MidiLatencyTrace::CaptureInput(&MidiNow);
  const auto output = MidiLatencyTrace::CaptureOutput(&MidiNow);
  CHECK(input.correlation == 0U);
  CHECK(output.correlation == 0U);
  CHECK(midiClockReads.load(std::memory_order_relaxed) == 0U);

  MidiByteQueue<4> queue;
  const std::array<std::uint8_t, 3> bytes{0x90, 60, 100};
  CHECK(queue.Push(bytes, 42.0));
  std::array<MidiByteRecord, 3> records{};
  CHECK(queue.Pop(records) == records.size());
  CHECK(records[0].byte == 0x90U);
  CHECK(records[2].byte == 100U);
}
