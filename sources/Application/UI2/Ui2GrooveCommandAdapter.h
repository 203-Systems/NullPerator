/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2GrooveController.h"
#include "Application/UI2/Controllers/Ui2TrackerGridController.h"

namespace ui2 {

// Groove is outside the tracker-grid controller hub, but its transport and
// performance commands must share the same Player-facing model port.
[[nodiscard]] constexpr Ui2TrackerCommand
Ui2GrooveTrackerCommand(Ui2GrooveCommand command, int track) {
  Ui2TrackerCommand trackerCommand = Ui2MakeTrackerCommand(
      Ui2TrackerCommandType::None, Ui2TrackerPage::Groove, command.row, 0U,
      Ui2ClampTrack(track));
  switch (command.type) {
  case Ui2GrooveCommandType::StartPlayback:
    trackerCommand.type = Ui2TrackerCommandType::StartPlayback;
    trackerCommand.flag = command.songTransport;
    break;
  case Ui2GrooveCommandType::ToggleSolo:
    trackerCommand.type = Ui2TrackerCommandType::ToggleSolo;
    break;
  case Ui2GrooveCommandType::UnmuteAll:
    trackerCommand.type = Ui2TrackerCommandType::UnmuteAll;
    break;
  case Ui2GrooveCommandType::None:
  case Ui2GrooveCommandType::InitializeStep:
  case Ui2GrooveCommandType::ClearStep:
  case Ui2GrooveCommandType::AdjustStep:
  case Ui2GrooveCommandType::CopySelection:
  case Ui2GrooveCommandType::CutSelection:
  case Ui2GrooveCommandType::PasteSelection:
  case Ui2GrooveCommandType::InterpolateSelection:
  case Ui2GrooveCommandType::SelectNumber:
    break;
  }
  return trackerCommand;
}

} // namespace ui2
