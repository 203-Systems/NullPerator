/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2TrackerGridController.h"

namespace ui2 {

class Ui2SongController {
public:
  static constexpr std::uint8_t SongRowCount = 128;
  static constexpr std::uint8_t MaximumRowOffset =
      SongRowCount - kUi2TrackerVisibleRows;

  constexpr Ui2SongController(std::uint8_t track = 0,
                              std::uint8_t visibleRow = 0,
                              std::uint8_t rowOffset = 0,
                              bool liveMode = false)
      : track_(Ui2ClampTrack(track)),
        visibleRow_(visibleRow < kUi2TrackerVisibleRows
                        ? visibleRow
                        : kUi2TrackerVisibleRows - 1U),
        rowOffset_(rowOffset <= MaximumRowOffset ? rowOffset
                                                : MaximumRowOffset),
        liveMode_(liveMode) {}

  [[nodiscard]] constexpr std::uint8_t Track() const { return track_; }
  [[nodiscard]] constexpr std::uint8_t VisibleRow() const {
    return visibleRow_;
  }
  [[nodiscard]] constexpr std::uint8_t RowOffset() const {
    return rowOffset_;
  }
  [[nodiscard]] constexpr std::uint8_t AbsoluteRow() const {
    return static_cast<std::uint8_t>(rowOffset_ + visibleRow_);
  }
  [[nodiscard]] constexpr bool LiveMode() const { return liveMode_; }
  [[nodiscard]] constexpr bool ClonePending() const { return clonePending_; }
  [[nodiscard]] constexpr std::uint16_t HeldMask() const {
    return input_.Mask();
  }
  [[nodiscard]] constexpr const Ui2GridSelectionState &Selection() const {
    return selection_;
  }

  constexpr Ui2TrackerCommandBatch<> Handle(TrackerAction action,
                                             bool pressed) {
    Ui2TrackerCommandBatch<> output;
    if (!input_.Update(action, pressed))
      return output;

    if (!pressed) {
      if (action == TrackerAction::Enter && valueEditDirty_) {
        output.Push(Command(Ui2TrackerCommandType::CommitValueEdits));
        valueEditDirty_ = false;
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
        selection_.Begin(track_, AbsoluteRow());
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
      } else if (direction == Ui2TrackerEditDirection::Up ||
                 direction == Ui2TrackerEditDirection::Down) {
        MovePage(direction == Ui2TrackerEditDirection::Up ? -16 : 16);
      } else if (direction == Ui2TrackerEditDirection::Left ||
                 direction == Ui2TrackerEditDirection::Right) {
        liveMode_ = !liveMode_;
        Ui2TrackerCommand command =
            Command(Ui2TrackerCommandType::SetLiveMode);
        command.flag = liveMode_;
        output.Push(command);
      } else if (input_.Held(TrackerAction::Enter) &&
                 action == TrackerAction::Enter) {
        output.Push(Command(Ui2TrackerCommandType::CutCell));
      } else if (input_.Held(TrackerAction::Nav) &&
                 action == TrackerAction::Nav) {
        output.Push(Command(Ui2TrackerCommandType::ToggleMute));
      } else if (action == TrackerAction::Play) {
        output.Push(Command(liveMode_ ? Ui2TrackerCommandType::StartImmediate
                                     : Ui2TrackerCommandType::OpenRecord));
      }
      return output;
    }

    if (input_.Held(TrackerAction::Enter)) {
      if (direction != Ui2TrackerEditDirection::None) {
        Ui2TrackerCommand command = Command(Ui2TrackerCommandType::AdjustCell);
        command.direction = direction;
        command.value = CellDelta(direction);
        output.Push(command);
        valueEditDirty_ = true;
      } else if (input_.Held(TrackerAction::Alt) &&
                 action == TrackerAction::Alt) {
        output.Push(Command(Ui2TrackerCommandType::PasteSelection));
      } else if (input_.Held(TrackerAction::Nav) &&
                 action == TrackerAction::Nav) {
        output.Push(Command(Ui2TrackerCommandType::ToggleSolo));
      } else if (action == TrackerAction::Enter &&
                 input_.Mask() == TrackerActionBit(TrackerAction::Enter)) {
        if (newEntryPending_) {
          output.Push(Command(Ui2TrackerCommandType::AllocateNext));
          newEntryPending_ = false;
        } else {
          output.Push(Command(Ui2TrackerCommandType::PasteLast));
          newEntryPending_ = true;
        }
      }
      return output;
    }

    if (input_.Held(TrackerAction::Nav)) {
      HandleNav(action, output);
      return output;
    }

    if (input_.Held(TrackerAction::Alt)) {
      HandleAlt(action, output);
      return output;
    }

    if (direction != Ui2TrackerEditDirection::None) {
      MoveCursor(direction);
    } else if (action == TrackerAction::Play) {
      output.Push(Command(Ui2TrackerCommandType::StartPlayback));
    }
    return output;
  }

private:
  [[nodiscard]] constexpr Ui2TrackerCommand
  Command(Ui2TrackerCommandType type) const {
    return Ui2MakeTrackerCommand(type, Ui2TrackerPage::Song, AbsoluteRow(),
                                 track_, track_);
  }

  [[nodiscard]] static constexpr std::int16_t
  CellDelta(Ui2TrackerEditDirection direction) {
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

  constexpr void ToggleSelection() {
    if (selection_.active)
      selection_.Clear();
    else
      selection_.Begin(track_, AbsoluteRow());
  }

  constexpr void MovePage(std::int16_t delta) {
    std::int16_t next = static_cast<std::int16_t>(rowOffset_) + delta;
    if (next < 0)
      next = 0;
    if (next > MaximumRowOffset)
      next = MaximumRowOffset;
    rowOffset_ = static_cast<std::uint8_t>(next);
  }

  constexpr void MoveCursor(Ui2TrackerEditDirection direction) {
    switch (direction) {
    case Ui2TrackerEditDirection::Left:
      if (track_ > 0U)
        --track_;
      break;
    case Ui2TrackerEditDirection::Right:
      if (track_ + 1U < kUi2TrackerTrackCount)
        ++track_;
      break;
    case Ui2TrackerEditDirection::Up:
      if (visibleRow_ > 0U)
        --visibleRow_;
      else if (rowOffset_ > 0U)
        --rowOffset_;
      break;
    case Ui2TrackerEditDirection::Down:
      if (visibleRow_ + 1U < kUi2TrackerVisibleRows)
        ++visibleRow_;
      else if (rowOffset_ < MaximumRowOffset)
        ++rowOffset_;
      break;
    case Ui2TrackerEditDirection::None:
      break;
    }
  }

  constexpr void HandleSelection(TrackerAction action,
                                 Ui2TrackerCommandBatch<> &output) {
    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (input_.Held(TrackerAction::Edit) &&
        input_.Held(TrackerAction::Alt)) {
      selection_.ExpandColumnsThenRows(
          kUi2TrackerTrackCount - 1U, rowOffset_,
          static_cast<std::uint8_t>(rowOffset_ +
                                    kUi2TrackerVisibleRows - 1U));
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
      command.value = CellDelta(direction);
      command.selection = selection_;
      output.Push(command);
      valueEditDirty_ = true;
      return;
    }
    if (!input_.AnyModifier() &&
        direction != Ui2TrackerEditDirection::None) {
      MoveCursor(direction);
      selection_.Follow(track_, AbsoluteRow());
    }
  }

  constexpr void HandleNav(TrackerAction action,
                           Ui2TrackerCommandBatch<> &output) const {
    if (input_.Held(TrackerAction::Alt))
      output.Push(Command(Ui2TrackerCommandType::UnmuteAll));

    Ui2TrackerPage target = Ui2TrackerPage::None;
    if (action == TrackerAction::Right)
      target = Ui2TrackerPage::Chain;
    else if (action == TrackerAction::Up)
      target = Ui2TrackerPage::Project;
    else if (action == TrackerAction::Down)
      target = Ui2TrackerPage::Mixer;
    else if (action == TrackerAction::Play) {
      output.Push(Command(Ui2TrackerCommandType::StopPlayback));
      return;
    } else {
      return;
    }
    Ui2TrackerCommand command = Command(Ui2TrackerCommandType::SwitchPage);
    command.targetPage = target;
    output.Push(command);
  }

  constexpr void HandleAlt(TrackerAction action,
                           Ui2TrackerCommandBatch<> &output) const {
    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (direction == Ui2TrackerEditDirection::Up ||
        direction == Ui2TrackerEditDirection::Down) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::JumpSection);
      command.value = direction == Ui2TrackerEditDirection::Up ? -1 : 1;
      output.Push(command);
    } else if (direction == Ui2TrackerEditDirection::Left ||
               direction == Ui2TrackerEditDirection::Right) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::NudgeTempo);
      command.value = direction == Ui2TrackerEditDirection::Left ? -1 : 1;
      output.Push(command);
    } else if (action == TrackerAction::Play) {
      output.Push(Command(Ui2TrackerCommandType::StartPlayback));
    }
  }

  Ui2HeldActionState input_{};
  Ui2GridSelectionState selection_{};
  std::uint8_t track_ = 0;
  std::uint8_t visibleRow_ = 0;
  std::uint8_t rowOffset_ = 0;
  bool liveMode_ = false;
  bool newEntryPending_ = false;
  bool valueEditDirty_ = false;
  bool clonePending_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2SongController>);
static_assert(sizeof(Ui2SongController) <= 16U);

} // namespace ui2
