/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

inline constexpr std::uint8_t kUi2TrackerVisibleRows = 16;
inline constexpr std::uint8_t kUi2TrackerTrackCount = 8;

enum class Ui2TrackerPage : std::uint8_t {
  None,
  Song,
  Chain,
  Phrase,
  PhraseTable,
  InstrumentTable,
  Project,
  Mixer,
  Groove,
  Instrument,
  Record,
};

enum class Ui2TrackerEditDirection : std::uint8_t {
  None,
  Left,
  Down,
  Right,
  Up,
};

enum class Ui2TrackerCommandType : std::uint8_t {
  None,
  AdjustCell,
  AdjustSelection,
  CommitValueEdits,
  PasteLast,
  AllocateNext,
  CutCell,
  CloneCell,
  CopySelection,
  CutSelection,
  PasteSelection,
  SelectTrack,
  SelectNumber,
  WarpVertical,
  SetLiveMode,
  SwitchPage,
  StartPlayback,
  StartImmediate,
  StopPlayback,
  ToggleMute,
  ToggleSolo,
  UnmuteAll,
  JumpSection,
  NudgeTempo,
  StartAudition,
  StopAudition,
};

struct Ui2GridSelectionState {
  std::uint8_t anchorColumn = 0;
  std::uint8_t anchorRow = 0;
  std::uint8_t activeColumn = 0;
  std::uint8_t activeRow = 0;
  bool active = false;

  constexpr void Begin(std::uint8_t column, std::uint8_t row) {
    anchorColumn = column;
    anchorRow = row;
    activeColumn = column;
    activeRow = row;
    active = true;
  }

  constexpr void Follow(std::uint8_t column, std::uint8_t row) {
    if (!active)
      return;
    activeColumn = column;
    activeRow = row;
  }

  constexpr void Clear() { active = false; }

  constexpr void ExpandColumnsThenRows(std::uint8_t maximumColumn,
                                       std::uint8_t firstRow,
                                       std::uint8_t lastRow) {
    if (!active)
      return;
    if (Left() > 0U || Right() < maximumColumn) {
      if (activeColumn < anchorColumn) {
        activeColumn = 0;
        anchorColumn = maximumColumn;
      } else {
        anchorColumn = 0;
        activeColumn = maximumColumn;
      }
      return;
    }
    if (activeRow < anchorRow) {
      activeRow = firstRow;
      anchorRow = lastRow;
    } else {
      anchorRow = firstRow;
      activeRow = lastRow;
    }
  }

  [[nodiscard]] constexpr std::uint8_t Left() const {
    return anchorColumn < activeColumn ? anchorColumn : activeColumn;
  }

  [[nodiscard]] constexpr std::uint8_t Right() const {
    return anchorColumn > activeColumn ? anchorColumn : activeColumn;
  }

  [[nodiscard]] constexpr std::uint8_t Top() const {
    return anchorRow < activeRow ? anchorRow : activeRow;
  }

  [[nodiscard]] constexpr std::uint8_t Bottom() const {
    return anchorRow > activeRow ? anchorRow : activeRow;
  }

  [[nodiscard]] constexpr bool SingleCell() const {
    return active && anchorColumn == activeColumn && anchorRow == activeRow;
  }
};

struct Ui2TrackerCommand {
  Ui2TrackerCommandType type = Ui2TrackerCommandType::None;
  Ui2TrackerPage sourcePage = Ui2TrackerPage::None;
  Ui2TrackerPage targetPage = Ui2TrackerPage::None;
  Ui2TrackerEditDirection direction = Ui2TrackerEditDirection::None;
  Ui2GridSelectionState selection{};
  std::int16_t value = 0;
  std::uint8_t row = 0;
  std::uint8_t column = 0;
  std::uint8_t track = 0;
  std::uint8_t digit = 0;
  bool flag = false;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2TrackerCommandType::None;
  }
};

// A single input edge emits at most one model/navigation command plus one
// transport or performance command. Keeping the default at two bounds stack
// traffic on the embedded input path.
template <std::size_t Capacity = 2> struct Ui2TrackerCommandBatch {
  std::array<Ui2TrackerCommand, Capacity> commands{};
  std::uint8_t count = 0;

  [[nodiscard]] constexpr bool Empty() const { return count == 0; }

  [[nodiscard]] constexpr const Ui2TrackerCommand &
  operator[](std::size_t index) const {
    return commands[index];
  }

  constexpr bool Push(const Ui2TrackerCommand &command) {
    if (!command.HasValue() || count >= Capacity)
      return false;
    commands[count++] = command;
    return true;
  }
};

class Ui2HeldActionState {
public:
  constexpr bool Update(TrackerAction action, bool pressed) {
    if (action >= TrackerAction::Count)
      return false;
    const std::uint16_t bit = TrackerActionBit(action);
    if (pressed)
      mask_ = static_cast<std::uint16_t>(mask_ | bit);
    else
      mask_ = static_cast<std::uint16_t>(mask_ & ~bit);
    return true;
  }

  [[nodiscard]] constexpr bool Held(TrackerAction action) const {
    return action < TrackerAction::Count &&
           (mask_ & TrackerActionBit(action)) != 0U;
  }

  [[nodiscard]] constexpr bool AnyModifier() const {
    constexpr std::uint16_t modifiers =
        TrackerActionBit(TrackerAction::Shift) |
        TrackerActionBit(TrackerAction::Option) |
        TrackerActionBit(TrackerAction::Edit);
    return (mask_ & modifiers) != 0U;
  }

  [[nodiscard]] constexpr std::uint16_t Mask() const { return mask_; }

  constexpr void SynchronizeModifiers(std::uint16_t mask) {
    constexpr std::uint16_t modifiers =
        TrackerActionBit(TrackerAction::Shift) |
        TrackerActionBit(TrackerAction::Option) |
        TrackerActionBit(TrackerAction::Edit);
    mask_ = static_cast<std::uint16_t>((mask_ & ~modifiers) |
                                      (mask & modifiers));
  }

private:
  std::uint16_t mask_ = 0;
};

template <std::uint8_t ColumnCount> class Ui2FixedGridCursor {
public:
  static_assert(ColumnCount > 0U);

  constexpr Ui2FixedGridCursor(std::uint8_t row = 0, std::uint8_t column = 0)
      : row_(row < kUi2TrackerVisibleRows ? row : kUi2TrackerVisibleRows - 1U),
        column_(column < ColumnCount ? column : ColumnCount - 1U) {}

  [[nodiscard]] constexpr std::uint8_t Row() const { return row_; }
  [[nodiscard]] constexpr std::uint8_t Column() const { return column_; }

  constexpr bool Move(Ui2TrackerEditDirection direction) {
    const std::uint8_t previousRow = row_;
    const std::uint8_t previousColumn = column_;
    switch (direction) {
    case Ui2TrackerEditDirection::Left:
      if (column_ > 0U)
        --column_;
      break;
    case Ui2TrackerEditDirection::Right:
      if (column_ + 1U < ColumnCount)
        ++column_;
      break;
    case Ui2TrackerEditDirection::Up:
      if (row_ > 0U)
        --row_;
      break;
    case Ui2TrackerEditDirection::Down:
      if (row_ + 1U < kUi2TrackerVisibleRows)
        ++row_;
      break;
    case Ui2TrackerEditDirection::None:
      break;
    }
    return row_ != previousRow || column_ != previousColumn;
  }

private:
  std::uint8_t row_ = 0;
  std::uint8_t column_ = 0;
};

[[nodiscard]] constexpr bool IsUi2TrackerDirection(TrackerAction action) {
  return action == TrackerAction::Left || action == TrackerAction::Down ||
         action == TrackerAction::Right || action == TrackerAction::Up;
}

[[nodiscard]] constexpr Ui2TrackerEditDirection
Ui2TrackerDirectionFor(TrackerAction action) {
  switch (action) {
  case TrackerAction::Left:
    return Ui2TrackerEditDirection::Left;
  case TrackerAction::Down:
    return Ui2TrackerEditDirection::Down;
  case TrackerAction::Right:
    return Ui2TrackerEditDirection::Right;
  case TrackerAction::Up:
    return Ui2TrackerEditDirection::Up;
  case TrackerAction::Shift:
  case TrackerAction::Option:
  case TrackerAction::Edit:
  case TrackerAction::Play:
  case TrackerAction::Reserved8:
  case TrackerAction::Reserved9:
  case TrackerAction::Power:
  case TrackerAction::Count:
    return Ui2TrackerEditDirection::None;
  }
  return Ui2TrackerEditDirection::None;
}

[[nodiscard]] constexpr std::int16_t
Ui2ParameterStep(std::uint8_t leftToRightDigit) {
  const std::uint8_t digit = leftToRightDigit > 3U ? 3U : leftToRightDigit;
  return static_cast<std::int16_t>(1U << (4U * (3U - digit)));
}

[[nodiscard]] constexpr std::uint8_t Ui2ClampTrack(std::int16_t track) {
  if (track < 0)
    return 0;
  if (track >= kUi2TrackerTrackCount)
    return kUi2TrackerTrackCount - 1U;
  return static_cast<std::uint8_t>(track);
}

[[nodiscard]] constexpr Ui2TrackerCommand
Ui2MakeTrackerCommand(Ui2TrackerCommandType type, Ui2TrackerPage page,
                      std::uint8_t row, std::uint8_t column,
                      std::uint8_t track) {
  Ui2TrackerCommand command;
  command.type = type;
  command.sourcePage = page;
  command.row = row;
  command.column = column;
  command.track = track;
  return command;
}

static_assert(std::is_trivially_copyable_v<Ui2GridSelectionState>);
static_assert(std::is_trivially_copyable_v<Ui2TrackerCommand>);
static_assert(std::is_trivially_copyable_v<Ui2TrackerCommandBatch<>>);
static_assert(std::is_trivially_copyable_v<Ui2HeldActionState>);
static_assert(sizeof(Ui2TrackerCommandBatch<>) <= 40U);

} // namespace ui2
