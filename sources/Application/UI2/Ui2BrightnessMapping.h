/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <cstdint>

namespace ui2 {

// Zero remains a meaningful UI percentage, but the hardware value never
// falls below the legacy-visible floor. This prevents a user from making a
// non-backlit device impossible to recover through the on-device UI.
inline constexpr std::uint8_t Ui2MinimumVisibleBrightness = 0x0FU;

[[nodiscard]] constexpr std::uint8_t
Ui2BrightnessRawFromPercent(std::uint16_t percent) {
  const std::uint32_t bounded = std::min<std::uint16_t>(percent, 100U);
  constexpr std::uint32_t span =
      0xFFU - static_cast<std::uint32_t>(Ui2MinimumVisibleBrightness);
  return static_cast<std::uint8_t>(
      Ui2MinimumVisibleBrightness + (bounded * span + 50U) / 100U);
}

[[nodiscard]] constexpr std::uint16_t
Ui2BrightnessPercentFromRaw(int raw) {
  const std::uint32_t bounded = static_cast<std::uint32_t>(
      std::clamp(raw, static_cast<int>(Ui2MinimumVisibleBrightness), 0xFF));
  constexpr std::uint32_t span =
      0xFFU - static_cast<std::uint32_t>(Ui2MinimumVisibleBrightness);
  return static_cast<std::uint16_t>(
      ((bounded - Ui2MinimumVisibleBrightness) * 100U + span / 2U) / span);
}

} // namespace ui2
