/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

struct OpalOutputLevelRegisters {
  std::uint8_t operator1 = 0U;
  std::uint8_t operator2 = 0U;
};

[[nodiscard]] constexpr OpalOutputLevelRegisters EncodeOpalOutputLevels(
    int operator1KeyScale, int operator1Level, int operator2KeyScale,
    int operator2Level) {
  return {
      .operator1 = static_cast<std::uint8_t>(
          ((operator1KeyScale & 0x03) << 6) | (operator1Level & 0x3F)),
      .operator2 = static_cast<std::uint8_t>(
          ((operator2KeyScale & 0x03) << 6) | (operator2Level & 0x3F)),
  };
}
