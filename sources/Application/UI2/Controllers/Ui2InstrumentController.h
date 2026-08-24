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

enum class Ui2InstrumentCursorKind : std::uint8_t {
  Name,
  Type,
  Field,
  Operator1,
  Operator2,
};

struct Ui2InstrumentCursorPosition {
  Ui2InstrumentCursorKind kind = Ui2InstrumentCursorKind::Name;
  std::uint8_t index = 0;
};

enum class Ui2InstrumentNameAction : std::uint8_t {
  Load = 0,
  Save,
  Rename,
  Count,
};

enum class Ui2InstrumentValueDirection : std::uint8_t {
  None,
  Left,
  Down,
  Right,
  Up,
};

enum class Ui2InstrumentCommandType : std::uint8_t {
  None,
  LoadInstrument,
  SaveInstrument,
  RenameInstrument,
  SetType,
  SelectNumber,
  SelectTrack,
  AdjustField,
  ActivateField,
  CommitValueEdits,
  StartPlayback,
  OpenRecord,
};

enum class Ui2InstrumentBottomKind : std::uint8_t {
  Hidden,
  NameActions,
  TypeSelector,
  TrackNotes,
};

struct Ui2InstrumentCommand {
  Ui2InstrumentCommandType type = Ui2InstrumentCommandType::None;
  Ui2InstrumentCursorPosition cursor{};
  Ui2InstrumentValueDirection direction = Ui2InstrumentValueDirection::None;
  std::int16_t value = 0;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2InstrumentCommandType::None;
  }
};

struct Ui2InstrumentBottomState {
  Ui2InstrumentBottomKind kind = Ui2InstrumentBottomKind::Hidden;
  std::uint8_t selectedIndex = 0;
  std::uint8_t optionCount = 0;
  bool wrap = false;
};

class Ui2InstrumentController {
public:
  static constexpr std::uint8_t MaximumFields = 16U;
  static constexpr std::uint8_t MaximumOperatorRows = 6U;
  static constexpr std::uint8_t MaximumRows =
      2U + MaximumFields + MaximumOperatorRows;
  static constexpr std::uint8_t TrackCount = 8U;
  static constexpr std::uint8_t DefaultInstrumentCount = 39U;

  constexpr Ui2InstrumentController(
      std::uint8_t number = 0, std::uint8_t selectedTrack = 0,
      std::uint8_t fieldCount = 0, std::uint8_t operatorCount = 0,
      Ui2InstrumentCursorPosition cursor = {},
      Ui2SelectorState typeSelector = {5U, 0U, true},
      std::uint8_t viewportRows = 10,
      std::uint8_t instrumentCount = DefaultInstrumentCount,
      bool instrumentWrap = true)
      : cursor_(RowFor(cursor, ClampFieldCount(fieldCount),
                       ClampOperatorCount(operatorCount)),
                EnabledRowsMask(ClampFieldCount(fieldCount),
                                ClampOperatorCount(operatorCount)),
                viewportRows),
        typeSelector_(typeSelector), fieldCount_(ClampFieldCount(fieldCount)),
        operatorCount_(ClampOperatorCount(operatorCount)),
        number_(SanitizeNumber(number, instrumentCount)),
        instrumentCount_(instrumentCount),
        selectedTrack_(selectedTrack < TrackCount ? selectedTrack
                                                  : TrackCount - 1U),
        operatorColumn_(cursor.kind == Ui2InstrumentCursorKind::Operator2
                            ? 1U
                            : 0U),
        instrumentWrap_(instrumentWrap) {}

  [[nodiscard]] constexpr Ui2InstrumentCursorPosition Cursor() const {
    const std::uint8_t row = cursor_.Selected();
    if (row == 0U)
      return {.kind = Ui2InstrumentCursorKind::Name};
    if (row == 1U)
      return {.kind = Ui2InstrumentCursorKind::Type};
    if (row < static_cast<std::uint8_t>(2U + fieldCount_)) {
      return {.kind = Ui2InstrumentCursorKind::Field,
              .index = static_cast<std::uint8_t>(row - 2U)};
    }
    return {.kind = operatorColumn_ == 0U
                        ? Ui2InstrumentCursorKind::Operator1
                        : Ui2InstrumentCursorKind::Operator2,
            .index = static_cast<std::uint8_t>(row - 2U - fieldCount_)};
  }

  [[nodiscard]] constexpr std::uint8_t Number() const { return number_; }
  [[nodiscard]] constexpr std::uint8_t SelectedTrack() const {
    return selectedTrack_;
  }
  [[nodiscard]] constexpr std::uint8_t FieldCount() const {
    return fieldCount_;
  }
  [[nodiscard]] constexpr std::uint8_t OperatorCount() const {
    return operatorCount_;
  }
  [[nodiscard]] constexpr Ui2SelectorState TypeSelector() const {
    return typeSelector_;
  }
  [[nodiscard]] constexpr Ui2InstrumentNameAction NameAction() const {
    return nameAction_;
  }
  [[nodiscard]] constexpr std::uint8_t FirstVisibleOrdinal() const {
    return cursor_.FirstVisibleOrdinal();
  }
  [[nodiscard]] constexpr bool NumberFocus() const {
    return input_.Held(TrackerAction::Edit);
  }
  [[nodiscard]] constexpr bool TrackFocus() const { return NumberFocus(); }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }

  constexpr void SetStructure(std::uint8_t fieldCount,
                              std::uint8_t operatorCount) {
    fieldCount_ = ClampFieldCount(fieldCount);
    operatorCount_ = ClampOperatorCount(operatorCount);
    cursor_.SetEnabledMask(EnabledRowsMask(fieldCount_, operatorCount_));
    if (operatorCount_ == 0U)
      operatorColumn_ = 0U;
  }

  constexpr void SetTypeSelector(Ui2SelectorState selector) {
    typeSelector_ = selector;
  }

  [[nodiscard]] constexpr Ui2InstrumentBottomState Bottom() const {
    if (TrackFocus()) {
      return {.kind = Ui2InstrumentBottomKind::TrackNotes,
              .selectedIndex = selectedTrack_,
              .optionCount = TrackCount};
    }
    const Ui2InstrumentCursorPosition cursor = Cursor();
    if (cursor.kind == Ui2InstrumentCursorKind::Name) {
      return {.kind = Ui2InstrumentBottomKind::NameActions,
              .selectedIndex = static_cast<std::uint8_t>(nameAction_),
              .optionCount = NameActionCount()};
    }
    if (cursor.kind == Ui2InstrumentCursorKind::Type &&
        typeSelector_.Valid()) {
      return {.kind = Ui2InstrumentBottomKind::TypeSelector,
              .selectedIndex =
                  static_cast<std::uint8_t>(typeSelector_.current),
              .optionCount = static_cast<std::uint8_t>(typeSelector_.count),
              .wrap = typeSelector_.wrap};
    }
    return {};
  }

  constexpr Ui2InstrumentCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed))
      return {};

    if (!pressed) {
      if (action == TrackerAction::Enter && valueEditDirty_) {
        valueEditDirty_ = false;
        return MakeCommand(Ui2InstrumentCommandType::CommitValueEdits);
      }
      return {};
    }

    const Ui2InstrumentValueDirection direction = DirectionFor(action);
    if (input_.Held(TrackerAction::Edit)) {
      if (direction == Ui2InstrumentValueDirection::Left ||
          direction == Ui2InstrumentValueDirection::Right) {
        return SelectTrack(direction);
      }
      if (direction == Ui2InstrumentValueDirection::Up ||
          direction == Ui2InstrumentValueDirection::Down) {
        return SelectNumber(direction);
      }
      if (action == TrackerAction::Play)
        return MakeCommand(Ui2InstrumentCommandType::OpenRecord);
      return {};
    }

    if (input_.Held(TrackerAction::Enter)) {
      if (direction != Ui2InstrumentValueDirection::None)
        return HandleEnterDirection(direction);
      if (action == TrackerAction::Enter &&
          input_.Mask() == TrackerActionBit(TrackerAction::Enter)) {
        const Ui2InstrumentCursorPosition cursor = Cursor();
        if (cursor.kind == Ui2InstrumentCursorKind::Name)
          return MakeCommand(NameCommand(nameAction_));
        if (cursor.kind == Ui2InstrumentCursorKind::Field ||
            cursor.kind == Ui2InstrumentCursorKind::Operator1 ||
            cursor.kind == Ui2InstrumentCursorKind::Operator2) {
          return MakeCommand(Ui2InstrumentCommandType::ActivateField);
        }
      }
      return {};
    }

    if (input_.AnyModifier())
      return {};

    if (action == TrackerAction::Up) {
      cursor_.MovePrevious();
      return {};
    }
    if (action == TrackerAction::Down) {
      cursor_.MoveNext();
      return {};
    }
    if (action == TrackerAction::Left || action == TrackerAction::Right)
      return HandleHorizontal(action == TrackerAction::Left ? -1 : 1);
    if (action == TrackerAction::Play)
      return MakeCommand(Ui2InstrumentCommandType::StartPlayback);
    return {};
  }

private:
  [[nodiscard]] static constexpr std::uint8_t
  ClampFieldCount(std::uint8_t count) {
    return count < MaximumFields ? count : MaximumFields;
  }

  [[nodiscard]] static constexpr std::uint8_t
  ClampOperatorCount(std::uint8_t count) {
    return count < MaximumOperatorRows ? count : MaximumOperatorRows;
  }

  [[nodiscard]] static constexpr std::uint8_t
  SanitizeNumber(std::uint8_t number, std::uint8_t count) {
    return count == 0U ? 0U : number < count ? number
                                             : static_cast<std::uint8_t>(count - 1U);
  }

  [[nodiscard]] static constexpr std::uint32_t
  EnabledRowsMask(std::uint8_t fieldCount, std::uint8_t operatorCount) {
    const std::uint8_t count =
        static_cast<std::uint8_t>(2U + fieldCount + operatorCount);
    return count >= 32U ? 0xFFFFFFFFU
                        : (std::uint32_t{1} << count) - 1U;
  }

  [[nodiscard]] static constexpr std::uint8_t
  RowFor(Ui2InstrumentCursorPosition cursor, std::uint8_t fieldCount,
         std::uint8_t operatorCount) {
    switch (cursor.kind) {
    case Ui2InstrumentCursorKind::Name:
      return 0U;
    case Ui2InstrumentCursorKind::Type:
      return 1U;
    case Ui2InstrumentCursorKind::Field:
      return fieldCount == 0U
                 ? 1U
                 : static_cast<std::uint8_t>(
                       2U + (cursor.index < fieldCount ? cursor.index
                                                       : fieldCount - 1U));
    case Ui2InstrumentCursorKind::Operator1:
    case Ui2InstrumentCursorKind::Operator2:
      return operatorCount == 0U
                 ? (fieldCount == 0U
                        ? 1U
                        : static_cast<std::uint8_t>(1U + fieldCount))
                 : static_cast<std::uint8_t>(
                       2U + fieldCount +
                       (cursor.index < operatorCount ? cursor.index
                                                     : operatorCount - 1U));
    }
    return 0U;
  }

  [[nodiscard]] static constexpr std::uint8_t NameActionCount() {
    return static_cast<std::uint8_t>(Ui2InstrumentNameAction::Count);
  }

  [[nodiscard]] static constexpr Ui2InstrumentValueDirection
  DirectionFor(TrackerAction action) {
    switch (action) {
    case TrackerAction::Left:
      return Ui2InstrumentValueDirection::Left;
    case TrackerAction::Down:
      return Ui2InstrumentValueDirection::Down;
    case TrackerAction::Right:
      return Ui2InstrumentValueDirection::Right;
    case TrackerAction::Up:
      return Ui2InstrumentValueDirection::Up;
    case TrackerAction::Alt:
    case TrackerAction::Edit:
    case TrackerAction::Enter:
    case TrackerAction::Nav:
    case TrackerAction::Play:
    case TrackerAction::Select:
    case TrackerAction::Power:
    case TrackerAction::Count:
      return Ui2InstrumentValueDirection::None;
    }
    return Ui2InstrumentValueDirection::None;
  }

  [[nodiscard]] constexpr Ui2InstrumentCommand
  MakeCommand(Ui2InstrumentCommandType type) const {
    return {.type = type, .cursor = Cursor()};
  }

  [[nodiscard]] static constexpr Ui2InstrumentCommandType
  NameCommand(Ui2InstrumentNameAction action) {
    switch (action) {
    case Ui2InstrumentNameAction::Load:
      return Ui2InstrumentCommandType::LoadInstrument;
    case Ui2InstrumentNameAction::Save:
      return Ui2InstrumentCommandType::SaveInstrument;
    case Ui2InstrumentNameAction::Rename:
      return Ui2InstrumentCommandType::RenameInstrument;
    case Ui2InstrumentNameAction::Count:
      return Ui2InstrumentCommandType::None;
    }
    return Ui2InstrumentCommandType::None;
  }

  constexpr Ui2InstrumentCommand HandleHorizontal(std::int8_t delta) {
    const Ui2InstrumentCursorPosition cursor = Cursor();
    if (cursor.kind == Ui2InstrumentCursorKind::Name) {
      const std::uint8_t current = static_cast<std::uint8_t>(nameAction_);
      nameAction_ = static_cast<Ui2InstrumentNameAction>(
          delta < 0 ? (current == 0U ? NameActionCount() - 1U : current - 1U)
                    : (current + 1U) % NameActionCount());
      return {};
    }
    if (cursor.kind == Ui2InstrumentCursorKind::Type) {
      if (!typeSelector_.Move(delta))
        return {};
      Ui2InstrumentCommand command = MakeCommand(Ui2InstrumentCommandType::SetType);
      command.value = static_cast<std::int16_t>(typeSelector_.current);
      command.direction = delta < 0 ? Ui2InstrumentValueDirection::Left
                                    : Ui2InstrumentValueDirection::Right;
      return command;
    }
    if (cursor.kind == Ui2InstrumentCursorKind::Operator1 && delta > 0)
      operatorColumn_ = 1U;
    else if (cursor.kind == Ui2InstrumentCursorKind::Operator2 && delta < 0)
      operatorColumn_ = 0U;
    return {};
  }

  constexpr Ui2InstrumentCommand
  HandleEnterDirection(Ui2InstrumentValueDirection direction) {
    const Ui2InstrumentCursorPosition cursor = Cursor();
    if (cursor.kind == Ui2InstrumentCursorKind::Name)
      return {};
    if (cursor.kind == Ui2InstrumentCursorKind::Type &&
        (direction == Ui2InstrumentValueDirection::Left ||
         direction == Ui2InstrumentValueDirection::Right)) {
      return HandleHorizontal(direction == Ui2InstrumentValueDirection::Left
                                  ? -1
                                  : 1);
    }
    Ui2InstrumentCommand command =
        MakeCommand(Ui2InstrumentCommandType::AdjustField);
    command.direction = direction;
    command.value = direction == Ui2InstrumentValueDirection::Left ||
                            direction == Ui2InstrumentValueDirection::Down
                        ? -1
                        : 1;
    valueEditDirty_ = true;
    return command;
  }

  constexpr Ui2InstrumentCommand
  SelectTrack(Ui2InstrumentValueDirection direction) {
    const std::uint8_t previous = selectedTrack_;
    if (direction == Ui2InstrumentValueDirection::Left) {
      if (selectedTrack_ > 0U)
        --selectedTrack_;
    } else if (selectedTrack_ + 1U < TrackCount) {
      ++selectedTrack_;
    }
    if (previous == selectedTrack_)
      return {};
    Ui2InstrumentCommand command =
        MakeCommand(Ui2InstrumentCommandType::SelectTrack);
    command.direction = direction;
    command.value = selectedTrack_;
    return command;
  }

  constexpr Ui2InstrumentCommand
  SelectNumber(Ui2InstrumentValueDirection direction) {
    if (instrumentCount_ == 0U)
      return {};
    const std::uint8_t previous = number_;
    if (direction == Ui2InstrumentValueDirection::Up) {
      if (number_ > 0U)
        --number_;
      else if (instrumentWrap_)
        number_ = static_cast<std::uint8_t>(instrumentCount_ - 1U);
    } else {
      if (number_ + 1U < instrumentCount_)
        ++number_;
      else if (instrumentWrap_)
        number_ = 0U;
    }
    if (previous == number_)
      return {};
    Ui2InstrumentCommand command =
        MakeCommand(Ui2InstrumentCommandType::SelectNumber);
    command.direction = direction;
    command.value = number_;
    return command;
  }

  Ui2FixedListCursor<MaximumRows> cursor_{};
  Ui2ControllerInputState input_{};
  Ui2SelectorState typeSelector_{5U, 0U, true};
  std::uint8_t fieldCount_ = 0;
  std::uint8_t operatorCount_ = 0;
  std::uint8_t number_ = 0;
  std::uint8_t instrumentCount_ = DefaultInstrumentCount;
  std::uint8_t selectedTrack_ = 0;
  std::uint8_t operatorColumn_ = 0;
  Ui2InstrumentNameAction nameAction_ = Ui2InstrumentNameAction::Load;
  bool instrumentWrap_ = true;
  bool valueEditDirty_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2InstrumentCursorPosition>);
static_assert(std::is_trivially_copyable_v<Ui2InstrumentCommand>);
static_assert(std::is_trivially_copyable_v<Ui2InstrumentBottomState>);
static_assert(std::is_trivially_copyable_v<Ui2InstrumentController>);
static_assert(sizeof(Ui2InstrumentController) <= 40U);

} // namespace ui2
