/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Persistency/PersistenceConstants.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace ui2 {

enum class Ui2ProjectLifecycleCommandType : std::uint8_t {
  None,
  NewProject,
  LoadProject,
  DeleteProject,
  OverwriteProject,
  OverwriteTheme,
  PurgeUnusedSamples,
  PurgeUnusedInstruments,
};

struct Ui2ProjectLifecycleCommand {
  Ui2ProjectLifecycleCommandType type = Ui2ProjectLifecycleCommandType::None;
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> project{};

  [[nodiscard]] bool HasValue() const {
    return type != Ui2ProjectLifecycleCommandType::None;
  }
};

enum class Ui2ProjectLifecycleFailure : std::uint8_t {
  OpenProjectBrowser,
  NewProject,
  LoadProject,
  SaveProject,
  DeleteProject,
  RefreshBrowserAfterDelete,
  SaveTheme,
};

// Application-owned Message Box lifecycle for project-destructive actions.
// The strings, button sets and conservative default selections intentionally
// mirror the established legacy MessageBox flows; this controller only adapts
// those already-designed states to TrackerAction and Ui2DialogSnapshot.
class Ui2ProjectLifecycleController {
public:
  [[nodiscard]] bool Active() const { return purpose_ != Purpose::None; }
  [[nodiscard]] std::uint32_t InstanceId() const { return instanceId_; }

  Ui2ProjectLifecycleCommand
  RequestNew(bool dirty, bool playerRunning,
             TrackerAction trigger = TrackerAction::Count) {
    if (playerRunning) {
      ShowInfo("Not while running!", nullptr, false, trigger);
      return {};
    }
    if (!dirty)
      return {.type = Ui2ProjectLifecycleCommandType::NewProject};
    project_.fill('\0');
    Show(Purpose::ConfirmNew, "Create a new project and",
         "   lose all changes?", UiDialogAction::Yes, UiDialogAction::No, 2U,
         false, trigger);
    return {};
  }

  Ui2ProjectLifecycleCommand RequestLoad(const char *project, bool dirty,
                                         bool playerRunning,
                                         TrackerAction trigger =
                                             TrackerAction::Count) {
    if (playerRunning) {
      ShowInfo("Not while running!", nullptr, false, trigger);
      return {};
    }
    if (!CopyProject(project))
      return {};
    if (!dirty)
      return Command(Ui2ProjectLifecycleCommandType::LoadProject);
    Show(Purpose::ConfirmLoad, "Load song and lose changes?", {},
         UiDialogAction::Yes, UiDialogAction::No, 2U, false, trigger);
    return {};
  }

  Ui2ProjectLifecycleCommand RequestDelete(const char *project,
                                           const char *currentProject,
                                           bool playerRunning,
                                           TrackerAction trigger =
                                               TrackerAction::Count) {
    if (playerRunning) {
      ShowInfo("Not while running!", nullptr, false, trigger);
      return {};
    }
    if (!CopyProject(project))
      return {};
    if (currentProject != nullptr &&
        std::strcmp(project_.data(), currentProject) == 0) {
      ShowInfo("Cannot delete the active", "project.", false, trigger);
      return {};
    }
    Show(Purpose::ConfirmDelete, "Delete selected project?", project_.data(),
         UiDialogAction::Yes, UiDialogAction::No, 2U, true, trigger);
    return {};
  }

  void RequestOverwrite(
      const char *project, TrackerAction trigger = TrackerAction::Count) {
    if (!CopyProject(project))
      return;
    Show(Purpose::ConfirmOverwrite, "Overwrite EXISTING project?", {},
         UiDialogAction::Ok, UiDialogAction::Cancel, 2U, false, trigger);
  }

  void RequestThemeOverwrite(
      const char *theme, TrackerAction trigger = TrackerAction::Count) {
    if (!CopyProject(theme))
      return;
    Show(Purpose::ConfirmThemeOverwrite, "Theme already exists",
         "Overwrite?", UiDialogAction::Yes, UiDialogAction::No, 2U, false,
         trigger);
  }

  void RequestPurgeUnusedSamples(
      bool audioActive, TrackerAction trigger = TrackerAction::Count) {
    RequestPurge(Purpose::ConfirmPurgeSamples, "Remove unused samples?",
                 audioActive, trigger);
  }

  void RequestPurgeUnusedInstruments(
      bool audioActive, TrackerAction trigger = TrackerAction::Count) {
    RequestPurge(Purpose::ConfirmPurgeInstruments,
                 "Remove unused instruments?", audioActive, trigger);
  }

  void WarnPendingRename() { ShowInfo("Save project rename first"); }

  // Shared legacy guard used by non-project-file operations such as opening
  // the Sample Pool while the sequencer owns sample resources.
  void ReportRunningBlocked() { ShowInfo("Not while running!"); }

  void ReportFailure(Ui2ProjectLifecycleFailure failure,
                     const char *project = nullptr) {
    switch (failure) {
    case Ui2ProjectLifecycleFailure::OpenProjectBrowser:
      ShowInfo("Project browser unavailable");
      break;
    case Ui2ProjectLifecycleFailure::NewProject:
      // There is no separate legacy New failure message. Reuse the existing
      // project-persist failure state instead of inventing a new UI state.
      ShowInfo("Error saving Project");
      break;
    case Ui2ProjectLifecycleFailure::LoadProject:
      ShowInfo("Invalid Project:", project, true);
      break;
    case Ui2ProjectLifecycleFailure::SaveProject:
      ShowInfo("Error saving Project");
      break;
    case Ui2ProjectLifecycleFailure::DeleteProject:
      ShowInfo("Project could not be deleted");
      break;
    case Ui2ProjectLifecycleFailure::RefreshBrowserAfterDelete:
      ShowInfo("Project deleted;", "browser refresh failed");
      break;
    case Ui2ProjectLifecycleFailure::SaveTheme:
      ShowInfo("Failed to export theme");
      break;
    }
  }

  Ui2ProjectLifecycleCommand Handle(TrackerAction action, bool pressed) {
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
    if (action != TrackerAction::Enter)
      return {};

    const UiDialogAction chosen = actions_[selectedAction_];
    const Purpose purpose = purpose_;
    purpose_ = Purpose::None;
    input_ = {};
    releaseGate_.Reset();
    if (chosen == UiDialogAction::No || chosen == UiDialogAction::Cancel ||
        purpose == Purpose::Info)
      return {};
    switch (purpose) {
    case Purpose::ConfirmNew:
      return {.type = Ui2ProjectLifecycleCommandType::NewProject};
    case Purpose::ConfirmLoad:
      return Command(Ui2ProjectLifecycleCommandType::LoadProject);
    case Purpose::ConfirmDelete:
      return Command(Ui2ProjectLifecycleCommandType::DeleteProject);
    case Purpose::ConfirmOverwrite:
      return Command(Ui2ProjectLifecycleCommandType::OverwriteProject);
    case Purpose::ConfirmThemeOverwrite:
      return Command(Ui2ProjectLifecycleCommandType::OverwriteTheme);
    case Purpose::ConfirmPurgeSamples:
      return {.type = Ui2ProjectLifecycleCommandType::PurgeUnusedSamples};
    case Purpose::ConfirmPurgeInstruments:
      return {.type =
                  Ui2ProjectLifecycleCommandType::PurgeUnusedInstruments};
    case Purpose::None:
    case Purpose::Info:
      return {};
    }
    return {};
  }

  [[nodiscard]] Ui2DialogSnapshot Snapshot() const {
    Ui2DialogSnapshot snapshot;
    snapshot.kind = UiDialogKind::Message;
    snapshot.SetTitle(line1_.data());
    if (line2UserText_)
      snapshot.SetUserLabel(line2_.data());
    else
      snapshot.SetLabel(line2_.data());
    for (std::uint8_t index = 0U; index < actionCount_; ++index)
      snapshot.PushAction(actions_[index]);
    snapshot.SetSelectedAction(selectedAction_, true);
    return snapshot;
  }

private:
  enum class Purpose : std::uint8_t {
    None,
    Info,
    ConfirmNew,
    ConfirmLoad,
    ConfirmDelete,
    ConfirmOverwrite,
    ConfirmThemeOverwrite,
    ConfirmPurgeSamples,
    ConfirmPurgeInstruments,
  };

  template <std::size_t Size>
  static void CopyText(std::array<char, Size> &destination,
                       const char *source) {
    destination.fill('\0');
    if (source != nullptr)
      std::snprintf(destination.data(), destination.size(), "%s", source);
  }

  bool CopyProject(const char *project) {
    CopyText(project_, project);
    return project_[0] != '\0';
  }

  void RequestPurge(Purpose purpose, const char *prompt,
                    bool audioActive, TrackerAction trigger) {
    // Legacy exposed these actions while playback was active, even though
    // they can release live instruments or delete sample files. UI2 keeps the
    // established prompt when idle and reuses its existing running guard so
    // confirmation can never mutate resources currently owned by Player.
    if (audioActive) {
      ShowInfo("Not while running!", nullptr, false, trigger);
      return;
    }
    Show(purpose, prompt, {}, UiDialogAction::Yes, UiDialogAction::No, 2U,
         false, trigger);
  }

  void ShowInfo(const char *line1, const char *line2 = nullptr,
                bool line2UserText = false,
                TrackerAction trigger = TrackerAction::Count) {
    Show(Purpose::Info, line1, line2, UiDialogAction::Ok, UiDialogAction::Ok,
         1U, line2UserText, trigger);
  }

  void Show(Purpose purpose, const char *line1, const char *line2,
            UiDialogAction first, UiDialogAction second,
            std::uint8_t count = 2U, bool line2UserText = false,
            TrackerAction trigger = TrackerAction::Count) {
    purpose_ = purpose;
    CopyText(line1_, line1);
    CopyText(line2_, line2);
    line2UserText_ = line2UserText;
    actions_.fill(UiDialogAction::Ok);
    actions_[0] = first;
    actions_[1] = second;
    actionCount_ = std::min<std::uint8_t>(count, actions_.size());
    // MessageBox has always selected the last button. For destructive
    // confirmations that is NO/CANCEL, so key repeat cannot accept data loss.
    selectedAction_ = static_cast<std::uint8_t>(actionCount_ - 1U);
    input_ = {};
    releaseGate_.BlockUntilRelease(trigger);
    ++instanceId_;
  }

  void MoveSelection(int delta) {
    if (actionCount_ <= 1U)
      return;
    const int count = actionCount_;
    selectedAction_ = static_cast<std::uint8_t>(
        (count + static_cast<int>(selectedAction_) + delta) % count);
  }

  Ui2ProjectLifecycleCommand
  Command(Ui2ProjectLifecycleCommandType type) const {
    Ui2ProjectLifecycleCommand command{.type = type};
    command.project = project_;
    return command;
  }

  Purpose purpose_ = Purpose::None;
  std::array<char, Ui2DialogSnapshot::TextCapacity> line1_{};
  std::array<char, Ui2DialogSnapshot::TextCapacity> line2_{};
  std::array<UiDialogAction, kUiDialogActionCapacity> actions_{};
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> project_{};
  Ui2ControllerInputState input_{};
  Ui2InputReleaseGate releaseGate_{};
  std::uint32_t instanceId_ = 0U;
  std::uint8_t actionCount_ = 0U;
  std::uint8_t selectedAction_ = 0U;
  bool line2UserText_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2ProjectLifecycleCommand>);
static_assert(std::is_trivially_copyable_v<Ui2ProjectLifecycleController>);

} // namespace ui2
