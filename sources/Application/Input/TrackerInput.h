/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

// Stable hardware/application action order. Platform adapters emit these
// actions directly; neither UI2 controllers nor firmware input need a legacy
// GUIEvent/View header to interpret them.
enum class TrackerAction : std::uint8_t {
  Left = 0,
  Down,
  Right,
  Up,
  Alt,
  Edit,
  Enter,
  Nav,
  Play,
  Select,
  Power,
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
  EPBM_ALT = TrackerActionBit(TrackerAction::Alt),
  EPBM_EDIT = TrackerActionBit(TrackerAction::Edit),
  EPBM_ENTER = TrackerActionBit(TrackerAction::Enter),
  EPBM_NAV = TrackerActionBit(TrackerAction::Nav),
  EPBM_PLAY = TrackerActionBit(TrackerAction::Play),
  EPBM_SELECT = TrackerActionBit(TrackerAction::Select),
  EPBM_POWER = TrackerActionBit(TrackerAction::Power),
};

static_assert(EPBM_LEFT == 1U);
static_assert(EPBM_POWER == 1024U);
