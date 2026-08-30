/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2TrackerGridController.h"

namespace ui2 {

class Ui2TableController {
public:
  constexpr Ui2TableController(
      Ui2TrackerPage tablePage = Ui2TrackerPage::PhraseTable,
      std::uint8_t number = 0, std::uint8_t selectedTrack = 0,
      std::uint8_t row = 0, std::uint8_t column = 0,
      std::uint8_t parameterDigit = 3)
      : grid_(row, column), page_(SanitizePage(tablePage)),
        number_(number & 0x1FU), selectedTrack_(Ui2ClampTrack(selectedTrack)),
        parameterDigit_(parameterDigit < 4U ? parameterDigit : 3U) {}

  [[nodiscard]] constexpr std::uint8_t Row() const { return grid_.Row(); }
  [[nodiscard]] constexpr std::uint8_t Column() const { return grid_.Column(); }
  [[nodiscard]] constexpr Ui2TrackerPage Page() const { return page_; }
  [[nodiscard]] constexpr std::uint8_t Number() const { return number_; }
  [[nodiscard]] constexpr std::uint8_t SelectedTrack() const {
    return selectedTrack_;
  }
  [[nodiscard]] constexpr std::uint8_t ParameterDigit() const {
    return parameterDigit_;
  }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }
  constexpr void SetNavigationHeld(bool held) {
    input_.Update(TrackerAction::Shift, held);
  }
  constexpr void SynchronizeHeldModifiers(std::uint16_t mask) {
    input_.SynchronizeModifiers(mask);
  }
  [[nodiscard]] constexpr const Ui2GridSelectionState &Selection() const {
    return selection_;
  }
  [[nodiscard]] constexpr bool NumberFocus() const {
    return !selection_.active && input_.Held(TrackerAction::Option);
  }
  [[nodiscard]] constexpr bool TrackFocus() const { return NumberFocus(); }
  [[nodiscard]] constexpr bool EnterDigitFocus() const {
    return !NumberFocus() && !selection_.active && IsParameterColumn() &&
           input_.Held(TrackerAction::Edit);
  }

  constexpr Ui2TrackerCommandBatch<> Handle(TrackerAction action,
                                            bool pressed) {
    Ui2TrackerCommandBatch<> output;
    if (!input_.Update(action, pressed))
      return output;

    if (!pressed) {
      if (action == TrackerAction::Shift)
        clonePending_ = false;
      if (action == TrackerAction::Edit && valueEditDirty_) {
        output.Push(Command(Ui2TrackerCommandType::CommitValueEdits));
        valueEditDirty_ = false;
      }
      return output;
    }

    if (action == TrackerAction::Play && input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Edit)) {
      output.Push(Command(input_.Held(TrackerAction::Shift)
                              ? Ui2TrackerCommandType::UnmuteAll
                              : Ui2TrackerCommandType::ToggleSolo));
      return output;
    }
    if (clonePending_ && action == TrackerAction::Edit &&
        input_.Held(TrackerAction::Shift) &&
        !input_.Held(TrackerAction::Option)) {
      clonePending_ = false;
      selection_.Clear();
      output.Push(Command(Ui2TrackerCommandType::CloneCell));
      return output;
    }
    if (selection_.active) {
      HandleSelection(action, output);
      return output;
    }

    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Shift)) {
      selection_.Begin(grid_.Column(), grid_.Row());
      clonePending_ = IsParameterColumn();
      return output;
    }
    if (action == TrackerAction::Shift && input_.Held(TrackerAction::Option)) {
      output.Push(Command(Ui2TrackerCommandType::ToggleMute));
      return output;
    }
    if (input_.Held(TrackerAction::Option)) {
      if (action == TrackerAction::Option && input_.Held(TrackerAction::Edit)) {
        output.Push(Command(Ui2TrackerCommandType::CutCell));
      } else if (!input_.Held(TrackerAction::Shift) &&
                 !input_.Held(TrackerAction::Edit) &&
                 direction != Ui2TrackerEditDirection::None) {
        HandleOptionDirection(direction, output);
      }
      return output;
    }

    if (action == TrackerAction::Edit && input_.Held(TrackerAction::Shift)) {
      output.Push(Command(Ui2TrackerCommandType::PasteSelection));
      return output;
    }

    if (input_.Held(TrackerAction::Edit)) {
      if (direction != Ui2TrackerEditDirection::None) {
        HandleEnterDirection(direction, output);
      } else if (action == TrackerAction::Edit &&
                 input_.Mask() == TrackerActionBit(TrackerAction::Edit)) {
        output.Push(Command(Ui2TrackerCommandType::PasteLast));
      }
      return output;
    }

    if (input_.Held(TrackerAction::Shift)) {
      HandleNav(action, output);
      return output;
    }

    if (!input_.AnyModifier() && direction != Ui2TrackerEditDirection::None) {
      grid_.Move(direction);
    } else if (!input_.AnyModifier() && action == TrackerAction::Play) {
      output.Push(Command(Ui2TrackerCommandType::StartPlayback));
    }
    return output;
  }

private:
  [[nodiscard]] static constexpr Ui2TrackerPage
  SanitizePage(Ui2TrackerPage page) {
    return page == Ui2TrackerPage::InstrumentTable
               ? Ui2TrackerPage::InstrumentTable
               : Ui2TrackerPage::PhraseTable;
  }

  [[nodiscard]] constexpr bool IsParameterColumn() const {
    return (grid_.Column() & 1U) != 0U;
  }

  [[nodiscard]] constexpr Ui2TrackerCommand
  Command(Ui2TrackerCommandType type) const {
    Ui2TrackerCommand command = Ui2MakeTrackerCommand(
        type, page_, grid_.Row(), grid_.Column(), selectedTrack_);
    command.digit = parameterDigit_;
    return command;
  }

  [[nodiscard]] static constexpr std::int16_t
  SelectionDelta(Ui2TrackerEditDirection direction) {
    switch (direction) {
    case Ui2TrackerEditDirection::Left:
      return -1;
    case Ui2TrackerEditDirection::Right:
      return 1;
    case Ui2TrackerEditDirection::Down:
      return -16;
    case Ui2TrackerEditDirection::Up:
      return 16;
    case Ui2TrackerEditDirection::None:
      return 0;
    }
    return 0;
  }

  constexpr void HandleSelection(TrackerAction action,
                                 Ui2TrackerCommandBatch<> &output) {
    clonePending_ = false;
    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (action == TrackerAction::Play) {
      if (input_.Held(TrackerAction::Edit))
        return;
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::StartPlayback);
      command.selection = selection_;
      command.flag = input_.Held(TrackerAction::Shift);
      output.Push(command);
      return;
    }
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Shift)) {
      selection_.ExpandColumnsThenRows(5U, 0U, kUi2TrackerVisibleRows - 1U);
      return;
    }
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Edit)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::CutSelection);
      command.selection = selection_;
      output.Push(command);
      selection_.Clear();
      return;
    }
    if (action == TrackerAction::Option &&
        input_.Mask() == TrackerActionBit(TrackerAction::Option)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::CopySelection);
      command.selection = selection_;
      output.Push(command);
      selection_.Clear();
      return;
    }
    if (input_.Held(TrackerAction::Edit) &&
        direction != Ui2TrackerEditDirection::None) {
      Ui2TrackerCommand command =
          Command(Ui2TrackerCommandType::AdjustSelection);
      command.direction = direction;
      command.value = SelectionDelta(direction);
      command.selection = selection_;
      output.Push(command);
      valueEditDirty_ = true;
      return;
    }
    if (direction != Ui2TrackerEditDirection::None &&
        !input_.Held(TrackerAction::Shift) &&
        !input_.Held(TrackerAction::Option)) {
      if (grid_.Move(direction))
        selection_.Follow(grid_.Column(), grid_.Row());
    }
  }

  constexpr void HandleOptionDirection(Ui2TrackerEditDirection direction,
                                       Ui2TrackerCommandBatch<> &output) {
    if (direction == Ui2TrackerEditDirection::Left ||
        direction == Ui2TrackerEditDirection::Right) {
      const std::int16_t delta =
          direction == Ui2TrackerEditDirection::Left ? -1 : 1;
      selectedTrack_ =
          Ui2ClampTrack(static_cast<std::int16_t>(selectedTrack_) + delta);
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::SelectTrack);
      command.value = selectedTrack_;
      command.direction = direction;
      output.Push(command);
      return;
    }

    std::int16_t delta = 0;
    switch (direction) {
    case Ui2TrackerEditDirection::Down:
      delta = -16;
      break;
    case Ui2TrackerEditDirection::Up:
      delta = 16;
      break;
    case Ui2TrackerEditDirection::None:
      return;
    case Ui2TrackerEditDirection::Left:
    case Ui2TrackerEditDirection::Right:
      return;
    }
    number_ = static_cast<std::uint8_t>(
        (static_cast<std::int16_t>(number_) + delta + 64) % 32);
    Ui2TrackerCommand command = Command(Ui2TrackerCommandType::SelectNumber);
    command.value = number_;
    command.direction = direction;
    output.Push(command);
  }

  constexpr void HandleEnterDirection(Ui2TrackerEditDirection direction,
                                      Ui2TrackerCommandBatch<> &output) {
    if (IsParameterColumn()) {
      if (direction == Ui2TrackerEditDirection::Left) {
        if (parameterDigit_ > 0U)
          --parameterDigit_;
        return;
      }
      if (direction == Ui2TrackerEditDirection::Right) {
        if (parameterDigit_ < 3U)
          ++parameterDigit_;
        return;
      }
    }

    Ui2TrackerCommand command = Command(Ui2TrackerCommandType::AdjustCell);
    command.direction = direction;
    if (IsParameterColumn()) {
      const std::int16_t step = Ui2ParameterStep(parameterDigit_);
      command.value = direction == Ui2TrackerEditDirection::Up ? step : -step;
    } else {
      switch (direction) {
      case Ui2TrackerEditDirection::Left:
        command.value = -1;
        break;
      case Ui2TrackerEditDirection::Right:
        command.value = 1;
        break;
      case Ui2TrackerEditDirection::Down:
        command.value = -16;
        break;
      case Ui2TrackerEditDirection::Up:
        command.value = 16;
        break;
      case Ui2TrackerEditDirection::None:
        break;
      }
    }
    output.Push(command);
    valueEditDirty_ = true;
  }

  constexpr void HandleNav(TrackerAction action,
                           Ui2TrackerCommandBatch<> &output) const {
    Ui2TrackerPage target = Ui2TrackerPage::None;
    if (action == TrackerAction::Up) {
      target = page_ == Ui2TrackerPage::PhraseTable
                   ? Ui2TrackerPage::Phrase
                   : Ui2TrackerPage::Instrument;
    } else if (action == TrackerAction::Left &&
               page_ == Ui2TrackerPage::InstrumentTable) {
      target = Ui2TrackerPage::PhraseTable;
    } else if (action == TrackerAction::Right &&
               page_ == Ui2TrackerPage::PhraseTable) {
      target = Ui2TrackerPage::InstrumentTable;
    } else if (action == TrackerAction::Play) {
      Ui2TrackerCommand playback =
          Command(Ui2TrackerCommandType::StartPlayback);
      playback.flag = true;
      output.Push(playback);
      return;
    } else {
      return;
    }
    Ui2TrackerCommand command = Command(Ui2TrackerCommandType::SwitchPage);
    command.targetPage = target;
    output.Push(command);
  }

  Ui2FixedGridCursor<6> grid_{};
  Ui2HeldActionState input_{};
  Ui2GridSelectionState selection_{};
  Ui2TrackerPage page_ = Ui2TrackerPage::PhraseTable;
  std::uint8_t number_ = 0;
  std::uint8_t selectedTrack_ = 0;
  std::uint8_t parameterDigit_ = 3;
  bool clonePending_ = false;
  bool valueEditDirty_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2TableController>);
static_assert(sizeof(Ui2TableController) <= 16U);

} // namespace ui2
