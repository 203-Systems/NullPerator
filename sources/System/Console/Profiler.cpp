#include "Profiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
std::uint64_t DefaultNow() {
  using Clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now().time_since_epoch())
          .count());
}

template <typename Value> void Store(std::uint8_t *destination, Value value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  std::memcpy(destination, &value, sizeof(value));
}
} // namespace

TraceRingBuffer<Profiler::RingCapacity> Profiler::ring_{};
std::atomic<std::uint32_t> Profiler::mask_{0};
std::atomic<std::uint32_t> Profiler::generation_{0};
std::atomic<std::uint64_t> Profiler::droppedBase_{0};
std::atomic<Profiler::NowFunction> Profiler::now_{DefaultNow};
std::array<std::uint8_t,
           Profiler::DrainHeaderBytes +
               Profiler::DrainCapacity * Profiler::DrainRecordBytes>
    Profiler::drain_{};

std::uint32_t Profiler::Start(std::uint32_t mask) noexcept {
  mask_.store(0, std::memory_order_release);
  const std::uint32_t generation =
      generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  ring_.ClearConsumer();
  droppedBase_.store(ring_.Dropped(), std::memory_order_relaxed);
  mask_.store(mask & AllCategoriesMask, std::memory_order_release);
  return generation;
}

std::uint32_t Profiler::Stop() noexcept {
  mask_.store(0, std::memory_order_release);
  return generation_.load(std::memory_order_acquire);
}

bool Profiler::CategoryEnabled(TraceCategory category) noexcept {
  return (mask_.load(std::memory_order_relaxed) &
          static_cast<std::uint32_t>(category)) != 0;
}

std::uint32_t Profiler::Generation() noexcept {
  return generation_.load(std::memory_order_acquire);
}
std::uint64_t Profiler::Now() noexcept {
  const NowFunction now = now_.load(std::memory_order_relaxed);
  return now == nullptr ? 0 : now();
}

void Profiler::Emit(TraceCategory category, TraceName name, TracePhase phase,
                    std::uint32_t value, TraceThread thread,
                    std::uint32_t expectedGeneration,
                    std::uint16_t flags) noexcept {
  const std::uint32_t generation = generation_.load(std::memory_order_relaxed);
  if ((expectedGeneration != 0 && expectedGeneration != generation) ||
      !CategoryEnabled(category))
    return;
  EmitAt(Now(), category, name, phase, value, thread, expectedGeneration,
         flags);
}

void Profiler::EmitAt(std::uint64_t timestampUs, TraceCategory category,
                      TraceName name, TracePhase phase, std::uint32_t value,
                      TraceThread thread, std::uint32_t expectedGeneration,
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

std::uint64_t Profiler::TimestampNow() noexcept { return Now(); }

std::uintptr_t Profiler::Drain() noexcept {
  const std::uint32_t generation = Generation();
  std::array<TraceRecord, DrainCapacity> records{};
  std::uint32_t count = 0;
  TraceRecord candidate{};
  while (count < records.size() && ring_.TryPop(candidate)) {
    if (candidate.generation == generation)
      records[count++] = candidate;
  }
  Store<std::uint32_t>(drain_.data(), 1);
  Store<std::uint32_t>(drain_.data() + 4, DrainHeaderBytes);
  Store<std::uint32_t>(drain_.data() + 8, DrainRecordBytes);
  Store<std::uint32_t>(drain_.data() + 12, count);
  const std::uint64_t dropped =
      ring_.Dropped() - droppedBase_.load(std::memory_order_relaxed);
  Store<std::uint64_t>(drain_.data() + 16, dropped);
  Store<std::uint32_t>(drain_.data() + 24,
                       mask_.load(std::memory_order_acquire));
  Store<std::uint32_t>(drain_.data() + 28, generation);
  Store<std::uint32_t>(drain_.data() + 32,
                       mask_.load(std::memory_order_relaxed) != 0 ? 1U : 0U);
  Store<std::uint32_t>(drain_.data() + 36, 0);
  for (std::uint32_t index = 0; index < count; ++index) {
    const TraceRecord &record = records[index];
    std::uint8_t *target =
        drain_.data() + DrainHeaderBytes + index * DrainRecordBytes;
    Store<std::uint64_t>(target, record.sequence);
    Store<std::uint64_t>(target + 8, record.timestampUs);
    Store<std::uint32_t>(target + 16, record.value);
    Store<std::uint32_t>(target + 20, record.generation);
    Store<std::uint16_t>(target + 24,
                         static_cast<std::uint16_t>(record.category));
    Store<std::uint16_t>(target + 26, static_cast<std::uint16_t>(record.name));
    target[28] = static_cast<std::uint8_t>(record.phase);
    target[29] = static_cast<std::uint8_t>(record.thread);
    Store<std::uint16_t>(target + 30, record.flags);
  }
  return reinterpret_cast<std::uintptr_t>(drain_.data());
}

void Profiler::SetClock(NowFunction now) noexcept {
  now_.store(now == nullptr ? DefaultNow : now, std::memory_order_relaxed);
}
std::uint64_t Profiler::WrittenForTesting() noexcept { return ring_.Written(); }

ProfileScope::ProfileScope(TraceCategory category, TraceName name) noexcept
    : category_(category), name_(name) {
  if (!Profiler::CategoryEnabled(category_))
    return;
  generation_ = Profiler::Generation();
  enabled_ = true;
  Profiler::Emit(category_, name_, TracePhase::Begin, 0,
                 TraceThread::Application, generation_);
}

ProfileScope::~ProfileScope() {
  if (enabled_)
    Profiler::Emit(category_, name_, TracePhase::End, 0,
                   TraceThread::Application, generation_);
}
