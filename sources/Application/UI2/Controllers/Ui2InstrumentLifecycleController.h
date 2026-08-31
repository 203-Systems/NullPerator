/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/I_Instrument.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2InstrumentLifecycleCommandType : std::uint8_t {
  None,
  ApplyType,
  OverwriteExport,
};

struct Ui2InstrumentLifecycleCommand {
  Ui2InstrumentLifecycleCommandType type =
      Ui2InstrumentLifecycleCommandType::None;
  InstrumentType instrumentType = IT_NONE;

  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2InstrumentLifecycleCommandType::None;
  }
};

// Fixed-capacity owner for the already-approved legacy Instrument type-change
// dialogs. The actual bank mutation remains at the application boundary and
// is emitted only for an immediate safe change or an explicit YES.
class Ui2InstrumentLifecycleController {
public:
  [[nodiscard]] constexpr bool Active() const {
    return purpose_ != Purpose::None;
  }
  [[nodiscard]] constexpr std::uint32_t InstanceId() const {
    return instanceId_;
  }

  Ui2InstrumentLifecycleCommand RequestTypeChange(
      InstrumentType requested, InstrumentType current,
      bool needsConfirmation, bool audioActive,
      TrackerAction trigger = TrackerAction::Count) {
    // NONE is a valid explicit target; values outside the type enum are not.
    if (requested < IT_NONE || requested >= IT_LAST)
      return {};
    if (requested == current)
      return {};
    if (audioActive) {
      Show(Purpose::PlayingBlocked, UiDialogAction::Ok, UiDialogAction::Ok,
           1U);
      BlockUntilRelease(trigger);
      return {};
    }
    if (!needsConfirmation)
      return {.type = Ui2InstrumentLifecycleCommandType::ApplyType,
              .instrumentType = requested};

    requested_ = requested;
    Show(Purpose::ConfirmTypeChange, UiDialogAction::Yes, UiDialogAction::No,
         2U);
    BlockUntilRelease(trigger);
    return {};
  }

  void RequestExportOverwrite(
      TrackerAction trigger = TrackerAction::Count) {
    Show(Purpose::ConfirmExportOverwrite, UiDialogAction::Yes,
         UiDialogAction::No, 2U);
    BlockUntilRelease(trigger);
  }

  Ui2InstrumentLifecycleCommand Handle(TrackerAction action, bool pressed) {
    if (!Active() || !input_.Update(action, pressed) ||
        !releaseGate_.Update(action, pressed) || !pressed)
      return {};
    if (action == TrackerAction::Left) {
      MoveSelection(-1);
      return {};
    }
    if (action == TrackerAction::Right) {
      MoveSelection(1);
      return {};
    }
    if (action != TrackerAction::Edit)
      return {};

    const Purpose purpose = purpose_;
    const UiDialogAction selected = actions_[selectedAction_];
    purpose_ = Purpose::None;
    input_ = {};
    releaseGate_.Reset();
    if (purpose == Purpose::ConfirmTypeChange &&
        selected == UiDialogAction::Yes) {
      return {.type = Ui2InstrumentLifecycleCommandType::ApplyType,
              .instrumentType = requested_};
    }
    if (purpose == Purpose::ConfirmExportOverwrite &&
        selected == UiDialogAction::Yes) {
      return {.type = Ui2InstrumentLifecycleCommandType::OverwriteExport};
    }
    return {};
  }

  [[nodiscard]] Ui2DialogSnapshot Snapshot() const {
    Ui2DialogSnapshot snapshot;
    snapshot.kind = UiDialogKind::Message;
    if (purpose_ == Purpose::PlayingBlocked) {
      snapshot.SetTitle("Not while playing");
    } else if (purpose_ == Purpose::ConfirmExportOverwrite) {
      snapshot.SetTitle("Overwrite existing file?");
    } else {
      snapshot.SetTitle("Change Instrument");
      snapshot.SetLabel("Lose settings?");
    }
    for (std::uint8_t index = 0U; index < actionCount_; ++index)
      snapshot.PushAction(actions_[index]);
    snapshot.SetSelectedAction(selectedAction_, true);
    return snapshot;
  }

private:
  enum class Purpose : std::uint8_t {
    None,
    PlayingBlocked,
    ConfirmTypeChange,
    ConfirmExportOverwrite,
  };

  void Show(Purpose purpose, UiDialogAction first, UiDialogAction second,
            std::uint8_t count) {
    purpose_ = purpose;
    actions_.fill(UiDialogAction::Ok);
    actions_[0] = first;
    actions_[1] = second;
    actionCount_ = count;
    // Legacy MessageBox selects its last action, so destructive type changes
    // open on NO and key repeat cannot discard settings.
    selectedAction_ = static_cast<std::uint8_t>(count - 1U);
    input_ = {};
    releaseGate_.Reset();
    ++instanceId_;
  }

  void BlockUntilRelease(TrackerAction action) {
    releaseGate_.BlockUntilRelease(action);
  }

  void MoveSelection(int delta) {
    if (actionCount_ <= 1U)
      return;
    const int count = actionCount_;
    selectedAction_ = static_cast<std::uint8_t>(
        (count + static_cast<int>(selectedAction_) + delta) % count);
  }

  Purpose purpose_ = Purpose::None;
  std::array<UiDialogAction, kUiDialogActionCapacity> actions_{};
  Ui2ControllerInputState input_{};
  Ui2InputReleaseGate releaseGate_{};
  std::uint32_t instanceId_ = 0U;
  InstrumentType requested_ = IT_NONE;
  std::uint8_t actionCount_ = 0U;
  std::uint8_t selectedAction_ = 0U;
};

static_assert(std::is_trivially_copyable_v<Ui2InstrumentLifecycleCommand>);
static_assert(std::is_trivially_copyable_v<Ui2InstrumentLifecycleController>);
static_assert(sizeof(Ui2InstrumentLifecycleController) <= 32U);

} // namespace ui2
