/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

// A browser-visible frame publication point. One UI producer writes the
// inactive snapshot while the odd sequence value tells JS readers to retry;
// publishing an even value releases a complete frame. Browser readers must
// sample sequence, copy bytes, then sample the same even sequence again.
template <std::size_t Size> class WasmFrameSnapshot {
public:
  static_assert(Size % sizeof(std::uint32_t) == 0U);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  void Publish(std::span<const std::uint8_t, Size> frame) noexcept {
    const std::uint32_t writing =
        sequence_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    for (std::size_t index = 0; index < words_.size(); ++index) {
      const std::size_t offset = index * sizeof(std::uint32_t);
      const std::uint32_t word =
          static_cast<std::uint32_t>(frame[offset]) |
          (static_cast<std::uint32_t>(frame[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(frame[offset + 2U]) << 16U) |
          (static_cast<std::uint32_t>(frame[offset + 3U]) << 24U);
      words_[index].store(word, std::memory_order_relaxed);
    }
    sequence_.store(writing + 1U, std::memory_order_release);
  }

  const std::uint8_t *Data() const noexcept {
    return reinterpret_cast<const std::uint8_t *>(words_.data());
  }
  std::uint32_t Sequence() const noexcept {
    return sequence_.load(std::memory_order_acquire);
  }
  const std::uint32_t *SequenceAddress() const noexcept {
    return reinterpret_cast<const std::uint32_t *>(&sequence_);
  }

private:
  std::array<std::atomic<std::uint32_t>, Size / sizeof(std::uint32_t)> words_{};
  alignas(std::uint32_t) std::atomic<std::uint32_t> sequence_{0U};
};
