/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

namespace ui2 {

inline constexpr std::uint8_t Ui2VuMeterHeight = 153U;

// Integer-only approximation of the legacy -60 dB..0 dB meter mapping.
[[nodiscard]] constexpr std::uint8_t
Ui2VuTopFromAmplitude(std::uint16_t amplitude) {
  if (amplitude < 33U)
    return Ui2VuMeterHeight;
  if (amplitude >= 32700U)
    return 0U;
  std::uint8_t exponent = 0;
  std::uint16_t value = amplitude;
  while (value > 1U) {
    value >>= 1U;
    ++exponent;
  }
  const std::uint32_t base = 1U << exponent;
  const std::uint32_t fraction =
      ((static_cast<std::uint32_t>(amplitude) - base) * 16U) / base;
  const std::uint32_t steps =
      (static_cast<std::uint32_t>(exponent - 5U) * 16U) + fraction;
  const std::uint32_t active =
      (steps * static_cast<std::uint32_t>(Ui2VuMeterHeight)) / 160U;
  return static_cast<std::uint8_t>(Ui2VuMeterHeight - active);
}

} // namespace ui2
