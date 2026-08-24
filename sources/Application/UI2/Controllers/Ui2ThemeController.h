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

enum class Ui2ThemeNameAction : std::uint8_t {
  New = 0,
  Load,
  Save,
  Rename,
  Count,
};

enum class Ui2ThemeCommandType : std::uint8_t {
  None,
  NewTheme,
  LoadTheme,
  SaveTheme,
  RenameTheme,
  ActivateColor,
};

enum class Ui2ThemeBottomKind : std::uint8_t { Hidden, NameActions };

struct Ui2ThemeCommand {
  Ui2ThemeCommandType type = Ui2ThemeCommandType::None;
  // Valid only for ActivateColor. It is an index into the nineteen public
  // palette slots, not a renderer token or a legacy color definition.
  std::int8_t color = -1;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2ThemeCommandType::None;
  }
};

struct Ui2ThemeBottomState {
  Ui2ThemeBottomKind kind = Ui2ThemeBottomKind::Hidden;
  Ui2ThemeCommandType selectedCommand = Ui2ThemeCommandType::None;
  std::uint8_t selectedIndex = 0;
  std::uint8_t optionCount = 0;
};

class Ui2ThemeController {
public:
  static constexpr std::uint8_t ColorCount = 19U;
  static constexpr std::uint8_t RowCount = ColorCount + 1U;

  constexpr Ui2ThemeController(std::int8_t selectedColor = -1,
                               Ui2ThemeNameAction nameAction =
                                   Ui2ThemeNameAction::New,
                               std::uint8_t viewportRows = 12)
      : cursor_(RowForColor(selectedColor),
                Ui2FixedListCursor<RowCount>::AllEnabledMask, viewportRows),
        nameAction_(Sanitize(nameAction)) {}

  [[nodiscard]] constexpr bool NameSelected() const {
    return cursor_.Selected() == 0U;
  }
  [[nodiscard]] constexpr std::int8_t SelectedColor() const {
    return NameSelected()
               ? -1
               : static_cast<std::int8_t>(cursor_.Selected() - 1U);
  }
  [[nodiscard]] constexpr Ui2ThemeNameAction NameAction() const {
    return nameAction_;
  }
  [[nodiscard]] constexpr std::uint8_t FirstVisibleOrdinal() const {
    return cursor_.FirstVisibleOrdinal();
  }
  [[nodiscard]] constexpr std::uint8_t SelectedOrdinal() const {
    return cursor_.SelectedOrdinal();
  }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }

  [[nodiscard]] constexpr Ui2ThemeBottomState Bottom() const {
    if (!NameSelected())
      return {};
    return {.kind = Ui2ThemeBottomKind::NameActions,
            .selectedCommand = NameCommand(nameAction_),
            .selectedIndex = static_cast<std::uint8_t>(nameAction_),
            .optionCount =
                static_cast<std::uint8_t>(Ui2ThemeNameAction::Count)};
  }

  [[nodiscard]] constexpr Ui2ThemeCommand Enter() const {
    if (NameSelected())
      return {.type = NameCommand(nameAction_)};
    return {.type = Ui2ThemeCommandType::ActivateColor,
            .color = SelectedColor()};
  }

  constexpr Ui2ThemeCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};

    if (input_.Held(TrackerAction::Enter)) {
      if (action == TrackerAction::Enter &&
          input_.Mask() == TrackerActionBit(TrackerAction::Enter))
        return Enter();
      return {};
    }
    if (input_.AnyModifier())
      return {};

    if (action == TrackerAction::Up) {
      cursor_.MovePrevious();
    } else if (action == TrackerAction::Down) {
      cursor_.MoveNext();
    } else if (NameSelected() && action == TrackerAction::Left) {
      nameAction_ = static_cast<Ui2ThemeNameAction>(Previous(
          static_cast<std::uint8_t>(nameAction_), NameActionCount()));
    } else if (NameSelected() && action == TrackerAction::Right) {
      nameAction_ = static_cast<Ui2ThemeNameAction>(
          Next(static_cast<std::uint8_t>(nameAction_), NameActionCount()));
    }
    return {};
  }

private:
  [[nodiscard]] static constexpr std::uint8_t
  RowForColor(std::int8_t color) {
    return color >= 0 && color < static_cast<std::int8_t>(ColorCount)
               ? static_cast<std::uint8_t>(color + 1)
               : 0U;
  }

  [[nodiscard]] static constexpr std::uint8_t NameActionCount() {
    return static_cast<std::uint8_t>(Ui2ThemeNameAction::Count);
  }

  [[nodiscard]] static constexpr std::uint8_t Next(std::uint8_t value,
                                                    std::uint8_t count) {
    return static_cast<std::uint8_t>((value + 1U) % count);
  }

  [[nodiscard]] static constexpr std::uint8_t Previous(std::uint8_t value,
                                                        std::uint8_t count) {
    return value == 0U ? static_cast<std::uint8_t>(count - 1U)
                       : static_cast<std::uint8_t>(value - 1U);
  }

  [[nodiscard]] static constexpr Ui2ThemeNameAction
  Sanitize(Ui2ThemeNameAction action) {
    return static_cast<std::uint8_t>(action) < NameActionCount()
               ? action
               : Ui2ThemeNameAction::New;
  }

  [[nodiscard]] static constexpr Ui2ThemeCommandType
  NameCommand(Ui2ThemeNameAction action) {
    switch (action) {
    case Ui2ThemeNameAction::New:
      return Ui2ThemeCommandType::NewTheme;
    case Ui2ThemeNameAction::Load:
      return Ui2ThemeCommandType::LoadTheme;
    case Ui2ThemeNameAction::Save:
      return Ui2ThemeCommandType::SaveTheme;
    case Ui2ThemeNameAction::Rename:
      return Ui2ThemeCommandType::RenameTheme;
    case Ui2ThemeNameAction::Count:
      return Ui2ThemeCommandType::None;
    }
    return Ui2ThemeCommandType::None;
  }

  Ui2FixedListCursor<RowCount> cursor_{};
  Ui2ControllerInputState input_{};
  Ui2ThemeNameAction nameAction_ = Ui2ThemeNameAction::New;
};

static_assert(std::is_trivially_copyable_v<Ui2ThemeCommand>);
static_assert(std::is_trivially_copyable_v<Ui2ThemeBottomState>);
static_assert(std::is_trivially_copyable_v<Ui2ThemeController>);
static_assert(sizeof(Ui2ThemeController) <= 16U);

} // namespace ui2
