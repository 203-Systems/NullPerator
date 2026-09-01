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
  Power = 10,
  Count,
};

// Action ids are part of the native/Web input protocol. Keep the reserved
// holes invalid without duplicating their numeric details in every adapter and
// controller boundary.
[[nodiscard]] constexpr bool TrackerActionIdIsValid(std::uint16_t action) {
  return action <= static_cast<std::uint16_t>(TrackerAction::Play) ||
         action == static_cast<std::uint16_t>(TrackerAction::Power);
}

[[nodiscard]] constexpr bool TrackerActionIsValid(TrackerAction action) {
  return TrackerActionIdIsValid(static_cast<std::uint16_t>(action));
}

static_assert(static_cast<std::uint8_t>(TrackerAction::Play) == 7U);
static_assert(static_cast<std::uint8_t>(TrackerAction::Power) == 10U);
static_assert(!TrackerActionIdIsValid(8U));
static_assert(!TrackerActionIdIsValid(9U));

[[nodiscard]] constexpr std::uint16_t
TrackerActionBit(TrackerAction action) {
  return static_cast<std::uint16_t>(
      1U << static_cast<std::uint8_t>(action));
}
