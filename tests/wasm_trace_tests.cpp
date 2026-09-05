#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/tracing/Benchmark.h"
#include "Adapters/wasm/tracing/InputFrameLatencyTracker.h"
#include "System/Console/Profiler.h"
#include "System/Console/TraceRingBuffer.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace {
std::atomic<std::uint64_t> clockReads{0};
std::uint64_t TraceNow() {
  return 100 + clockReads.fetch_add(1, std::memory_order_relaxed);
}
std::uint64_t benchmarkTime = 0;
std::uint64_t BenchmarkNow() {
  benchmarkTime += 10;
  return benchmarkTime;
}
std::uint64_t inputTime = 0;
std::uint64_t InputNow() { return inputTime; }
std::atomic<std::uint64_t> concurrentInputTime{100};
std::uint64_t ConcurrentInputNow() {
  return concurrentInputTime.fetch_add(1U, std::memory_order_relaxed);
}
bool inputQueueAccepts = true;
bool AcceptInputEvent(std::uint16_t, bool) { return inputQueueAccepts; }

struct DecodedTraceRecord {
  std::uint64_t timestampUs = 0;
  std::uint32_t value = 0;
  TraceName name = TraceName::Frame;
  TracePhase phase = TracePhase::Instant;
  TraceThread thread = TraceThread::Application;
  std::uint16_t flags = 0;
};

std::vector<DecodedTraceRecord> DrainTraceRecords() {
  std::vector<DecodedTraceRecord> output;
  for (;;) {
    const auto *bytes =
        reinterpret_cast<const std::uint8_t *>(Profiler::Drain());
    std::uint32_t count = 0;
    std::memcpy(&count, bytes + 12, sizeof(count));
    for (std::uint32_t index = 0; index < count; ++index) {
      const std::uint8_t *record = bytes + Profiler::DrainHeaderBytes +
                                   index * Profiler::DrainRecordBytes;
      DecodedTraceRecord decoded{};
      std::uint16_t name = 0;
      std::memcpy(&decoded.timestampUs, record + 8,
                  sizeof(decoded.timestampUs));
      std::memcpy(&decoded.value, record + 16, sizeof(decoded.value));
      std::memcpy(&name, record + 26, sizeof(name));
      std::memcpy(&decoded.flags, record + 30, sizeof(decoded.flags));
      decoded.name = static_cast<TraceName>(name);
      decoded.phase = static_cast<TracePhase>(record[28]);
      decoded.thread = static_cast<TraceThread>(record[29]);
      output.push_back(decoded);
    }
    if (count < Profiler::DrainCapacity) {
      return output;
    }
  }
}
TraceRecord Event(std::uint64_t timestamp, TracePhase phase) {
  TraceRecord record{};
  record.timestampUs = timestamp;
  record.category = TraceCategory::Ui;
  record.name = TraceName::Frame;
  record.phase = phase;
  record.thread = TraceThread::Application;
  return record;
}

TEST_CASE("WASM profiler disabled scope reads no clock and writes no records") {
  Profiler::SetClock(TraceNow);
  Profiler::Stop();
  clockReads.store(0, std::memory_order_relaxed);
  const auto before = Profiler::WrittenForTesting();
  {
    PROFILE_SCOPE(TraceCategory::Ui, TraceName::Frame);
  }
  InputFrameLatencyTracker::ResetForTesting();
  InputFrameLatencyTracker::AcceptPress(3);
  CHECK(clockReads.load(std::memory_order_relaxed) == 0);
  CHECK(Profiler::WrittenForTesting() == before);
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 0);
}

TEST_CASE("WASM profiler masks categories and emits a paired fixed drain ABI") {
  Profiler::SetClock(TraceNow);
  clockReads.store(0, std::memory_order_relaxed);
  const std::uint32_t generation =
      Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Ui));
  {
    PROFILE_SCOPE(TraceCategory::Input, TraceName::InputDispatch);
  }
  {
    PROFILE_SCOPE(TraceCategory::Ui, TraceName::Frame);
  }
  Profiler::Stop();
  CHECK(clockReads.load(std::memory_order_relaxed) == 2);
  const auto pointer = Profiler::Drain();
  REQUIRE(pointer != 0);
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(pointer);
  std::uint32_t version = 0, header = 0, recordBytes = 0, count = 0,
                drainedGeneration = 0;
  std::memcpy(&version, bytes, 4);
  std::memcpy(&header, bytes + 4, 4);
  std::memcpy(&recordBytes, bytes + 8, 4);
  std::memcpy(&count, bytes + 12, 4);
  std::memcpy(&drainedGeneration, bytes + 28, 4);
  CHECK(version == 1);
  CHECK(header == Profiler::DrainHeaderBytes);
  CHECK(recordBytes == Profiler::DrainRecordBytes);
  CHECK(count == 2);
  CHECK(drainedGeneration == generation);
  CHECK(bytes[header + 28] == static_cast<std::uint8_t>(TracePhase::Begin));
  CHECK(bytes[header + recordBytes + 28] ==
        static_cast<std::uint8_t>(TracePhase::End));
}

TEST_CASE(
    "WASM profiler preserves all application-thread audio snapshot counters") {
  Profiler::SetClock(TraceNow);
  const std::uint32_t generation =
      Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Audio));
  constexpr std::array<TraceName, 9> names{
      TraceName::AudioSnapshot,
      TraceName::AudioCallbackCount,
      TraceName::AudioUnderrunFrames,
      TraceName::AudioOverrunFrames,
      TraceName::AudioRenderDurationUs,
      TraceName::AudioCallbackDurationUs,
      TraceName::AudioCallbackMaxDurationUs,
      TraceName::AudioCallbackDeadlineUs,
      TraceName::AudioCallbackProcessingDeadlineMisses};
  constexpr std::array<std::uint32_t, 9> values{2048U, 17U,   3U,    5U, 611U,
                                                913U,  1201U, 2903U, 2U};
  for (std::size_t index = 0; index < names.size(); ++index) {
    Profiler::Emit(TraceCategory::Audio, names[index], TracePhase::Counter,
                   values[index]);
  }
  Profiler::Stop();

  const auto *bytes = reinterpret_cast<const std::uint8_t *>(Profiler::Drain());
  REQUIRE(bytes != nullptr);
  std::uint32_t count = 0U;
  std::uint32_t drainedGeneration = 0U;
  std::memcpy(&count, bytes + 12, sizeof(count));
  std::memcpy(&drainedGeneration, bytes + 28, sizeof(drainedGeneration));
  REQUIRE(count == names.size());
  CHECK(drainedGeneration == generation);
  for (std::size_t index = 0; index < names.size(); ++index) {
    const std::uint8_t *record =
        bytes + Profiler::DrainHeaderBytes + index * Profiler::DrainRecordBytes;
    std::uint32_t value = 0U;
    std::uint16_t category = 0U;
    std::uint16_t name = 0U;
    std::memcpy(&value, record + 16, sizeof(value));
    std::memcpy(&category, record + 24, sizeof(category));
    std::memcpy(&name, record + 26, sizeof(name));
    CHECK(value == values[index]);
    CHECK(category == static_cast<std::uint16_t>(TraceCategory::Audio));
    CHECK(name == static_cast<std::uint16_t>(names[index]));
    CHECK(record[28] == static_cast<std::uint8_t>(TracePhase::Counter));
    CHECK(record[29] == static_cast<std::uint8_t>(TraceThread::Application));
  }
}

TEST_CASE("WASM input tracing measures acceptance to committed frame rather "
          "than dispatch duration") {
  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  InputMap::SetQueueForTesting(&AcceptInputEvent);
  inputQueueAccepts = true;
  InputMap::ReleaseAllActions();
  Profiler::SetClock(InputNow);
  inputTime = 1'000;
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Input));

  REQUIRE(InputMap::SetAction(3, true));
  REQUIRE(InputFrameLatencyTracker::PendingForTesting() == 1);
  // This boundary mirrors WasmEventManager: SDL decode happens on the
  // application pthread immediately before DispatchEvent. It arms but does
  // not finish the latency sample.
  inputTime = 1'040;
  std::uint16_t decodedAction = 0;
  bool decodedPressed = false;
  REQUIRE(InputMap::DecodeActionEvent((3U << 1U) | 1U, decodedAction,
                                      decodedPressed));
  CHECK(decodedAction == 3);
  CHECK(decodedPressed);
  InputMap::AcknowledgeAction(3, true);
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 1);
  inputTime = 1'175;
  InputFrameLatencyTracker::PresentedFrame();
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 0);
  Profiler::Stop();

  const auto records = DrainTraceRecords();
  REQUIRE(records.size() == 3);
  CHECK(records[0].name == TraceName::InputAccepted);
  CHECK(records[0].phase == TracePhase::Instant);
  CHECK(records[0].thread == TraceThread::Browser);
  CHECK(records[0].timestampUs == 1'000);
  const std::uint16_t correlation =
      static_cast<std::uint16_t>(records[0].value);
  REQUIRE(correlation != 0);
  CHECK(records[1].name == TraceName::InputPresented);
  CHECK(records[1].phase == TracePhase::Instant);
  CHECK(records[1].thread == TraceThread::Application);
  CHECK(records[1].timestampUs == 1'175);
  CHECK(records[1].value == correlation);
  CHECK(records[2].name == TraceName::InputToFrameLatencyUs);
  CHECK(records[2].phase == TracePhase::Counter);
  CHECK(records[2].value == 175);
  CHECK(records[2].flags == correlation);

  InputMap::SetAction(3, false);
  InputMap::AcknowledgeAction(3, false);
  InputMap::ResetQueueForTesting();
  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(nullptr);
}

TEST_CASE("WASM input tracing does not attach a coalesced press to a later SDL "
          "event") {
  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  InputMap::SetQueueForTesting(&AcceptInputEvent);
  inputQueueAccepts = true;
  InputMap::ReleaseAllActions();
  Profiler::SetClock(InputNow);
  inputTime = 3'000;
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Input));

  inputQueueAccepts = false;
  CHECK_FALSE(InputMap::SetAction(2, true));
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 1);
  inputTime = 3'010;
  CHECK(InputMap::SetAction(2, false));
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 0);

  inputQueueAccepts = true;
  inputTime = 3'100;
  REQUIRE(InputMap::SetAction(2, true));
  std::uint16_t decodedAction = 0;
  bool decodedPressed = false;
  REQUIRE(InputMap::DecodeActionEvent((2U << 1U) | 1U, decodedAction,
                                      decodedPressed));
  inputTime = 3'160;
  InputFrameLatencyTracker::PresentedFrame();
  InputMap::AcknowledgeAction(2, true);
  Profiler::Stop();

  const auto records = DrainTraceRecords();
  REQUIRE(records.size() == 5);
  CHECK(records[0].name == TraceName::InputAccepted);
  CHECK(records[1].name == TraceName::InputLatencyDropped);
  CHECK(records[1].value == records[0].value);
  CHECK((records[1].flags &
         static_cast<std::uint16_t>(TraceFlag::InputCoalesced)) != 0);
  CHECK(records[2].name == TraceName::InputAccepted);
  CHECK(records[3].name == TraceName::InputPresented);
  CHECK(records[3].value == records[2].value);
  CHECK(records[4].name == TraceName::InputToFrameLatencyUs);
  CHECK(records[4].value == 60);
  CHECK(records[4].flags == records[2].value);

  InputMap::SetAction(2, false);
  InputMap::AcknowledgeAction(2, false);
  InputMap::ResetQueueForTesting();
  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(nullptr);
}

TEST_CASE("WASM input tracing emits one correlated latency for every press in "
          "a committed frame") {
  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(InputNow);
  inputTime = 5'000;
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Input));

  InputFrameLatencyTracker::AcceptPress(1);
  inputTime = 5'020;
  InputFrameLatencyTracker::AcceptPress(6);
  InputFrameLatencyTracker::MarkDispatching(1, true);
  InputFrameLatencyTracker::MarkDispatching(6, true);
  inputTime = 5'100;
  InputFrameLatencyTracker::PresentedFrame();
  Profiler::Stop();

  const auto records = DrainTraceRecords();
  REQUIRE(records.size() == 6);
  REQUIRE(records[0].name == TraceName::InputAccepted);
  REQUIRE(records[1].name == TraceName::InputAccepted);
  CHECK(records[2].name == TraceName::InputPresented);
  CHECK(records[2].value == records[0].value);
  CHECK(records[3].name == TraceName::InputToFrameLatencyUs);
  CHECK(records[3].value == 100);
  CHECK(records[3].flags == records[0].value);
  CHECK(records[4].name == TraceName::InputPresented);
  CHECK(records[4].value == records[1].value);
  CHECK(records[5].name == TraceName::InputToFrameLatencyUs);
  CHECK(records[5].value == 80);
  CHECK(records[5].flags == records[1].value);

  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(nullptr);
}

TEST_CASE("WASM input latency tickets drop newest presses explicitly at fixed "
          "capacity") {
  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(InputNow);
  inputTime = 10'000;
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Input));

  for (std::size_t index = 0; index < InputFrameLatencyTracker::Capacity + 1U;
       ++index) {
    inputTime += 1;
    InputFrameLatencyTracker::AcceptPress(
        static_cast<std::uint16_t>(index % 11U));
  }
  CHECK(InputFrameLatencyTracker::PendingForTesting() ==
        InputFrameLatencyTracker::Capacity);
  CHECK(InputFrameLatencyTracker::OverflowForTesting() == 1);
  Profiler::Stop();

  const auto records = DrainTraceRecords();
  REQUIRE(records.size() == InputFrameLatencyTracker::Capacity + 2U);
  CHECK(records.back().name == TraceName::InputLatencyDropped);
  CHECK(records.back().phase == TracePhase::Instant);
  CHECK(records.back().thread == TraceThread::Browser);
  CHECK((records.back().flags &
         static_cast<std::uint16_t>(TraceFlag::InputOverflow)) != 0);

  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(nullptr);
}

TEST_CASE("WASM input latency retires dispatched presses that never present") {
  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(InputNow);
  inputTime = 20'000;
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Input));

  InputFrameLatencyTracker::AcceptPress(6);
  InputFrameLatencyTracker::MarkDispatching(6, true);
  inputTime += InputFrameLatencyTracker::NoPresentationTimeoutUs;
  InputFrameLatencyTracker::ObserveNoPresentation();
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 0);
  Profiler::Stop();

  const auto records = DrainTraceRecords();
  REQUIRE(records.size() == 2);
  CHECK(records[0].name == TraceName::InputAccepted);
  CHECK(records[1].name == TraceName::InputLatencyDropped);
  CHECK(records[1].timestampUs == inputTime);
  CHECK(records[1].value == records[0].value);
  CHECK((records[1].flags &
         static_cast<std::uint16_t>(TraceFlag::InputNoPresentation)) != 0);
  CHECK_FALSE(std::any_of(
      records.begin(), records.end(), [](const DecodedTraceRecord &record) {
        return record.name == TraceName::InputToFrameLatencyUs;
      }));

  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(nullptr);
}

TEST_CASE("WASM input latency slots remain bounded under browser and "
          "application contention") {
  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  concurrentInputTime.store(100, std::memory_order_relaxed);
  Profiler::SetClock(ConcurrentInputNow);
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Input));

  constexpr std::size_t transitionsPerProducer = 2'048;
  std::atomic<std::uint32_t> producersRemaining{2};
  const auto produce = [&] {
    for (std::size_t index = 0; index < transitionsPerProducer; ++index) {
      InputFrameLatencyTracker::AcceptPress(
          static_cast<std::uint16_t>(index % 11U));
    }
    producersRemaining.fetch_sub(1U, std::memory_order_release);
  };
  std::thread producerA(produce);
  std::thread producerB(produce);
  std::thread application([&] {
    while (producersRemaining.load(std::memory_order_acquire) != 0U) {
      for (std::uint16_t action = 0; action < 11U; ++action) {
        InputFrameLatencyTracker::MarkDispatching(action, true);
      }
      InputFrameLatencyTracker::PresentedFrame();
    }
  });
  producerA.join();
  producerB.join();
  application.join();

  for (std::size_t pass = 0; pass < InputFrameLatencyTracker::Capacity;
       ++pass) {
    for (std::uint16_t action = 0; action < 11U; ++action) {
      InputFrameLatencyTracker::MarkDispatching(action, true);
    }
  }
  InputFrameLatencyTracker::PresentedFrame();
  CHECK(InputFrameLatencyTracker::PendingForTesting() == 0);
  CHECK(InputFrameLatencyTracker::OverflowForTesting() <=
        transitionsPerProducer * 2U);

  Profiler::Stop();
  InputFrameLatencyTracker::ResetForTesting();
  Profiler::SetClock(nullptr);
}

TEST_CASE("WASM benchmark has deterministic percentiles and "
          "tracing-independent audio") {
  const WasmBenchmarkConfig config{WasmBenchmarkConfig::Version, 32, 4, 15};
  Profiler::Stop();
  benchmarkTime = 0;
  const WasmBenchmarkResult disabled = WasmBenchmark::Run(config, BenchmarkNow);
  Profiler::Start(static_cast<std::uint32_t>(TraceCategory::Benchmark));
  benchmarkTime = 0;
  const WasmBenchmarkResult enabled = WasmBenchmark::Run(config, BenchmarkNow);
  Profiler::Stop();
  benchmarkTime = 0;
  const WasmBenchmarkResult cold = WasmBenchmark::Run(
      {WasmBenchmarkConfig::Version, 32, 0, 15}, BenchmarkNow);
  benchmarkTime = 0;
  const WasmBenchmarkResult fullyWarmed = WasmBenchmark::Run(
      {WasmBenchmarkConfig::Version, 32, 64, 15}, BenchmarkNow);
  CHECK(disabled.sampleCount == 32);
  CHECK(disabled.medianUs == 10);
  CHECK(disabled.p95Us == 10);
  CHECK(disabled.p99Us == 10);
  CHECK(disabled.maximumUs == 10);
  CHECK(disabled.deadlineMisses == 0);
  CHECK(disabled.totalWork == 32U * 128U * 8U);
  CHECK(disabled.fixtureHash == 0xc45e4b1cU);
  CHECK(enabled.fixtureHash == disabled.fixtureHash);
  CHECK(enabled.totalWork == disabled.totalWork);
  CHECK(enabled.sampleCount == disabled.sampleCount);
  CHECK(enabled.deadlineMisses == disabled.deadlineMisses);
  // Warm-up exercises the same kernel but resets every fixture phase/hash
  // before measured rendering, so it cannot alter a single hashed PCM byte.
  CHECK(cold.fixtureHash == disabled.fixtureHash);
  CHECK(fullyWarmed.fixtureHash == disabled.fixtureHash);
}
} // namespace

TEST_CASE("WASM trace ring preserves published records and reports overload") {
  TraceRingBuffer<2> ring;
  ring.Push(Event(1, TracePhase::Begin));
  ring.Push(Event(2, TracePhase::End));
  ring.Push(Event(3, TracePhase::Begin));
  CHECK(ring.Dropped() == 1);
  std::array<TraceRecord, 2> records{};
  CHECK(ring.Pop(records) == 2);
  CHECK(records[0].timestampUs == 1);
  CHECK(records[0].phase == TracePhase::Begin);
  CHECK(records[1].timestampUs == 2);
  CHECK(records[1].phase == TracePhase::End);
  CHECK(records[1].sequence > records[0].sequence);

  ring.Push(Event(4, TracePhase::Instant));
  TraceRecord resumed{};
  REQUIRE(ring.TryPop(resumed));
  CHECK(resumed.timestampUs == 4);
  CHECK(resumed.sequence == 3);
}

TEST_CASE("WASM trace ring clear preserves monotonic publication identities") {
  TraceRingBuffer<4> ring;
  ring.Push(Event(1, TracePhase::Instant));
  ring.ClearConsumer();
  ring.Push(Event(2, TracePhase::Instant));
  TraceRecord record{};
  REQUIRE(ring.TryPop(record));
  CHECK(record.timestampUs == 2);
  CHECK(record.sequence == 2);
  CHECK_FALSE(ring.TryPop(record));
}

TEST_CASE("WASM trace ring copies atomically published fields under "
          "two-producer contention") {
  TraceRingBuffer<64> ring;
  constexpr std::size_t eventCount = 32'768;
  std::atomic<std::uint32_t> producersRemaining{2};
  std::atomic<std::uint64_t> nextTimestamp{1};
  std::atomic<bool> valid{true};
  std::atomic<std::uint64_t> lastSequence{0};
  std::atomic<std::uint64_t> consumed{0};
  const auto produce = [&] {
    for (std::size_t index = 0; index < eventCount / 2; ++index) {
      const std::uint64_t timestamp =
          nextTimestamp.fetch_add(1, std::memory_order_relaxed);
      TraceRecord record = Event(timestamp, TracePhase::Counter);
      record.value = static_cast<std::uint32_t>(timestamp ^ 0x55aa55aaU);
      record.generation = static_cast<std::uint32_t>(timestamp);
      ring.Push(record);
    }
    producersRemaining.fetch_sub(1, std::memory_order_release);
  };
  std::thread producerA(produce);
  std::thread producerB(produce);
  std::thread consumer([&] {
    while (producersRemaining.load(std::memory_order_acquire) != 0U) {
      TraceRecord record{};
      if (!ring.TryPop(record)) {
        std::this_thread::yield();
        continue;
      }
      const std::uint64_t previous =
          lastSequence.exchange(record.sequence, std::memory_order_relaxed);
      if (record.sequence <= previous)
        valid.store(false, std::memory_order_relaxed);
      consumed.fetch_add(1U, std::memory_order_relaxed);
      if (record.value !=
              static_cast<std::uint32_t>(record.timestampUs ^ 0x55aa55aaU) ||
          record.generation != record.timestampUs)
        valid.store(false, std::memory_order_relaxed);
    }
    TraceRecord record{};
    while (ring.TryPop(record)) {
      const std::uint64_t previous =
          lastSequence.exchange(record.sequence, std::memory_order_relaxed);
      if (record.sequence <= previous)
        valid.store(false, std::memory_order_relaxed);
      consumed.fetch_add(1U, std::memory_order_relaxed);
      if (record.value !=
              static_cast<std::uint32_t>(record.timestampUs ^ 0x55aa55aaU) ||
          record.generation != record.timestampUs)
        valid.store(false, std::memory_order_relaxed);
    }
  });
  producerA.join();
  producerB.join();
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
  CHECK(consumed.load(std::memory_order_relaxed) == ring.Written());
  CHECK(consumed.load(std::memory_order_relaxed) + ring.Dropped() ==
        eventCount);
  CHECK(lastSequence.load(std::memory_order_relaxed) == ring.Written());
}
