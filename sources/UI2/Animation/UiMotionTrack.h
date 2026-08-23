/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <cstdint>

namespace ui2 {

using UnitQ16 = std::uint16_t;

[[nodiscard]] constexpr UnitQ16 EaseOutCubic(UnitQ16 time) {
  const std::uint64_t remaining = 65'535U - time;
  const std::uint64_t squared = (remaining * remaining) >> 16U;
  const std::uint64_t cubed = (squared * remaining) >> 16U;
  return static_cast<UnitQ16>(65'535U - cubed);
}

class UiMotionTrack {
public:
  void Start(std::int32_t from, std::int32_t to, std::uint32_t nowMs,
             std::uint16_t durationMs) {
    from_ = from;
    to_ = to;
    startMs_ = nowMs;
    durationMs_ = std::max<std::uint16_t>(durationMs, 1);
    active_ = from != to;
  }

  [[nodiscard]] std::int32_t Sample(std::uint32_t nowMs) const {
    if (!active_ || nowMs <= startMs_) return from_;
    const std::uint32_t elapsed = nowMs - startMs_;
    if (elapsed >= durationMs_) return to_;
    const UnitQ16 time = static_cast<UnitQ16>(
        (static_cast<std::uint64_t>(elapsed) * 65'535U) / durationMs_);
    const std::int64_t delta = static_cast<std::int64_t>(to_) - from_;
    return static_cast<std::int32_t>(
        from_ + ((delta * EaseOutCubic(time)) >> 16U));
  }

  [[nodiscard]] bool Active(std::uint32_t nowMs) const {
    if (!active_ || nowMs < startMs_) return false;
    return nowMs - startMs_ < durationMs_;
  }

private:
  std::int32_t from_ = 0;
  std::int32_t to_ = 0;
  std::uint32_t startMs_ = 0;
  std::uint16_t durationMs_ = 1;
  bool active_ = false;
};

} // namespace ui2
