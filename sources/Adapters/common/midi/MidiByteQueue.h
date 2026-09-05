/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_SHARED_MIDI_BYTE_QUEUE_H
#define PICOTRACKER_SHARED_MIDI_BYTE_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

template <typename Value, std::size_t CapacityEntries> class MidiSpscRing {
  static_assert(CapacityEntries != 0U);
  static_assert((CapacityEntries & (CapacityEntries - 1U)) == 0U,
                "MIDI queue capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<Value>);

public:
  using Position = std::uint64_t;
  static constexpr std::size_t Capacity = CapacityEntries;

  [[nodiscard]] bool Push(const Value &value) noexcept {
    return PushBatch(std::span<const Value>(&value, 1U));
  }

  [[nodiscard]] bool PushBatch(std::span<const Value> values) noexcept {
    if (values.empty())
      return true;
    if (values.size() > CapacityEntries)
      return false;
    const Position write = writePosition_.load(std::memory_order_relaxed);
    const Position read = readPosition_.load(std::memory_order_acquire);
    const std::size_t used = static_cast<std::size_t>(write - read);
    if (used > CapacityEntries || values.size() > CapacityEntries - used) {
      return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      values_[(write + index) & (CapacityEntries - 1U)] = values[index];
    }
    writePosition_.store(write + values.size(), std::memory_order_release);
    return true;
  }

  template <typename Generator>
  [[nodiscard]] bool PushGenerated(std::size_t count,
                                   Generator generator) noexcept {
    if (count == 0U)
      return true;
    if (count > CapacityEntries)
      return false;
    const Position write = writePosition_.load(std::memory_order_relaxed);
    const Position read = readPosition_.load(std::memory_order_acquire);
    const std::size_t used = static_cast<std::size_t>(write - read);
    if (used > CapacityEntries || count > CapacityEntries - used)
      return false;
    for (std::size_t index = 0; index < count; ++index) {
      values_[(write + index) & (CapacityEntries - 1U)] = generator(index);
    }
    writePosition_.store(write + count, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool TryPeek(Value &value) const noexcept {
    const Position read = readPosition_.load(std::memory_order_relaxed);
    const Position write = writePosition_.load(std::memory_order_acquire);
    if (read == write)
      return false;
    value = values_[read & (CapacityEntries - 1U)];
    return true;
  }

  [[nodiscard]] bool TryPop(Value &value) noexcept {
    const Position read = readPosition_.load(std::memory_order_relaxed);
    const Position write = writePosition_.load(std::memory_order_acquire);
    if (read == write)
      return false;
    value = values_[read & (CapacityEntries - 1U)];
    readPosition_.store(read + 1U, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t Pop(std::span<Value> output) noexcept {
    std::size_t count = 0;
    while (count < output.size() && TryPop(output[count]))
      ++count;
    return count;
  }

  [[nodiscard]] std::size_t Size() const noexcept {
    const Position write = writePosition_.load(std::memory_order_acquire);
    const Position read = readPosition_.load(std::memory_order_acquire);
    const std::size_t size = static_cast<std::size_t>(write - read);
    return size <= CapacityEntries ? size : 0U;
  }

  // Consumer-only flush. The producer may continue after this release store;
  // already-published values are discarded and positions remain monotonic.
  void DiscardConsumer() noexcept {
    readPosition_.store(writePosition_.load(std::memory_order_acquire),
                        std::memory_order_release);
  }

private:
  alignas(64) std::array<Value, CapacityEntries> values_{};
  alignas(64) std::atomic<Position> writePosition_{0U};
  alignas(64) std::atomic<Position> readPosition_{0U};
};

struct MidiByteRecord {
  std::uint8_t byte = 0;
  bool batchStart = false;
  double timestampMilliseconds = 0.0;
  std::uint64_t sequence = 0U;
  // The browser-producer acceptance boundary is separate from the original
  // DOM event timestamp above. Every byte carries the same fixed metadata so
  // a batch remains correlated even when it crosses application polls.
  double acceptedMilliseconds = 0.0;
  std::uint32_t traceGeneration = 0U;
  std::uint16_t traceCorrelation = 0U;
};

static_assert(std::is_trivially_copyable_v<MidiByteRecord>);

template <std::size_t CapacityBytes> class MidiByteQueue {
public:
  [[nodiscard]] bool Push(std::span<const std::uint8_t> bytes,
                          double timestampMilliseconds,
                          double acceptedMilliseconds = 0.0,
                          std::uint32_t traceGeneration = 0U,
                          std::uint16_t traceCorrelation = 0U) noexcept {
    if (bytes.empty())
      return true;
    if (bytes.size() > CapacityBytes) {
      dropped_.fetch_add(bytes.size(), std::memory_order_relaxed);
      return false;
    }
    const std::uint64_t firstSequence = nextSequence_;
    if (!ring_.PushGenerated(bytes.size(), [&](std::size_t index) {
          MidiByteRecord record{};
          record.byte = bytes[index];
          record.batchStart = index == 0U;
          record.timestampMilliseconds = timestampMilliseconds;
          record.sequence = firstSequence + index;
          record.acceptedMilliseconds = acceptedMilliseconds;
          record.traceGeneration = traceGeneration;
          record.traceCorrelation = traceCorrelation;
          return record;
        })) {
      dropped_.fetch_add(bytes.size(), std::memory_order_relaxed);
      return false;
    }
    nextSequence_ += bytes.size();
    return true;
  }

  [[nodiscard]] bool TryPop(MidiByteRecord &record) noexcept {
    return ring_.TryPop(record);
  }
  [[nodiscard]] std::size_t Pop(std::span<MidiByteRecord> output) noexcept {
    return ring_.Pop(output);
  }
  [[nodiscard]] std::size_t Size() const noexcept { return ring_.Size(); }
  [[nodiscard]] std::uint64_t Dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }
  void DiscardConsumer() noexcept { ring_.DiscardConsumer(); }

private:
  MidiSpscRing<MidiByteRecord, CapacityBytes> ring_{};
  std::uint64_t nextSequence_ = 0U;
  std::atomic<std::uint64_t> dropped_{0U};
};

struct MidiPacket {
  std::array<std::uint8_t, 3> bytes{};
  std::uint8_t length = 0;
  double timestampMilliseconds = 0.0;
  std::uint64_t sequence = 0U;
  // Queue latency is measured from this independent producer timestamp. The
  // scheduled timestamp above remains the value passed to MIDIOutput.send().
  double enqueuedMilliseconds = 0.0;
  std::uint32_t traceGeneration = 0U;
  std::uint16_t traceCorrelation = 0U;
};

static_assert(std::is_trivially_copyable_v<MidiPacket>);

template <std::size_t NormalCapacity, std::size_t RealtimeCapacity>
class MidiPacketQueue {
public:
  [[nodiscard]] bool Push(MidiPacket packet) noexcept {
    packet.sequence = nextSequence_++;
    const bool realtime = packet.length != 0U && packet.bytes[0] >= 0xF8U;
    if ((realtime ? realtime_.Push(packet) : normal_.Push(packet)))
      return true;
    (realtime ? droppedRealtime_ : droppedNormal_)
        .fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  [[nodiscard]] bool TryPop(MidiPacket &packet) noexcept {
    MidiPacket normal{};
    MidiPacket realtime{};
    const bool hasNormal = normal_.TryPeek(normal);
    const bool hasRealtime = realtime_.TryPeek(realtime);
    if (!hasNormal && !hasRealtime)
      return false;
    if (hasNormal && (!hasRealtime || normal.sequence < realtime.sequence)) {
      return normal_.TryPop(packet);
    }
    return realtime_.TryPop(packet);
  }

  [[nodiscard]] std::size_t Pop(std::span<MidiPacket> output) noexcept {
    std::size_t count = 0;
    while (count < output.size() && TryPop(output[count]))
      ++count;
    return count;
  }

  [[nodiscard]] std::uint64_t DroppedNormal() const noexcept {
    return droppedNormal_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t DroppedRealtime() const noexcept {
    return droppedRealtime_.load(std::memory_order_relaxed);
  }
  void DiscardConsumer() noexcept {
    normal_.DiscardConsumer();
    realtime_.DiscardConsumer();
  }

private:
  MidiSpscRing<MidiPacket, NormalCapacity> normal_{};
  MidiSpscRing<MidiPacket, RealtimeCapacity> realtime_{};
  std::uint64_t nextSequence_ = 0U;
  std::atomic<std::uint64_t> droppedNormal_{0U};
  std::atomic<std::uint64_t> droppedRealtime_{0U};
};

#endif
