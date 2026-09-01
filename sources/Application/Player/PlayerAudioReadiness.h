/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <atomic>
#include <cstdint>

// Player entry points may be called by UI and MIDI producers after project
// recovery has succeeded but platform audio initialization has failed. Keep
// that lifecycle edge independent from project/model readiness and publish it
// without a data race to those producers.
class PlayerAudioReadiness final {
public:
  PlayerAudioReadiness() noexcept = default;

  PlayerAudioReadiness(const PlayerAudioReadiness &) = delete;
  PlayerAudioReadiness &operator=(const PlayerAudioReadiness &) = delete;

  void BeginInitialization() noexcept {
    ready_.store(0U, std::memory_order_release);
  }

  [[nodiscard]] bool CompleteInitialization(bool started) noexcept {
    ready_.store(started ? 1U : 0U, std::memory_order_release);
    return started;
  }

  void Close() noexcept { ready_.store(0U, std::memory_order_release); }

  [[nodiscard]] bool IsReady() const noexcept {
    return ready_.load(std::memory_order_acquire) != 0U;
  }

  // Starting transport also reads the project-backed editor state. Keep the
  // complete fail-closed predicate shared by every public start entry point so
  // a failed initialization cannot reach that state after its bindings have
  // been rolled back.
  [[nodiscard]] bool CanStartTransport(bool projectBound,
                                       bool viewDataBound) const noexcept {
    return IsReady() && projectBound && viewDataBound;
  }

private:
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "audio readiness must remain lock-free");
  std::atomic<std::uint32_t> ready_{0U};
};
