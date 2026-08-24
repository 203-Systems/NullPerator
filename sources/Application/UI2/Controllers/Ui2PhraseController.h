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
                                std::uint8_t row = 0,
                                std::uint8_t column = 0,
                                std::uint8_t parameterDigit = 3)
      : grid_(row, column), number_(number < 0x80U ? number : 0x7FU),
        selectedTrack_(Ui2ClampTrack(selectedTrack)),
        parameterDigit_(parameterDigit < 4U ? parameterDigit : 3U) {}

  [[nodiscard]] constexpr std::uint8_t Row() const { return grid_.Row(); }
  [[nodiscard]] constexpr std::uint8_t Column() const {
    return grid_.Column();
  }
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
    input_.Update(TrackerAction::Nav, held);
  }
  [[nodiscard]] constexpr bool ClonePending() const { return clonePending_; }
  [[nodiscard]] constexpr const Ui2GridSelectionState &Selection() const {
    return selection_;
  }
  [[nodiscard]] constexpr bool NumberFocus() const {
    return !selection_.active && input_.Held(TrackerAction::Edit);
  }
  [[nodiscard]] constexpr bool TrackFocus() const { return NumberFocus(); }
  [[nodiscard]] constexpr bool EnterDigitFocus() const {
    return !NumberFocus() && !selection_.active && IsParameterColumn() &&
           input_.Held(TrackerAction::Enter);
  }

  constexpr Ui2TrackerCommandBatch<> Handle(TrackerAction action,
                                             bool pressed) {
    Ui2TrackerCommandBatch<> output;
    if (!input_.Update(action, pressed))
      return output;

    if (!pressed) {
      if (action == TrackerAction::Enter && auditionActive_) {
        auditionActive_ = false;
        output.Push(Command(Ui2TrackerCommandType::StopAudition));
      }
      return output;
    }

    if (action != TrackerAction::Enter)
      newEntryPending_ = false;

    if (action == TrackerAction::Select) {
      clonePending_ = false;
      ToggleSelection();
      return output;
    }

    if (clonePending_) {
      if (input_.Held(TrackerAction::Edit) &&
          input_.Held(TrackerAction::Alt) &&
          input_.Held(TrackerAction::Enter) &&
          action == TrackerAction::Enter) {
        clonePending_ = false;
        output.Push(Command(Ui2TrackerCommandType::CloneCell));
        return output;
      }
      if (action != TrackerAction::Edit && action != TrackerAction::Alt) {
        clonePending_ = false;
        selection_.Begin(grid_.Column(), grid_.Row());
        HandleSelection(action, output);
        return output;
      }
    }

    if (selection_.active) {
      HandleSelection(action, output);
      return output;
    }

    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (input_.Held(TrackerAction::Edit)) {
      if (input_.Held(TrackerAction::Alt) &&
          input_.Held(TrackerAction::Enter) &&
          action == TrackerAction::Enter) {
        output.Push(Command(Ui2TrackerCommandType::CloneCell));
      } else if (input_.Held(TrackerAction::Alt) &&
                 (action == TrackerAction::Alt ||
                  action == TrackerAction::Edit)) {
        clonePending_ = true;
      } else if (direction != Ui2TrackerEditDirection::None) {
        HandleEditDirection(direction, output);
      } else if (input_.Held(TrackerAction::Enter) &&
                 action == TrackerAction::Enter) {
        output.Push(Command(Ui2TrackerCommandType::CutCell));
      } else if (input_.Held(TrackerAction::Nav) &&
                 action == TrackerAction::Nav) {
        output.Push(Command(Ui2TrackerCommandType::ToggleMute));
      } else if (action == TrackerAction::Play) {
        output.Push(Command(Ui2TrackerCommandType::OpenRecord));
      }
      return output;
    }

    if (input_.Held(TrackerAction::Enter)) {
      if (direction != Ui2TrackerEditDirection::None) {
        HandleEnterDirection(direction, output);
      } else if (input_.Held(TrackerAction::Alt) &&
                 action == TrackerAction::Alt) {
        output.Push(Command(Ui2TrackerCommandType::PasteSelection));
      } else if (input_.Held(TrackerAction::Nav) &&
                 action == TrackerAction::Nav) {
        output.Push(Command(Ui2TrackerCommandType::ToggleSolo));
      } else if (action == TrackerAction::Enter &&
                 input_.Mask() == TrackerActionBit(TrackerAction::Enter)) {
        HandlePlainEnter(output);
      }
      return output;
    }

    if (input_.Held(TrackerAction::Nav)) {
      HandleNav(action, output);
      return output;
    }

    if (!input_.AnyModifier() &&
        direction != Ui2TrackerEditDirection::None) {
      grid_.Move(direction);
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
    Ui2TrackerCommand command = Ui2MakeTrackerCommand(
        type, Ui2TrackerPage::Phrase, grid_.Row(), grid_.Column(),
        selectedTrack_);
    command.digit = parameterDigit_;
    return command;
  }

  constexpr void ToggleSelection() {
    if (selection_.active)
      selection_.Clear();
    else
      selection_.Begin(grid_.Column(), grid_.Row());
  }

  constexpr void HandleSelection(TrackerAction action,
                                 Ui2TrackerCommandBatch<> &output) {
    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (input_.Held(TrackerAction::Edit) &&
        input_.Held(TrackerAction::Alt)) {
      selection_.ExpandColumnsThenRows(5U, 0U,
                                       kUi2TrackerVisibleRows - 1U);
      return;
    }
    if (input_.Held(TrackerAction::Enter) &&
        input_.Held(TrackerAction::Alt) &&
        (action == TrackerAction::Enter || action == TrackerAction::Alt)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::CutSelection);
      command.selection = selection_;
      output.Push(command);
      selection_.Clear();
      return;
    }
    if (input_.Held(TrackerAction::Edit) &&
        action == TrackerAction::Edit &&
        !input_.Held(TrackerAction::Alt)) {
      Ui2TrackerCommand command =
          Command(Ui2TrackerCommandType::CopySelection);
      command.selection = selection_;
      output.Push(command);
      selection_.Clear();
      return;
    }
    if (input_.Held(TrackerAction::Enter) &&
        direction != Ui2TrackerEditDirection::None) {
      Ui2TrackerCommand command =
          Command(Ui2TrackerCommandType::AdjustSelection);
      command.direction = direction;
      command.selection = selection_;
      output.Push(command);
      return;
    }
    if (!input_.AnyModifier() &&
        direction != Ui2TrackerEditDirection::None) {
      grid_.Move(direction);
      selection_.Follow(grid_.Column(), grid_.Row());
    }
  }

  constexpr void HandleEditDirection(Ui2TrackerEditDirection direction,
                                     Ui2TrackerCommandBatch<> &output) {
    if (direction == Ui2TrackerEditDirection::Left ||
        direction == Ui2TrackerEditDirection::Right) {
      const std::int16_t delta =
          direction == Ui2TrackerEditDirection::Left ? -1 : 1;
      selectedTrack_ = Ui2ClampTrack(
          static_cast<std::int16_t>(selectedTrack_) + delta);
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
  }

  constexpr void HandlePlainEnter(Ui2TrackerCommandBatch<> &output) {
    if (newEntryPending_) {
      output.Push(Command(Ui2TrackerCommandType::AllocateNext));
      newEntryPending_ = false;
    } else {
      output.Push(Command(Ui2TrackerCommandType::PasteLast));
      newEntryPending_ = grid_.Column() == 1U || grid_.Column() == 3U ||
                         grid_.Column() == 5U;
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
    case TrackerAction::Play:
      output.Push(Command(Ui2TrackerCommandType::StartPlayback));
      return;
    case TrackerAction::Alt:
      output.Push(Command(Ui2TrackerCommandType::UnmuteAll));
      return;
    case TrackerAction::Edit:
    case TrackerAction::Enter:
    case TrackerAction::Nav:
    case TrackerAction::Select:
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
};

static_assert(std::is_trivially_copyable_v<Ui2PhraseController>);
static_assert(sizeof(Ui2PhraseController) <= 16U);

} // namespace ui2
