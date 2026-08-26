/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2TrackerGridController.h"

namespace ui2 {

class Ui2PhraseController {
public:
  constexpr Ui2PhraseController(std::uint8_t number = 0,
                                std::uint8_t selectedTrack = 0,
                                std::uint8_t row = 0, std::uint8_t column = 0,
                                std::uint8_t parameterDigit = 3)
      : grid_(row, column), number_(number < 0x80U ? number : 0x7FU),
        selectedTrack_(Ui2ClampTrack(selectedTrack)),
        parameterDigit_(parameterDigit < 4U ? parameterDigit : 3U) {}

  [[nodiscard]] constexpr std::uint8_t Row() const { return grid_.Row(); }
  [[nodiscard]] constexpr std::uint8_t Column() const { return grid_.Column(); }
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
  [[nodiscard]] constexpr bool ClonePending() const { return clonePending_; }
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
      if (action == TrackerAction::Edit && valueEditDirty_) {
        output.Push(Command(Ui2TrackerCommandType::CommitValueEdits));
        valueEditDirty_ = false;
      }
      if (action == TrackerAction::Edit && auditionActive_) {
        auditionActive_ = false;
        output.Push(Command(Ui2TrackerCommandType::StopAudition));
      }
      return output;
    }

    if (action != TrackerAction::Edit)
      newEntryPending_ = false;

    if (action == TrackerAction::Play && input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Edit)) {
      output.Push(Command(input_.Held(TrackerAction::Shift)
                              ? Ui2TrackerCommandType::UnmuteAll
                              : Ui2TrackerCommandType::ToggleSolo));
      return output;
    }
    if (selection_.active) {
      HandleSelection(action, output);
      return output;
    }

    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Shift)) {
      selection_.Begin(grid_.Column(), grid_.Row());
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
        HandleEditDirection(direction, output);
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
        HandlePlainEnter(output);
      }
      return output;
    }

    if (input_.Held(TrackerAction::Shift)) {
      HandleNav(action, output);
      return output;
    }

    if (!input_.AnyModifier() && direction != Ui2TrackerEditDirection::None) {
      if (!grid_.Move(direction) &&
          (direction == Ui2TrackerEditDirection::Up ||
           direction == Ui2TrackerEditDirection::Down)) {
        // Phrase rows are a window into the current Chain. Crossing 00/0F
        // asks the model to resolve the adjacent Chain phrase; an empty target
        // is rejected and synchronization leaves this cursor at the edge.
        Ui2TrackerCommand command = Command(Ui2TrackerCommandType::WarpVertical);
        command.direction = direction;
        command.value = direction == Ui2TrackerEditDirection::Up ? -1 : 1;
        output.Push(command);
      }
    } else if (!input_.AnyModifier() && action == TrackerAction::Play) {
      output.Push(Command(Ui2TrackerCommandType::StartPlayback));
    }
    return output;
  }

private:
  [[nodiscard]] constexpr bool IsParameterColumn() const {
    return grid_.Column() == 3U || grid_.Column() == 5U;
  }

  [[nodiscard]] constexpr Ui2TrackerCommand
  Command(Ui2TrackerCommandType type) const {
    Ui2TrackerCommand command =
        Ui2MakeTrackerCommand(type, Ui2TrackerPage::Phrase, grid_.Row(),
                              grid_.Column(), selectedTrack_);
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

  constexpr void HandleEditDirection(Ui2TrackerEditDirection direction,
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

    Ui2TrackerCommand command = Command(Ui2TrackerCommandType::WarpVertical);
    command.value = direction == Ui2TrackerEditDirection::Up ? -1 : 1;
    command.direction = direction;
    command.flag = true; // OPTION changes phrase context but preserves its row.
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
    }
    output.Push(command);
    valueEditDirty_ = true;
  }

  constexpr void HandlePlainEnter(Ui2TrackerCommandBatch<> &output) {
    if (newEntryPending_) {
      output.Push(Command(Ui2TrackerCommandType::AllocateNext));
      newEntryPending_ = false;
    } else {
      output.Push(Command(Ui2TrackerCommandType::PasteLast));
      newEntryPending_ =
          grid_.Column() == 1U || grid_.Column() == 3U || grid_.Column() == 5U;
    }

    if (grid_.Column() <= 1U) {
      output.Push(Command(Ui2TrackerCommandType::StartAudition));
      auditionActive_ = true;
    }
  }

  constexpr void HandleNav(TrackerAction action,
                           Ui2TrackerCommandBatch<> &output) const {
    Ui2TrackerPage target = Ui2TrackerPage::None;
    switch (action) {
    case TrackerAction::Left:
      target = Ui2TrackerPage::Chain;
      break;
    case TrackerAction::Right:
      target = Ui2TrackerPage::Instrument;
      break;
    case TrackerAction::Down:
      target = Ui2TrackerPage::PhraseTable;
      break;
    case TrackerAction::Up:
      target = Ui2TrackerPage::Groove;
      break;
    case TrackerAction::Play: {
      Ui2TrackerCommand playback =
          Command(Ui2TrackerCommandType::StartPlayback);
      playback.flag = true;
      output.Push(playback);
      return;
    }
    case TrackerAction::Option:
    case TrackerAction::Edit:
    case TrackerAction::Shift:
    case TrackerAction::Reserved8:
    case TrackerAction::Reserved9:
    case TrackerAction::Power:
    case TrackerAction::Count:
      return;
    }
    Ui2TrackerCommand command = Command(Ui2TrackerCommandType::SwitchPage);
    command.targetPage = target;
    output.Push(command);
  }

  Ui2FixedGridCursor<6> grid_{};
  Ui2HeldActionState input_{};
  Ui2GridSelectionState selection_{};
  std::uint8_t number_ = 0;
  std::uint8_t selectedTrack_ = 0;
  std::uint8_t parameterDigit_ = 3;
  bool newEntryPending_ = false;
  bool auditionActive_ = false;
  bool clonePending_ = false;
  bool valueEditDirty_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2PhraseController>);
static_assert(sizeof(Ui2PhraseController) <= 16U);

} // namespace ui2
