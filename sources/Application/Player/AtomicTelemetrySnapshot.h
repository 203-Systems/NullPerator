/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 *
 * This file is part of the picoTracker firmware
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Lock-free publication for one fixed-size telemetry writer and any number of
// readers. Every payload word is atomic, so a reader that retries an odd or
// changed sequence never performs a C++ data race while rejecting a torn copy.
// Callers must serialize multiple producers before Publish().
template <typename Value> class AtomicTelemetrySnapshot final {
public:
  static_assert(std::is_trivially_copyable_v<Value>);
  static constexpr std::size_t WordBytes = sizeof(std::uint32_t);
  static constexpr std::size_t WordCount =
      (sizeof(Value) + WordBytes - 1U) / WordBytes;

  void Publish(const Value &value) noexcept {
    const std::uint32_t writing =
        sequence_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);
    for (std::size_t index = 0U; index < WordCount; ++index) {
      const std::size_t offset = index * WordBytes;
      const std::size_t remaining = sizeof(Value) - offset;
      const std::size_t count = remaining < WordBytes ? remaining : WordBytes;
      std::uint32_t word = 0U;
      std::memcpy(&word, bytes + offset, count);
      // Release/acquire on the payload words prevents a reader from accepting
      // a newly visible word while it still observes the previous even epoch.
      words_[index].store(word, std::memory_order_release);
    }
    sequence_.store(writing + 1U, std::memory_order_release);
  }

  [[nodiscard]] Value Capture() const noexcept {
    std::array<std::uint32_t, WordCount> copy{};
    for (;;) {
      const std::uint32_t before = sequence_.load(std::memory_order_acquire);
      if ((before & 1U) != 0U)
        continue;
      for (std::size_t index = 0U; index < WordCount; ++index)
        copy[index] = words_[index].load(std::memory_order_acquire);
      const std::uint32_t after = sequence_.load(std::memory_order_acquire);
      if (before == after && (after & 1U) == 0U)
        break;
    }

    Value value{};
    std::memcpy(&value, copy.data(), sizeof(Value));
    return value;
  }

private:
  std::array<std::atomic<std::uint32_t>, WordCount> words_{};
  std::atomic<std::uint32_t> sequence_{0U};
};
