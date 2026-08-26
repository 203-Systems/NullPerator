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

enum class Ui2FontField : std::uint8_t { TextCase, Browse };
enum class Ui2FontCommandType : std::uint8_t { None, SetTextCase, BrowseFont };

struct Ui2FontCommand {
  Ui2FontCommandType type = Ui2FontCommandType::None;
  std::uint8_t value = 0;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2FontCommandType::None;
  }
};

class Ui2FontController {
public:
  [[nodiscard]] constexpr Ui2FontField SelectedField() const { return field_; }
  [[nodiscard]] constexpr std::uint8_t TextCase() const { return textCase_; }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }

  constexpr void SetTextCase(std::uint8_t value) {
    textCase_ = static_cast<std::uint8_t>(value % 3U);
  }

  constexpr Ui2FontCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};
    if (field_ == Ui2FontField::Browse && action == TrackerAction::Edit &&
        input_.Mask() == TrackerActionBit(TrackerAction::Edit))
      return {.type = Ui2FontCommandType::BrowseFont};
    if (input_.AnyModifier())
      return {};
    if (action == TrackerAction::Up || action == TrackerAction::Down) {
      field_ = field_ == Ui2FontField::TextCase ? Ui2FontField::Browse
                                                : Ui2FontField::TextCase;
      return {};
    }
    if (field_ == Ui2FontField::TextCase &&
        (action == TrackerAction::Left || action == TrackerAction::Right)) {
      const int delta = action == TrackerAction::Left ? -1 : 1;
      textCase_ = static_cast<std::uint8_t>((textCase_ + delta + 3) % 3);
      return {.type = Ui2FontCommandType::SetTextCase, .value = textCase_};
    }
    return {};
  }

private:
  Ui2ControllerInputState input_{};
  Ui2FontField field_ = Ui2FontField::TextCase;
  std::uint8_t textCase_ = 1;
};

static_assert(std::is_trivially_copyable_v<Ui2FontCommand>);
static_assert(std::is_trivially_copyable_v<Ui2FontController>);
static_assert(sizeof(Ui2FontController) <= 8U);

} // namespace ui2
