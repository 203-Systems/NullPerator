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
                              std::uint8_t rowOffset = 0, bool liveMode = false)
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
  [[nodiscard]] constexpr std::uint8_t RowOffset() const { return rowOffset_; }
  [[nodiscard]] constexpr std::uint8_t AbsoluteRow() const {
    return static_cast<std::uint8_t>(rowOffset_ + visibleRow_);
  }
  [[nodiscard]] constexpr bool LiveMode() const { return liveMode_; }
  [[nodiscard]] constexpr bool ClonePending() const {
    return clonePending_ && input_.Held(TrackerAction::Shift);
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
  [[nodiscard]] constexpr const Ui2GridSelectionState &Selection() const {
    return selection_;
  }

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
      if (action == TrackerAction::Edit && wasHeld) {
        const bool committedValueEdits = valueEditDirty_;
        if (committedValueEdits) {
          output.Push(Command(Ui2TrackerCommandType::CommitValueEdits));
          valueEditDirty_ = false;
        } else if (liveMode_ && !editChordConsumed_) {
          HandlePrimaryEdit(output);
        }
        editChordConsumed_ = false;
      }
      return output;
    }

    if (action != TrackerAction::Edit)
      newEntryPending_ = false;

    if (action == TrackerAction::Play && input_.Held(TrackerAction::Option) &&
        !input_.Held(TrackerAction::Edit)) {
      clonePending_ = false;
      Ui2TrackerCommand command =
          Command(input_.Held(TrackerAction::Shift)
                      ? Ui2TrackerCommandType::UnmuteAll
                      : Ui2TrackerCommandType::ToggleSolo);
      // Song solo follows the horizontal extent of the current selection.
      // Carry it across the controller/model boundary; the model port owns the
      // saved mute mask needed to restore the exact pre-solo state.
      command.selection = selection_;
      output.Push(command);
      return output;
    }

    if (clonePending_ && action == TrackerAction::Edit &&
        input_.Held(TrackerAction::Shift) &&
        !input_.Held(TrackerAction::Option)) {
      clonePending_ = false;
      editChordConsumed_ = true;
      selection_.Clear();
      output.Push(Command(Ui2TrackerCommandType::CloneCell));
      return output;
    }
    if (action == TrackerAction::Play && liveMode_ &&
        input_.Held(TrackerAction::Left) &&
        !input_.Held(TrackerAction::Shift) &&
        !input_.Held(TrackerAction::Edit)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::StartPlayback);
      // M8's LEFT+PLAY chord cues the selected Song row across every track.
      // LEFT keeps its existing press-edge cursor/selection motion; the PLAY
      // edge contributes only this temporary transport range.
      command.selection.Begin(0U, AbsoluteRow());
      command.selection.Follow(kUi2TrackerTrackCount - 1U, AbsoluteRow());
      output.Push(command);
      return output;
    }
    if (selection_.active) {
      HandleSelection(action, output);
      return output;
    }

    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    // M8 distinguishes modifier order: SHIFT then OPTION begins selection;
    // OPTION then SHIFT toggles mute.
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Shift)) {
      selection_.Begin(track_, AbsoluteRow());
      clonePending_ = true;
      return output;
    }
    if (action == TrackerAction::Shift && input_.Held(TrackerAction::Option)) {
      output.Push(Command(Ui2TrackerCommandType::ToggleMute));
      return output;
    }
    if (input_.Held(TrackerAction::Option)) {
      if (action == TrackerAction::Option && input_.Held(TrackerAction::Edit)) {
        output.Push(Command(Ui2TrackerCommandType::CutCell));
        editChordConsumed_ = true;
      } else if (input_.Held(TrackerAction::Shift) ||
                 input_.Held(TrackerAction::Edit)) {
        return output;
      } else if (direction == Ui2TrackerEditDirection::Up ||
                 direction == Ui2TrackerEditDirection::Down) {
        Ui2TrackerCommand command = Command(Ui2TrackerCommandType::JumpSection);
        command.direction = direction;
        command.value = direction == Ui2TrackerEditDirection::Up ? -1 : 1;
        output.Push(command);
      } else if (direction == Ui2TrackerEditDirection::Left ||
                 direction == Ui2TrackerEditDirection::Right) {
        // SONG/LIVE is a two-item wrapping selector. Either horizontal
        // direction advances to the other item, including across either edge.
        liveMode_ = !liveMode_;
        Ui2TrackerCommand command = Command(Ui2TrackerCommandType::SetLiveMode);
        command.flag = liveMode_;
        output.Push(command);
      }
      return output;
    }

    if (action == TrackerAction::Edit && input_.Held(TrackerAction::Shift)) {
      editChordConsumed_ = true;
      output.Push(Command(Ui2TrackerCommandType::PasteSelection));
      return output;
    }

    if (input_.Held(TrackerAction::Edit)) {
      if (action == TrackerAction::Play && liveMode_ &&
          !input_.Held(TrackerAction::Shift)) {
        output.Push(Command(Ui2TrackerCommandType::StartImmediate));
        editChordConsumed_ = true;
      } else if (direction != Ui2TrackerEditDirection::None) {
        Ui2TrackerCommand command = Command(Ui2TrackerCommandType::AdjustCell);
        command.direction = direction;
        command.value = CellDelta(direction);
        output.Push(command);
        valueEditDirty_ = true;
        editChordConsumed_ = true;
      } else if (action == TrackerAction::Edit &&
                 input_.Mask() == TrackerActionBit(TrackerAction::Edit)) {
        editChordConsumed_ = false;
        if (!liveMode_)
          HandlePrimaryEdit(output);
      }
      return output;
    }

    if (input_.Held(TrackerAction::Shift)) {
      HandleNav(action, output);
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
  constexpr void HandlePrimaryEdit(Ui2TrackerCommandBatch<> &output) {
    if (newEntryPending_) {
      output.Push(Command(Ui2TrackerCommandType::AllocateNext));
      newEntryPending_ = false;
    } else {
      output.Push(Command(Ui2TrackerCommandType::PasteLast));
      newEntryPending_ = true;
    }
  }

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

  constexpr bool MoveCursor(Ui2TrackerEditDirection direction) {
    const std::uint8_t previousTrack = track_;
    const std::uint8_t previousRow = AbsoluteRow();
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
    return track_ != previousTrack || AbsoluteRow() != previousRow;
  }

  constexpr void HandleSelection(TrackerAction action,
                                 Ui2TrackerCommandBatch<> &output) {
    // Clone is a strict next-action gesture. Once selection handles anything
    // other than the matching EDIT path above, the pending clone is canceled.
    clonePending_ = false;
    // LIVE defers a plain EDIT until release so EDIT+PLAY can be recognized.
    // While a selection owns EDIT, consume that deferred cell edit even when
    // no selection adjustment follows; releasing EDIT must not paste a cell.
    if (action == TrackerAction::Edit)
      editChordConsumed_ = true;
    const Ui2TrackerEditDirection direction = Ui2TrackerDirectionFor(action);
    if (action == TrackerAction::Shift &&
        input_.Held(TrackerAction::Option)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::ToggleMute);
      command.selection = selection_;
      output.Push(command);
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
      selection_.ExpandColumnsThenRows(
          kUi2TrackerTrackCount - 1U, rowOffset_,
          static_cast<std::uint8_t>(rowOffset_ + kUi2TrackerVisibleRows - 1U));
      return;
    }
    if (action == TrackerAction::Option && input_.Held(TrackerAction::Edit)) {
      Ui2TrackerCommand command = Command(Ui2TrackerCommandType::CutSelection);
      command.selection = selection_;
      output.Push(command);
      editChordConsumed_ = true;
      selection_.Clear();
      return;
    }
    if (action == TrackerAction::Option &&
        input_.Mask() == TrackerActionBit(TrackerAction::Option)) {
      // OPTION is also the prefix of OPTION+SHIFT mute. Defer a plain copy
      // until release so modifier order remains observable.
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
      const std::uint8_t previousTrack = track_;
      const std::uint8_t previousVisibleRow = visibleRow_;
      const std::uint8_t previousRowOffset = rowOffset_;
      if (MoveCursor(direction)) {
        const std::uint8_t nextRow = AbsoluteRow();
        const std::uint8_t top =
            nextRow < selection_.anchorRow ? nextRow : selection_.anchorRow;
        const std::uint8_t bottom =
            nextRow > selection_.anchorRow ? nextRow : selection_.anchorRow;
        if (bottom - top < kUi2TrackerVisibleRows) {
          selection_.Follow(track_, nextRow);
        } else {
          track_ = previousTrack;
          visibleRow_ = previousVisibleRow;
          rowOffset_ = previousRowOffset;
        }
      }
    }
  }

  constexpr void HandleNav(TrackerAction action,
                           Ui2TrackerCommandBatch<> &output) const {
    Ui2TrackerPage target = Ui2TrackerPage::None;
    if (action == TrackerAction::Right)
      target = Ui2TrackerPage::Chain;
    else if (action == TrackerAction::Up)
      target = Ui2TrackerPage::Project;
    else if (action == TrackerAction::Down)
      target = Ui2TrackerPage::Mixer;
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

  Ui2ControllerInputState input_{};
  Ui2GridSelectionState selection_{};
  std::uint8_t track_ = 0;
  std::uint8_t visibleRow_ = 0;
  std::uint8_t rowOffset_ = 0;
  bool liveMode_ = false;
  bool newEntryPending_ = false;
  bool valueEditDirty_ = false;
  bool clonePending_ = false;
  bool editChordConsumed_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2SongController>);
static_assert(sizeof(Ui2SongController) <= 16U);

} // namespace ui2
