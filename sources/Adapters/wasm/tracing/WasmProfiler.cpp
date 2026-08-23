/* SPDX-License-Identifier: BSD-3-Clause */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_TRACE_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_TRACE_EXPORT
#endif

#include "WasmProfiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
std::uint64_t DefaultNow() {
#ifdef __EMSCRIPTEN__
  const double micros = emscripten_get_now() * 1000.0;
  return micros > 0.0 ? static_cast<std::uint64_t>(micros) : 0;
#else
  using Clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      Clock::now().time_since_epoch()).count());
#endif
}

template <typename Value> void Store(std::uint8_t *destination, Value value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  std::memcpy(destination, &value, sizeof(value));
}
} // namespace

TraceRingBuffer<WasmProfiler::RingCapacity> WasmProfiler::ring_{};
std::atomic<std::uint32_t> WasmProfiler::mask_{0};
std::atomic<std::uint32_t> WasmProfiler::generation_{0};
std::atomic<std::uint64_t> WasmProfiler::droppedBase_{0};
std::atomic<WasmProfiler::NowFunction> WasmProfiler::now_{DefaultNow};
std::array<std::uint8_t, WasmProfiler::DrainHeaderBytes + WasmProfiler::DrainCapacity * WasmProfiler::DrainRecordBytes> WasmProfiler::drain_{};

std::uint32_t WasmProfiler::Start(std::uint32_t mask) noexcept {
  mask_.store(0, std::memory_order_release);
  const std::uint32_t generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  ring_.ClearConsumer();
  droppedBase_.store(ring_.Dropped(), std::memory_order_relaxed);
  mask_.store(mask & AllCategoriesMask, std::memory_order_release);
  return generation;
}

std::uint32_t WasmProfiler::Stop() noexcept {
  mask_.store(0, std::memory_order_release);
  return generation_.load(std::memory_order_acquire);
}

bool WasmProfiler::CategoryEnabled(WasmTraceCategory category) noexcept {
  return (mask_.load(std::memory_order_relaxed) & static_cast<std::uint32_t>(category)) != 0;
}

std::uint32_t WasmProfiler::Generation() noexcept { return generation_.load(std::memory_order_acquire); }
std::uint64_t WasmProfiler::Now() noexcept {
  const NowFunction now = now_.load(std::memory_order_relaxed);
  return now == nullptr ? 0 : now();
}

void WasmProfiler::Emit(WasmTraceCategory category, WasmTraceName name,
                        WasmTracePhase phase, std::uint32_t value,
                        WasmTraceThread thread,
                        std::uint32_t expectedGeneration,
                        std::uint16_t flags) noexcept {
  const std::uint32_t generation = generation_.load(std::memory_order_relaxed);
  if ((expectedGeneration != 0 && expectedGeneration != generation) || !CategoryEnabled(category)) return;
  EmitAt(Now(), category, name, phase, value, thread, expectedGeneration,
         flags);
}

void WasmProfiler::EmitAt(std::uint64_t timestampUs,
                          WasmTraceCategory category, WasmTraceName name,
                          WasmTracePhase phase, std::uint32_t value,
                          WasmTraceThread thread,
                          std::uint32_t expectedGeneration,
                          std::uint16_t flags) noexcept {
  const std::uint32_t generation = generation_.load(std::memory_order_relaxed);
  if ((expectedGeneration != 0 && expectedGeneration != generation) ||
      !CategoryEnabled(category)) {
    return;
  }
  TraceRecord record{};
  record.timestampUs = timestampUs;
  record.value = value;
  record.generation = generation;
  record.category = category;
  record.name = name;
  record.phase = phase;
  record.thread = thread;
  record.flags = flags;
  ring_.Push(record);
}

std::uint64_t WasmProfiler::TimestampNow() noexcept { return Now(); }

std::uintptr_t WasmProfiler::Drain() noexcept {
  const std::uint32_t generation = Generation();
  std::array<TraceRecord, DrainCapacity> records{};
  std::uint32_t count = 0;
  TraceRecord candidate{};
  while (count < records.size() && ring_.TryPop(candidate)) {
    if (candidate.generation == generation) records[count++] = candidate;
  }
  Store<std::uint32_t>(drain_.data(), 1);
  Store<std::uint32_t>(drain_.data() + 4, DrainHeaderBytes);
  Store<std::uint32_t>(drain_.data() + 8, DrainRecordBytes);
  Store<std::uint32_t>(drain_.data() + 12, count);
  const std::uint64_t dropped = ring_.Dropped() - droppedBase_.load(std::memory_order_relaxed);
  Store<std::uint64_t>(drain_.data() + 16, dropped);
  Store<std::uint32_t>(drain_.data() + 24, mask_.load(std::memory_order_acquire));
  Store<std::uint32_t>(drain_.data() + 28, generation);
  Store<std::uint32_t>(drain_.data() + 32, mask_.load(std::memory_order_relaxed) != 0 ? 1U : 0U);
  Store<std::uint32_t>(drain_.data() + 36, 0);
  for (std::uint32_t index = 0; index < count; ++index) {
    const TraceRecord &record = records[index];
    std::uint8_t *target = drain_.data() + DrainHeaderBytes + index * DrainRecordBytes;
    Store<std::uint64_t>(target, record.sequence);
    Store<std::uint64_t>(target + 8, record.timestampUs);
    Store<std::uint32_t>(target + 16, record.value);
    Store<std::uint32_t>(target + 20, record.generation);
    Store<std::uint16_t>(target + 24, static_cast<std::uint16_t>(record.category));
    Store<std::uint16_t>(target + 26, static_cast<std::uint16_t>(record.name));
    target[28] = static_cast<std::uint8_t>(record.phase);
    target[29] = static_cast<std::uint8_t>(record.thread);
    Store<std::uint16_t>(target + 30, record.flags);
  }
  return reinterpret_cast<std::uintptr_t>(drain_.data());
}

void WasmProfiler::SetClockForTesting(NowFunction now) noexcept { now_.store(now == nullptr ? DefaultNow : now, std::memory_order_relaxed); }
std::uint64_t WasmProfiler::WrittenForTesting() noexcept { return ring_.Written(); }

WasmTraceScope::WasmTraceScope(WasmTraceCategory category, WasmTraceName name) noexcept
    : category_(category), name_(name) {
  if (!WasmProfiler::CategoryEnabled(category_)) return;
  generation_ = WasmProfiler::Generation();
  enabled_ = true;
  WasmProfiler::Emit(category_, name_, WasmTracePhase::Begin, 0,
                     WasmTraceThread::Application, generation_);
}

WasmTraceScope::~WasmTraceScope() {
  if (enabled_) WasmProfiler::Emit(category_, name_, WasmTracePhase::End, 0,
                                   WasmTraceThread::Application, generation_);
}

extern "C" WASM_TRACE_EXPORT std::uint32_t PicoTracker_Wasm_TraceStart(std::uint32_t mask) { return WasmProfiler::Start(mask); }
extern "C" WASM_TRACE_EXPORT std::uint32_t PicoTracker_Wasm_TraceStop() { return WasmProfiler::Stop(); }
extern "C" WASM_TRACE_EXPORT std::uintptr_t PicoTracker_Wasm_TraceDrain() { return WasmProfiler::Drain(); }
extern "C" WASM_TRACE_EXPORT void PicoTracker_Wasm_TraceStorageSync(
    std::uint32_t phase, std::uint32_t syncId, std::uint32_t flags,
    std::uint32_t expectedGeneration) {
  if (phase > static_cast<std::uint32_t>(WasmTracePhase::End) || syncId == 0)
    return;
  constexpr std::uint32_t AllowedFlags =
      static_cast<std::uint32_t>(WasmTraceFlag::Success) |
      static_cast<std::uint32_t>(WasmTraceFlag::Failure) |
      static_cast<std::uint32_t>(WasmTraceFlag::Populate);
  WasmProfiler::Emit(
      WasmTraceCategory::Storage, WasmTraceName::StorageSync,
      static_cast<WasmTracePhase>(phase), syncId, WasmTraceThread::Browser,
      expectedGeneration, static_cast<std::uint16_t>(flags & AllowedFlags));
}
