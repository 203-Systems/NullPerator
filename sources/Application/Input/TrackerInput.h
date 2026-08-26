/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

// Stable M8-style UI2 action order. Platform adapters translate their raw
// button labels (A/B/SELECT/START) into these semantic actions exactly once.
enum class TrackerAction : std::uint8_t {
  Left = 0,
  Down,
  Right,
  Up,
  Shift,  // Node SELECT; M8 page/navigation modifier.
  Option, // Node B; M8 context/fast-action modifier.
  Edit,   // Node A; M8 edit/confirm action.
  Play,   // Node START; immediate context playback action.
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

enum TrackerButtonMask : std::uint16_t {
  EPBM_LEFT = TrackerActionBit(TrackerAction::Left),
  EPBM_DOWN = TrackerActionBit(TrackerAction::Down),
  EPBM_RIGHT = TrackerActionBit(TrackerAction::Right),
  EPBM_UP = TrackerActionBit(TrackerAction::Up),
  EPBM_SHIFT = TrackerActionBit(TrackerAction::Shift),
  EPBM_OPTION = TrackerActionBit(TrackerAction::Option),
  EPBM_M8_EDIT = TrackerActionBit(TrackerAction::Edit),
  EPBM_M8_PLAY = TrackerActionBit(TrackerAction::Play),

  // Legacy masks remain available only while the old reference UI is still
  // part of non-UI2 builds. Native UI2 code must use the semantic masks above.
  EPBM_ALT = EPBM_SHIFT,
  EPBM_EDIT = EPBM_OPTION,
  EPBM_ENTER = EPBM_M8_EDIT,
  EPBM_START = EPBM_M8_PLAY,
  EPBM_NAV = EPBM_SHIFT,
  EPBM_PLAY = TrackerActionBit(TrackerAction::Play),
  EPBM_SELECT = TrackerActionBit(TrackerAction::Reserved9),
  EPBM_POWER = TrackerActionBit(TrackerAction::Power),
};

static_assert(EPBM_LEFT == 1U);
static_assert(EPBM_POWER == 1024U);
