/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_SHARED_TRACE_RING_BUFFER_H
#define PICOTRACKER_SHARED_TRACE_RING_BUFFER_H

#include "System/Console/TraceRecord.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

template <std::size_t CapacityRecords> class TraceRingBuffer {
  static_assert(CapacityRecords != 0 &&
                (CapacityRecords & (CapacityRecords - 1)) == 0);

  struct Slot {
    std::atomic<std::uint64_t> publication{0};
    TraceRecord record{};
  };

public:
  static constexpr std::size_t Capacity = CapacityRecords;

  TraceRingBuffer() noexcept {
    for (std::uint64_t index = 0; index < CapacityRecords; ++index) {
      slots_[index].publication.store(index, std::memory_order_relaxed);
    }
  }

  void Push(TraceRecord record) noexcept {
    std::uint64_t position = writePosition_.load(std::memory_order_relaxed);
    Slot *slot = nullptr;
    for (;;) {
      slot = &slots_[position & (CapacityRecords - 1)];
      const std::uint64_t publication =
          slot->publication.load(std::memory_order_acquire);
      const std::int64_t difference = static_cast<std::int64_t>(publication) -
                                      static_cast<std::int64_t>(position);
      if (difference == 0) {
        if (writePosition_.compare_exchange_weak(position, position + 1U,
                                                 std::memory_order_relaxed,
                                                 std::memory_order_relaxed)) {
          break;
        }
      } else if (difference < 0) {
        // Preserve the already-published history and report overload instead
        // of waiting for the browser consumer or overwriting an active slot.
        dropped_.fetch_add(1U, std::memory_order_relaxed);
        return;
      } else {
        position = writePosition_.load(std::memory_order_relaxed);
      }
    }

    record.sequence = position + 1U;
    slot->record = record;
    slot->publication.store(position + 1U, std::memory_order_release);
  }

  [[nodiscard]] bool TryPop(TraceRecord &record) noexcept {
    std::uint64_t position = readPosition_.load(std::memory_order_relaxed);
    Slot *slot = nullptr;
    for (;;) {
      slot = &slots_[position & (CapacityRecords - 1)];
      const std::uint64_t publication =
          slot->publication.load(std::memory_order_acquire);
      const std::int64_t difference = static_cast<std::int64_t>(publication) -
                                      static_cast<std::int64_t>(position + 1U);
      if (difference == 0) {
        if (readPosition_.compare_exchange_weak(position, position + 1U,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
          break;
        }
      } else if (difference < 0) {
        return false;
      } else {
        position = readPosition_.load(std::memory_order_relaxed);
      }
    }

    record = slot->record;
    slot->publication.store(position + CapacityRecords,
                            std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t Pop(std::span<TraceRecord> output) noexcept {
    std::size_t count = 0;
    while (count < output.size()) {
      TraceRecord record{};
      if (!TryPop(record))
        break;
      output[count++] = record;
    }
    return count;
  }

  void ClearConsumer() noexcept {
    TraceRecord ignored{};
    while (TryPop(ignored)) {
    }
  }
  [[nodiscard]] std::uint64_t Dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t Written() const noexcept {
    return writePosition_.load(std::memory_order_acquire);
  }

private:
  std::array<Slot, CapacityRecords> slots_{};
  alignas(64) std::atomic<std::uint64_t> writePosition_{0};
  alignas(64) std::atomic<std::uint64_t> readPosition_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

#endif
