/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <cstdint>

namespace node::ui2 {

// Stable names for the raw Nullperator controls. Keeping this translation
// independent of the HAL makes the physical-to-semantic contract directly
// host-testable without pulling ESP-IDF into the test binary.
enum class PhysicalButton : std::uint8_t {
  Left = 0,
  Down,
  Right,
  Up,
  Start,
  Select,
  B,
  A,
  Func,
};

// TrackerAction::Count is the invalid-action sentinel for unknown raw values.
[[nodiscard]] constexpr TrackerAction
ActionForPhysicalButton(PhysicalButton button) {
  switch (button) {
  case PhysicalButton::Left:
    return TrackerAction::Left;
  case PhysicalButton::Down:
    return TrackerAction::Down;
  case PhysicalButton::Right:
    return TrackerAction::Right;
  case PhysicalButton::Up:
    return TrackerAction::Up;
  case PhysicalButton::Start:
    return TrackerAction::Shift;
  case PhysicalButton::Select:
    return TrackerAction::Play;
  case PhysicalButton::B:
    return TrackerAction::Option;
  case PhysicalButton::A:
    return TrackerAction::Enter;
  case PhysicalButton::Func:
    return TrackerAction::Power;
  }
  return TrackerAction::Count;
}

} // namespace node::ui2
