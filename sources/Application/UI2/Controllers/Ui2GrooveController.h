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

enum class Ui2GrooveCommandType : std::uint8_t {
  None,
  InitializeStep,
  ClearStep,
  AdjustStep,
  SelectNumber,
  StartPlayback,
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
  [[nodiscard]] constexpr bool BottomVisible() const { return false; }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }

  constexpr Ui2GrooveCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};

    const Ui2GrooveDirection direction = DirectionFor(action);
    if (action == TrackerAction::Option &&
        input_.Held(TrackerAction::Edit))
      return MakeCommand(Ui2GrooveCommandType::ClearStep);
    if (action == TrackerAction::Play &&
        input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Edit)) {
      return MakeCommand(input_.Held(TrackerAction::Shift)
                             ? Ui2GrooveCommandType::UnmuteAll
                             : Ui2GrooveCommandType::ToggleSolo);
    }
    if (action == TrackerAction::Play &&
        input_.Held(TrackerAction::Shift) &&
        !input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Edit)) {
      Ui2GrooveCommand command =
          MakeCommand(Ui2GrooveCommandType::StartPlayback);
      command.songTransport = true;
      return command;
    }
    if (input_.Held(TrackerAction::Option)) {
      if (direction != Ui2GrooveDirection::None)
        return SelectNumber(direction);
      return {};
    }

    if (input_.Held(TrackerAction::Edit)) {
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
      if (action == TrackerAction::Edit &&
          input_.Mask() == TrackerActionBit(TrackerAction::Edit))
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
    case TrackerAction::Option:
    case TrackerAction::Edit:
    case TrackerAction::Shift:
    case TrackerAction::Play:
    case TrackerAction::Reserved8:
    case TrackerAction::Reserved9:
    case TrackerAction::Power:
    case TrackerAction::Count:
      return Ui2GrooveDirection::None;
    }
    return Ui2GrooveDirection::None;
  }

  [[nodiscard]] constexpr Ui2GrooveCommand
  MakeCommand(Ui2GrooveCommandType type) const {
    return {.type = type, .row = row_};
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
  std::uint8_t number_ = 0;
  std::uint8_t row_ = 0;
  std::uint8_t grooveCount_ = DefaultGrooveCount;
  bool grooveWrap_ = true;
};

static_assert(std::is_trivially_copyable_v<Ui2GrooveCommand>);
static_assert(std::is_trivially_copyable_v<Ui2GrooveController>);
static_assert(sizeof(Ui2GrooveController) <= 8U);

} // namespace ui2
