/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"

#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2FontCommandType : std::uint8_t { None, BrowseFont };

struct Ui2FontCommand {
  Ui2FontCommandType type = Ui2FontCommandType::None;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2FontCommandType::None;
  }
};

// The approved Font page has one focusable content action, BROWSE. It does not
// expose legacy font spacing or line-step fields and does not need a bottom
// selector state.
class Ui2FontController {
public:
  [[nodiscard]] constexpr bool BrowseSelected() const { return true; }
  [[nodiscard]] constexpr bool BottomVisible() const { return false; }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }

  constexpr Ui2FontCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};
    if (action == TrackerAction::Enter &&
        input_.Mask() == TrackerActionBit(TrackerAction::Enter))
      return {.type = Ui2FontCommandType::BrowseFont};
    return {};
  }

private:
  Ui2ControllerInputState input_{};
};

static_assert(std::is_trivially_copyable_v<Ui2FontCommand>);
static_assert(std::is_trivially_copyable_v<Ui2FontController>);
static_assert(sizeof(Ui2FontController) <= 4U);

} // namespace ui2
