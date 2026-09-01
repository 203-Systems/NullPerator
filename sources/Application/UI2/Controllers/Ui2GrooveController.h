/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/UI2/Controllers/Ui2TrackerGridController.h"

#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2GrooveCommandType : std::uint8_t {
  None,
  InitializeStep,
  ClearStep,
  AdjustStep,
  CopySelection,
  CutSelection,
  PasteSelection,
  InterpolateSelection,
  SelectNumber,
  StartPlayback,
  ToggleMute,
  ToggleSolo,
  UnmuteAll,
};

enum class Ui2GrooveDirection : std::uint8_t {
  None,
  Left,
  Down,
  Right,
  Up,
};

struct Ui2GrooveCommand {
  Ui2GrooveCommandType type = Ui2GrooveCommandType::None;
  Ui2GrooveDirection direction = Ui2GrooveDirection::None;
  std::int16_t value = 0;
  std::uint8_t row = 0;
  bool synchronized = false;
  bool songTransport = false;
  Ui2GridSelectionState selection{};

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2GrooveCommandType::None;
  }
};

struct Ui2GrooveStepPolicy {
  static constexpr std::uint8_t Empty = 0xFFU;
  static constexpr std::uint8_t Initial = 6U;

  [[nodiscard]] static constexpr std::uint8_t
  Initialize(std::uint8_t current) {
    return current == Empty ? Initial : current;
  }

  [[nodiscard]] static constexpr std::uint8_t
  Adjust(std::uint8_t current, std::int16_t delta) {
    const int value = current == Empty ? 0 : current;
    const int adjusted = value + delta;
    if (adjusted < 1)
      return 1U;
    if (adjusted > 15)
      return 15U;
    return static_cast<std::uint8_t>(adjusted);
  }
};

class Ui2GrooveController {
public:
  static constexpr std::uint8_t RowCount = 16U;
  static constexpr std::uint8_t DefaultGrooveCount = 32U;

  constexpr Ui2GrooveController(std::uint8_t number = 0,
                                std::uint8_t row = 0,
                                std::uint8_t grooveCount = DefaultGrooveCount,
                                bool grooveWrap = true)
      : number_(grooveCount == 0U
                    ? 0U
                    : number < grooveCount ? number
                                           : static_cast<std::uint8_t>(
                                                 grooveCount - 1U)),
        row_(row < RowCount ? row : RowCount - 1U),
        grooveCount_(grooveCount), grooveWrap_(grooveWrap) {}

  [[nodiscard]] constexpr std::uint8_t Number() const { return number_; }
  [[nodiscard]] constexpr std::uint8_t Row() const { return row_; }
  [[nodiscard]] constexpr bool BottomVisible() const {
    return selection_.active;
  }
  [[nodiscard]] constexpr const Ui2GridSelectionState &Selection() const {
    return selection_;
  }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }
  constexpr void SetNavigationHeld(bool held) {
    input_.SetNavigationHeld(held);
  }

  constexpr Ui2GrooveCommand Handle(TrackerAction action, bool pressed) {
    const bool wasHeld = input_.Held(action);
    if (!input_.Update(action, pressed))
      return {};
    if (!pressed) {
      if (action == TrackerAction::Option && wasHeld && copyPending_ &&
          selection_.active && !input_.Held(TrackerAction::Shift)) {
        Ui2GrooveCommand command =
            MakeCommand(Ui2GrooveCommandType::CopySelection);
        command.selection = selection_;
        selection_.Clear();
        copyPending_ = false;
        return command;
      }
      return {};
    }

    const Ui2GrooveDirection direction = DirectionFor(action);
    if (action == TrackerAction::Play &&
        input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Enter)) {
      copyPending_ = false;
      return MakeCommand(input_.Held(TrackerAction::Shift)
                             ? Ui2GrooveCommandType::UnmuteAll
                             : Ui2GrooveCommandType::ToggleSolo);
    }
    if (action == TrackerAction::Play &&
        input_.Held(TrackerAction::Shift) &&
        !input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Enter)) {
      Ui2GrooveCommand command =
          MakeCommand(Ui2GrooveCommandType::StartPlayback);
      command.songTransport = true;
      return command;
    }
    if (action == TrackerAction::Shift &&
        input_.Held(TrackerAction::Option)) {
      copyPending_ = false;
      return MakeCommand(Ui2GrooveCommandType::ToggleMute);
    }
    if (selection_.active)
      return HandleSelection(action, direction);
    if (action == TrackerAction::Option &&
        input_.Held(TrackerAction::Shift)) {
      selection_.Begin(0U, row_);
      return {};
    }
    if (action == TrackerAction::Option &&
        input_.Held(TrackerAction::Enter))
      return MakeCommand(Ui2GrooveCommandType::ClearStep);
    if (input_.Held(TrackerAction::Option)) {
      if (direction != Ui2GrooveDirection::None)
        return SelectNumber(direction);
      return {};
    }

    if (action == TrackerAction::Enter &&
        input_.Held(TrackerAction::Shift))
      return MakeCommand(Ui2GrooveCommandType::PasteSelection);

    if (input_.Held(TrackerAction::Enter)) {
      if (direction != Ui2GrooveDirection::None) {
        Ui2GrooveCommand command =
            MakeCommand(Ui2GrooveCommandType::AdjustStep);
        command.direction = direction;
        command.value = direction == Ui2GrooveDirection::Left ||
                                direction == Ui2GrooveDirection::Down
                            ? -1
                            : 1;
        command.synchronized = direction == Ui2GrooveDirection::Down ||
                               direction == Ui2GrooveDirection::Up;
        return command;
      }
      if (action == TrackerAction::Enter &&
          input_.Mask() == TrackerActionBit(TrackerAction::Enter))
        return MakeCommand(Ui2GrooveCommandType::InitializeStep);
      return {};
    }

    if (input_.AnyModifier())
      return {};

    if (action == TrackerAction::Up) {
      row_ = row_ == 0U ? RowCount - 1U
                        : static_cast<std::uint8_t>(row_ - 1U);
    } else if (action == TrackerAction::Down) {
      row_ = static_cast<std::uint8_t>((row_ + 1U) % RowCount);
    } else if (action == TrackerAction::Play) {
      return MakeCommand(Ui2GrooveCommandType::StartPlayback);
    }
    return {};
  }

private:
  [[nodiscard]] static constexpr Ui2GrooveDirection
  DirectionFor(TrackerAction action) {
    switch (action) {
    case TrackerAction::Left:
      return Ui2GrooveDirection::Left;
    case TrackerAction::Down:
      return Ui2GrooveDirection::Down;
    case TrackerAction::Right:
      return Ui2GrooveDirection::Right;
    case TrackerAction::Up:
      return Ui2GrooveDirection::Up;
    default:
      return Ui2GrooveDirection::None;
    }
  }

  [[nodiscard]] constexpr Ui2GrooveCommand
  MakeCommand(Ui2GrooveCommandType type) const {
    return {.type = type, .row = row_};
  }

  constexpr Ui2GrooveCommand
  HandleSelection(TrackerAction action, Ui2GrooveDirection direction) {
    copyPending_ = false;
    if (action == TrackerAction::Play) {
      if (input_.Held(TrackerAction::Enter))
        return {};
      Ui2GrooveCommand command =
          MakeCommand(Ui2GrooveCommandType::StartPlayback);
      command.songTransport = input_.Held(TrackerAction::Shift);
      return command;
    }
    if (action == TrackerAction::Option &&
        input_.Held(TrackerAction::Shift)) {
      selection_.ExpandColumnsThenRows(0U, 0U, RowCount - 1U);
      return {};
    }
    if (action == TrackerAction::Option &&
        input_.Held(TrackerAction::Enter)) {
      Ui2GrooveCommand command =
          MakeCommand(Ui2GrooveCommandType::CutSelection);
      command.selection = selection_;
      selection_.Clear();
      return command;
    }
    if (action == TrackerAction::Option &&
        input_.Mask() == TrackerActionBit(TrackerAction::Option)) {
      copyPending_ = true;
      return {};
    }
    if (action == TrackerAction::Enter &&
        input_.Held(TrackerAction::Shift)) {
      Ui2GrooveCommand command =
          MakeCommand(Ui2GrooveCommandType::InterpolateSelection);
      command.selection = selection_;
      return command;
    }
    if (input_.AnyModifier())
      return {};

    const std::uint8_t previous = row_;
    if (direction == Ui2GrooveDirection::Up && row_ > 0U) {
      --row_;
    } else if (direction == Ui2GrooveDirection::Down &&
               row_ + 1U < RowCount) {
      ++row_;
    }
    if (row_ != previous)
      selection_.Follow(0U, row_);
    return {};
  }

  constexpr Ui2GrooveCommand SelectNumber(Ui2GrooveDirection direction) {
    if (grooveCount_ == 0U)
      return {};
    std::int16_t delta = 0;
    switch (direction) {
    case Ui2GrooveDirection::Left:
      delta = -1;
      break;
    case Ui2GrooveDirection::Right:
      delta = 1;
      break;
    case Ui2GrooveDirection::Down:
      delta = -16;
      break;
    case Ui2GrooveDirection::Up:
      delta = 16;
      break;
    case Ui2GrooveDirection::None:
      return {};
    }

    const std::uint8_t previous = number_;
    std::int16_t next = static_cast<std::int16_t>(number_) + delta;
    if (grooveWrap_) {
      while (next < 0)
        next += grooveCount_;
      while (next >= grooveCount_)
        next -= grooveCount_;
    } else {
      if (next < 0)
        next = 0;
      if (next >= grooveCount_)
        next = static_cast<std::int16_t>(grooveCount_ - 1U);
    }
    number_ = static_cast<std::uint8_t>(next);
    if (number_ == previous)
      return {};
    Ui2GrooveCommand command = MakeCommand(Ui2GrooveCommandType::SelectNumber);
    command.direction = direction;
    command.value = number_;
    return command;
  }

  Ui2ControllerInputState input_{};
  Ui2GridSelectionState selection_{};
  std::uint8_t number_ = 0;
  std::uint8_t row_ = 0;
  std::uint8_t grooveCount_ = DefaultGrooveCount;
  bool grooveWrap_ = true;
  bool copyPending_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2GrooveCommand>);
static_assert(std::is_trivially_copyable_v<Ui2GrooveController>);
static_assert(sizeof(Ui2GrooveController) <= 16U);

} // namespace ui2
