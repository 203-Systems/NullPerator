/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2TrackerGridController.h"

namespace ui2 {

class Ui2ChainController {
public:
  constexpr Ui2ChainController(std::uint8_t number = 0,
                               std::uint8_t selectedTrack = 0,
                               std::uint8_t row = 0, std::uint8_t column = 0)
      : grid_(row, column), number_(number == 0xFFU ? 0xFEU : number),
        selectedTrack_(Ui2ClampTrack(selectedTrack)) {}

  [[nodiscard]] constexpr std::uint8_t Row() const { return grid_.Row(); }
  [[nodiscard]] constexpr std::uint8_t Column() const { return grid_.Column(); }
  [[nodiscard]] constexpr std::uint8_t Number() const { return number_; }
  [[nodiscard]] constexpr std::uint8_t SelectedTrack() const {
    return selectedTrack_;
  }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }
  constexpr void SetNavigationHeld(bool held) {
    input_.SetNavigationHeld(held);
  }
  constexpr void SynchronizeHeldModifiers(std::uint16_t mask) {
    input_.SynchronizeModifiers(mask);
  }
  [[nodiscard]] constexpr bool ClonePending() const {
    return clonePending_ && input_.Held(TrackerAction::Shift);
  }
  [[nodiscard]] constexpr const Ui2GridSelectionState &Selection() const {
    return selection_;
  }
  [[nodiscard]] constexpr bool NumberFocus() const {
    return !selection_.active && input_.Held(TrackerAction::Option);
  }
  [[nodiscard]] constexpr bool TrackFocus() const { return NumberFocus(); }

  constexpr Ui2TrackerCommandBatch<> Handle(TrackerAction action,
                                            bool pressed) {
    Ui2TrackerCommandBatch<> output;
    const bool wasHeld = input_.Held(action);
    if (!input_.Update(action, pressed))
      return output;

    if (!pressed) {
      if (action == TrackerAction::Option && wasHeld && clonePending_ &&
          selection_.active && !input_.Held(TrackerAction::Shift)) {
        Ui2TrackerCommand command =
            Command(Ui2TrackerCommandType::CopySelection);
        command.selection = selection_;
        output.Push(command);
        selection_.Clear();
        clonePending_ = false;
      }
      if (action == TrackerAction::Shift)
        clonePending_ = false;
      if (action == TrackerAction::Edit && valueEditDirty_) {
        output.Push(Command(Ui2TrackerCommandType::CommitValueEdits));
        valueEditDirty_ = false;
      }
      return output;
    }

    if (action != TrackerAction::Edit)
      newEntryPending_ = false;

    if (action == TrackerAction::Play && input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Edit)) {
      clonePending_ = false;
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
      clonePending_ = grid_.Column() == 0U;
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
        Ui2TrackerCommand command = Command(Ui2TrackerCommandType::AdjustCell);
        command.direction = direction;
        command.value = CellDelta(direction);
        output.Push(command);
        valueEditDirty_ = true;
      } else if (action == TrackerAction::Edit &&
                 input_.Mask() == TrackerActionBit(TrackerAction::Edit)) {
        if (newEntryPending_) {
          output.Push(Command(Ui2TrackerCommandType::AllocateNext));
          newEntryPending_ = false;
        } else {
          output.Push(Command(Ui2TrackerCommandType::PasteLast));
          newEntryPending_ = grid_.Column() == 0U;
        }
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
  [[nodiscard]] constexpr Ui2TrackerCommand
  Command(Ui2TrackerCommandType type) const {
    return Ui2MakeTrackerCommand(type, Ui2TrackerPage::Chain, grid_.Row(),
                                 grid_.Column(), selectedTrack_);
  }

  [[nodiscard]] constexpr std::int16_t
  CellDelta(Ui2TrackerEditDirection direction) const {
    switch (direction) {
    case Ui2TrackerEditDirection::Left:
      return -1;
    case Ui2TrackerEditDirection::Right:
      return 1;
    case Ui2TrackerEditDirection::Down:
      return grid_.Column() == 0U ? -16 : -12;
    case Ui2TrackerEditDirection::Up:
      return grid_.Column() == 0U ? 16 : 12;
    case Ui2TrackerEditDirection::None:
      return 0;
    }
    return 0;
  }

  constexpr void HandleSelection(TrackerAction action,
                                 Ui2TrackerCommandBatch<> &output) {
    clonePending_ = false;
    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (action == TrackerAction::Shift && input_.Held(TrackerAction::Option)) {
      output.Push(Command(Ui2TrackerCommandType::ToggleMute));
      return;
    }
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
      selection_.ExpandColumnsThenRows(1U, 0U, kUi2TrackerVisibleRows - 1U);
      return;
    }
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Edit)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::CutSelection);
      command.selection = selection_;
      output.Push(command);
      selection_.Clear();
      return;
    }
    if (action == TrackerAction::Edit && input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Shift)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::CutSelection);
      command.selection = selection_;
      output.Push(command);
      selection_.Clear();
      return;
    }

    if (action == TrackerAction::Option &&
        input_.Mask() == TrackerActionBit(TrackerAction::Option)) {
      clonePending_ = true;
      return;
    }
    if (input_.Held(TrackerAction::Edit) &&
        direction != Ui2TrackerEditDirection::None) {
      Ui2TrackerCommand command =
          Command(Ui2TrackerCommandType::AdjustSelection);
      command.direction = direction;
      command.value = CellDelta(direction);
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
    output.Push(command);
  }

  constexpr void HandleNav(TrackerAction action,
                           Ui2TrackerCommandBatch<> &output) const {
    Ui2TrackerPage target = Ui2TrackerPage::None;
    if (action == TrackerAction::Left)
      target = Ui2TrackerPage::Song;
    else if (action == TrackerAction::Right)
      target = Ui2TrackerPage::Phrase;
    else if (action == TrackerAction::Play) {
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

  Ui2FixedGridCursor<2> grid_{};
  Ui2ControllerInputState input_{};
  Ui2GridSelectionState selection_{};
  std::uint8_t number_ = 0;
  std::uint8_t selectedTrack_ = 0;
  bool newEntryPending_ = false;
  bool valueEditDirty_ = false;
  bool clonePending_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2ChainController>);
static_assert(sizeof(Ui2ChainController) <= 16U);

} // namespace ui2
