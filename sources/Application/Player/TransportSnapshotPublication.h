/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// One serialized producer publishes whole, trivially-copyable values to any
// number of lock-free readers. Payload words are atomic so a reader may safely
// retry when its two sequence samples do not identify one complete frame.
//
// Every operation is a lock-free 32-bit atomic operation. Publish has a fixed
// amount of work and cannot reject a control edge when readers are delayed;
// Capture never takes the producer-side audio mutex. The fences surrounding
// relaxed payload words are intentional: if a reader observes any word from a
// new frame, the writer's release fence synchronizes with the reader's acquire
// fence. The odd sequence transition then happens-before the final sequence
// load, which therefore cannot still accept the preceding even sequence.
// As with any finite sequence counter, one Capture attempt must not span
// 2^31 complete publications; Player's bounded <=128-byte copy makes that
// interval many orders of magnitude larger than the Node UI reader section.
template <typename Snapshot> class TransportSnapshotPublication final {
  static_assert(std::is_trivially_copyable_v<Snapshot>);
  static_assert(std::has_unique_object_representations_v<Snapshot>,
                "transport snapshots must not contain padding bits");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "transport publication requires lock-free 32-bit atomics");
  static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t),
                "transport publication words must remain tightly packed");

public:
  TransportSnapshotPublication() noexcept {
    for (auto &word : payload_)
      word.store(0U, std::memory_order_relaxed);
  }

  TransportSnapshotPublication(const TransportSnapshotPublication &) = delete;
  TransportSnapshotPublication &
  operator=(const TransportSnapshotPublication &) = delete;

  // Publish must be serialized with every other Publish call. Player uses the
  // existing MixerService mutex for that producer contract.
  void Publish(const Snapshot &snapshot) noexcept {
    WordArray words{};
    std::memcpy(words.data(), &snapshot, sizeof(snapshot));

    const std::uint32_t writing =
        sequence_.load(std::memory_order_relaxed) + 1U;
    sequence_.store(writing, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t index = 0U; index < kWordCount; ++index)
      payload_[index].store(words[index], std::memory_order_relaxed);
    sequence_.store(writing + 1U, std::memory_order_release);
  }

  [[nodiscard]] Snapshot Capture() const noexcept {
    WordArray words{};
    for (;;) {
      const std::uint32_t before =
          sequence_.load(std::memory_order_acquire);
      if ((before & 1U) != 0U)
        continue;

      for (std::size_t index = 0U; index < kWordCount; ++index)
        words[index] = payload_[index].load(std::memory_order_relaxed);

      std::atomic_thread_fence(std::memory_order_acquire);
      const std::uint32_t after = sequence_.load(std::memory_order_relaxed);
      if (before == after) {
        Snapshot snapshot{};
        std::memcpy(&snapshot, words.data(), sizeof(snapshot));
        return snapshot;
      }
    }
  }

private:
  static constexpr std::size_t kWordCount =
      (sizeof(Snapshot) + sizeof(std::uint32_t) - 1U) /
      sizeof(std::uint32_t);
  using WordArray = std::array<std::uint32_t, kWordCount>;

  std::array<std::atomic<std::uint32_t>, kWordCount> payload_{};
  std::atomic<std::uint32_t> sequence_{0U};
};
