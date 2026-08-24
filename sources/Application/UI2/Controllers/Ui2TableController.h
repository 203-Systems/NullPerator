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
  constexpr Ui2TableController(Ui2TrackerPage tablePage =
                                   Ui2TrackerPage::PhraseTable,
                               std::uint8_t number = 0,
                               std::uint8_t selectedTrack = 0,
                               std::uint8_t row = 0,
                               std::uint8_t column = 0,
                               std::uint8_t parameterDigit = 3)
      : grid_(row, column), page_(SanitizePage(tablePage)),
        number_(number & 0x1FU),
        selectedTrack_(Ui2ClampTrack(selectedTrack)),
        parameterDigit_(parameterDigit < 4U ? parameterDigit : 3U) {}

  [[nodiscard]] constexpr std::uint8_t Row() const { return grid_.Row(); }
  [[nodiscard]] constexpr std::uint8_t Column() const {
    return grid_.Column();
  }
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
    if (!input_.Update(action, pressed) || !pressed)
      return output;

    if (action == TrackerAction::Select) {
      ToggleSelection();
      return output;
    }

    if (selection_.active) {
      HandleSelection(action, output);
      return output;
    }

    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (input_.Held(TrackerAction::Edit)) {
      if (input_.Held(TrackerAction::Alt) &&
          (action == TrackerAction::Alt ||
           action == TrackerAction::Edit)) {
        selection_.Begin(grid_.Column(), grid_.Row());
      } else if (direction != Ui2TrackerEditDirection::None) {
        SelectRelativeNumber(direction, output);
      } else if (input_.Held(TrackerAction::Enter) &&
                 action == TrackerAction::Enter) {
        output.Push(Command(Ui2TrackerCommandType::CutCell));
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
      } else if (action == TrackerAction::Enter &&
                 input_.Mask() == TrackerActionBit(TrackerAction::Enter)) {
        output.Push(Command(Ui2TrackerCommandType::PasteLast));
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

  constexpr void SelectRelativeNumber(Ui2TrackerEditDirection direction,
                                      Ui2TrackerCommandBatch<> &output) {
    std::int16_t delta = 0;
    switch (direction) {
    case Ui2TrackerEditDirection::Left:
      delta = -1;
      break;
    case Ui2TrackerEditDirection::Right:
      delta = 1;
      break;
    case Ui2TrackerEditDirection::Down:
      delta = -16;
      break;
    case Ui2TrackerEditDirection::Up:
      delta = 16;
      break;
    case Ui2TrackerEditDirection::None:
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
      output.Push(Command(Ui2TrackerCommandType::StartPlayback));
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
};

static_assert(std::is_trivially_copyable_v<Ui2TableController>);
static_assert(sizeof(Ui2TableController) <= 16U);

} // namespace ui2
