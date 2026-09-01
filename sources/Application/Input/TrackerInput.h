/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

// Stable product action order shared by native and Web input adapters.
enum class TrackerAction : std::uint8_t {
  Left = 0,
  Down,
  Right,
  Up,
  Shift,  // Page/navigation modifier.
  Option, // Context/fast-action modifier.
  Enter,  // Confirm/data-entry action.
  Play,   // Immediate context playback action.
  Reserved8,
  Reserved9,
  Power = 10,
  Count,
};

[[nodiscard]] constexpr std::uint16_t
TrackerActionBit(TrackerAction action) {
  return static_cast<std::uint16_t>(
      1U << static_cast<std::uint8_t>(action));
}
