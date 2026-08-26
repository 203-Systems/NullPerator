/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/ITrackerInputSink.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/UI2/Ui2SamplePathPolicy.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"
#include "Application/Views/Ui2BrowserSnapshot.h"
#include "System/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace ui2 {

enum class Ui2SampleBrowserMode : std::uint8_t { ProjectPool, Library };

enum class Ui2SampleBrowserCommandType : std::uint8_t {
  None,
  PreviewStart,
  PreviewStop,
  Import,
  Edit,
  RequestDelete,
  DeleteConfirmed,
  ModeChanged,
  // The approved Sample Browser shows the preview volume in its footer, but
  // has no approved volume focus/control. Keep the mutation typed so a later
  // input design can connect it without changing the application boundary.
  AdjustPreviewVolume,
};

struct Ui2SampleBrowserCommand {
  Ui2SampleBrowserCommandType type = Ui2SampleBrowserCommandType::None;
  std::array<char, PFILENAME_SIZE> filename{};
  std::int8_t delta = 0;
  bool projectSample = false;
  bool singleCycle = false;

  [[nodiscard]] bool HasValue() const {
    return type != Ui2SampleBrowserCommandType::None;
  }
};

// Fixed-capacity native Sample Pool / Import browser. File names are copied
// only into command/snapshot packets; the controller retains stable listing
// indices and a bounded directory-selection stack suitable for ESP32 builds.
class Ui2SampleBrowserController {
public:
  static constexpr std::uint8_t DirectoryDepthCapacity = 32U;
  using SampleUseQuery = bool (*)(void *context, const char *filename);

  bool Open(const char *projectName, std::uint8_t instrument) {
    Close();
    if (projectName == nullptr || projectName[0] == '\0')
      return false;
    std::snprintf(projectName_.data(), projectName_.size(), "%s", projectName);
    instrument_ = instrument;
    active_ = true;
    mode_ = Ui2SampleBrowserMode::ProjectPool;
    if (!JumpToModeRoot()) {
      Close();
      return false;
    }
    return true;
  }

  void Close() {
    active_ = false;
    previewHeld_ = false;
    toggleChordLatched_ = false;
    heldMask_ = 0U;
    dialogInput_ = {};
    dialogActive_ = false;
    count_ = selected_ = top_ = 0U;
    depth_ = 0U;
    selectedAction_ = 0U;
    projectName_.fill('\0');
    pendingDelete_.fill('\0');
    error_.fill('\0');
  }

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] Ui2SampleBrowserMode Mode() const { return mode_; }
  [[nodiscard]] bool DialogActive() const { return dialogActive_; }
  [[nodiscard]] std::uint32_t DialogInstanceId() const {
    return dialogInstanceId_;
  }

  Ui2SampleBrowserCommand Handle(TrackerAction action, bool pressed) {
    if (!active_ || dialogActive_)
      return {};

    const std::uint16_t bit = TrackerActionBit(action);
    if (pressed) {
      if ((heldMask_ & bit) != 0U)
        return {};
      heldMask_ |= bit;
    } else {
      heldMask_ &= static_cast<std::uint16_t>(~bit);
      if (action == TrackerAction::Play && previewHeld_) {
        previewHeld_ = false;
        return {.type = Ui2SampleBrowserCommandType::PreviewStop};
      }
      if ((heldMask_ & ToggleChordMask()) != ToggleChordMask())
        toggleChordLatched_ = false;
      return {};
    }

    const bool toggleChord =
        (heldMask_ & ToggleChordMask()) == ToggleChordMask();
    if (toggleChord && !toggleChordLatched_) {
      toggleChordLatched_ = true;
      mode_ = mode_ == Ui2SampleBrowserMode::ProjectPool
                  ? Ui2SampleBrowserMode::Library
                  : Ui2SampleBrowserMode::ProjectPool;
      if (!JumpToModeRoot()) {
        mode_ = mode_ == Ui2SampleBrowserMode::ProjectPool
                    ? Ui2SampleBrowserMode::Library
                    : Ui2SampleBrowserMode::ProjectPool;
        JumpToModeRoot();
        SetError("BROWSER UNAVAILABLE");
      }
      return {.type = Ui2SampleBrowserCommandType::ModeChanged};
    }

    if (action == TrackerAction::Play) {
      if (!HasFileSelection())
        return {};
      if ((heldMask_ & TrackerActionBit(TrackerAction::Shift)) != 0U &&
          mode_ == Ui2SampleBrowserMode::Library)
        return MakeSelected(Ui2SampleBrowserCommandType::Import);
      Ui2SampleBrowserCommand command =
          MakeSelected(Ui2SampleBrowserCommandType::PreviewStart);
      command.singleCycle = IsSelectedSingleCycle();
      previewHeld_ = command.filename[0] != '\0';
      return command;
    }

    // Legacy OPTION+LEFT walked to the parent directory without leaving the
    // browser. SHIFT+LEFT remains the application-level return chord.
    if (mode_ == Ui2SampleBrowserMode::Library &&
        action == TrackerAction::Left &&
        (heldMask_ & TrackerActionBit(TrackerAction::Option)) != 0U) {
      NavigateParent();
      return {};
    }

    if ((heldMask_ & (TrackerActionBit(TrackerAction::Shift) |
                      TrackerActionBit(TrackerAction::Option))) != 0U)
      return {};

    if (action == TrackerAction::Up) {
      if (selected_ > 0U)
        --selected_;
      SelectionChanged();
    } else if (action == TrackerAction::Down) {
      if (selected_ + 1U < count_)
        ++selected_;
      SelectionChanged();
    } else if (action == TrackerAction::Left) {
      MoveAction(-1);
    } else if (action == TrackerAction::Right) {
      MoveAction(1);
    } else if (action == TrackerAction::Edit) {
      return ActivateSelection();
    }
    return {};
  }

  void RequestDeleteConfirmation(const char *filename) {
    pendingDelete_.fill('\0');
    if (filename == nullptr || filename[0] == '\0')
      return;
    std::snprintf(pendingDelete_.data(), pendingDelete_.size(), "%s",
                  filename);
    dialogActive_ = true;
    dialogSelectedAction_ = 1U; // NO is the conservative legacy default.
    dialogInput_ = {};
    ++dialogInstanceId_;
  }

  Ui2SampleBrowserCommand HandleDialog(TrackerAction action, bool pressed) {
    if (!dialogActive_ || !dialogInput_.Update(action, pressed) || !pressed)
      return {};
    if (action == TrackerAction::Left || action == TrackerAction::Right) {
      dialogSelectedAction_ = static_cast<std::uint8_t>(
          1U - std::min<std::uint8_t>(dialogSelectedAction_, 1U));
      return {};
    }
    if (action != TrackerAction::Edit)
      return {};
    const bool confirmed = dialogSelectedAction_ == 0U;
    dialogActive_ = false;
    dialogInput_ = {};
    if (!confirmed) {
      pendingDelete_.fill('\0');
      return {};
    }
    Ui2SampleBrowserCommand command{
        .type = Ui2SampleBrowserCommandType::DeleteConfirmed,
        .projectSample = true};
    command.filename = pendingDelete_;
    pendingDelete_.fill('\0');
    return command;
  }

  [[nodiscard]] Ui2DialogSnapshot DialogSnapshot() const {
    Ui2DialogSnapshot snapshot;
    snapshot.kind = UiDialogKind::Message;
    snapshot.SetTitle("Remove sample?");
    snapshot.SetLabel(pendingDelete_.data());
    snapshot.PushAction(UiDialogAction::Yes);
    snapshot.PushAction(UiDialogAction::No);
    snapshot.SetSelectedAction(dialogSelectedAction_, true);
    return snapshot;
  }

  void SetError(const char *message) {
    error_.fill('\0');
    if (message != nullptr)
      std::snprintf(error_.data(), error_.size(), "%s", message);
  }

  void ClearError() { error_.fill('\0'); }

  bool RefreshCurrentDirectory() {
    count_ = 0U;
    selected_ = top_ = 0U;
    selectedAction_ = 0U;
    ClearError();
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr)
      return false;
    etl::vector<int, MAX_FILE_INDEX_SIZE> listed;
    fileSystem->list(&listed, ".wav", false);
    for (const int fileIndex : listed) {
      if (count_ >= indices_.size())
        break;
      char name[PFILENAME_SIZE]{};
      fileSystem->getFileName(fileIndex, name, sizeof(name));
      name[sizeof(name) - 1U] = '\0';
      if (name[0] == '\0' || std::strcmp(name, ".") == 0)
        continue;
      // SamplePool::Load() has a flat, files-only contract. Directory entries
      // must never become selectable in ProjectPool: after entering one, the
      // old leaf-only command packet could resolve against the pool root and
      // edit/delete a different same-named sample. Library remains recursive.
      if (mode_ == Ui2SampleBrowserMode::ProjectPool &&
          (fileSystem->getFileType(fileIndex) == PFT_DIR ||
           !Ui2IsFlatProjectSampleLeaf(name)))
        continue;
      indices_[count_++] = fileIndex;
    }
    return true;
  }

  [[nodiscard]] Ui2BrowserSnapshot
  Snapshot(int previewVolume, SampleUseQuery sampleInUse = nullptr,
           void *sampleUseContext = nullptr) const {
    Ui2BrowserSnapshot snapshot;
    Ui2BrowserSnapshot::CopyText(
        snapshot.title,
        mode_ == Ui2SampleBrowserMode::ProjectPool ? "SAMPLES" : "IMPORT");
    std::snprintf(snapshot.meta.data(), snapshot.meta.size(), "%02X",
                  static_cast<unsigned>(instrument_));
    snapshot.ConfigureWindow(count_, selected_, top_);
    FileSystem *fileSystem = FileSystem::GetInstance();
    for (std::uint8_t row = 0U; row < snapshot.visibleItemCount; ++row) {
      const std::uint16_t index =
          static_cast<std::uint16_t>(snapshot.topIndex + row);
      char name[PFILENAME_SIZE]{};
      ReadName(index, name, sizeof(name));
      char display[Ui2BrowserSnapshot::ItemTextCapacity]{};
      if (fileSystem != nullptr && index < count_ &&
          fileSystem->getFileType(indices_[index]) == PFT_DIR) {
        std::snprintf(display, sizeof(display), "%s%s",
                      std::strcmp(name, "..") == 0 ? "" : "/", name);
      } else {
        const bool used = mode_ == Ui2SampleBrowserMode::ProjectPool &&
                          sampleInUse != nullptr &&
                          sampleInUse(sampleUseContext, name);
        std::snprintf(display, sizeof(display), "%s%s",
                      used ? "*" : IsSingleCycleSize(SelectedSize(index)) ? "~"
                                                                            : "",
                      name);
      }
      Ui2BrowserSnapshot::CopyText(snapshot.items[row], display);
    }

    if (error_[0] != '\0') {
      Ui2BrowserSnapshot::CopyText(snapshot.footer, error_.data());
    } else if (snapshot.hasSelection) {
      const std::uint64_t bytes = SelectedSize(selected_);
      const unsigned kb = static_cast<unsigned>((bytes + 1023U) / 1024U);
      std::snprintf(snapshot.footer.data(), snapshot.footer.size(),
                    "%u KB  /  %d", kb,
                    std::clamp(previewVolume, 0, 99));
    } else {
      std::snprintf(snapshot.footer.data(), snapshot.footer.size(), "0 ITEMS");
    }

    if (!snapshot.hasSelection || fileSystem == nullptr)
      return snapshot;
    if (IsSelectedDirectory()) {
      Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "OPEN");
      snapshot.actionCount = 1U;
      return snapshot;
    }
    if (mode_ == Ui2SampleBrowserMode::ProjectPool) {
      // Matches the approved 240x240 Sample Pool bottom bar exactly.
      Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "EDIT");
      Ui2BrowserSnapshot::CopyText(snapshot.actions[1], "IMPORT");
      Ui2BrowserSnapshot::CopyText(snapshot.actions[2], "DELETE");
      snapshot.actionCount = 3U;
    } else {
      Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "IMPORT");
      Ui2BrowserSnapshot::CopyText(snapshot.actions[1], "EDIT");
      snapshot.actionCount = 2U;
    }
    snapshot.activeAction = std::min<std::uint8_t>(
        selectedAction_, static_cast<std::uint8_t>(snapshot.actionCount - 1U));
    return snapshot;
  }

private:
  [[nodiscard]] static constexpr std::uint16_t ToggleChordMask() {
    return TrackerActionBit(TrackerAction::Shift) |
           TrackerActionBit(TrackerAction::Option);
  }

  [[nodiscard]] static bool IsSingleCycleSize(std::uint64_t size) {
    return size == 1376U || size == 1344U || size == 300U;
  }

  [[nodiscard]] bool IsSelectedSingleCycle() const {
    return HasFileSelection() && IsSingleCycleSize(SelectedSize(selected_));
  }

  [[nodiscard]] bool HasSelection() const { return selected_ < count_; }

  [[nodiscard]] bool IsSelectedDirectory() const {
    FileSystem *fileSystem = FileSystem::GetInstance();
    return HasSelection() && fileSystem != nullptr &&
           fileSystem->getFileType(indices_[selected_]) == PFT_DIR;
  }

  [[nodiscard]] bool HasFileSelection() const {
    return HasSelection() && !IsSelectedDirectory();
  }

  [[nodiscard]] std::uint64_t SelectedSize(std::uint16_t index) const {
    FileSystem *fileSystem = FileSystem::GetInstance();
    return fileSystem == nullptr || index >= count_ ||
                   fileSystem->getFileType(indices_[index]) != PFT_FILE
               ? 0U
               : fileSystem->getFileSize(indices_[index]);
  }

  void ReadName(std::uint16_t index, char *destination,
                std::size_t capacity) const {
    if (destination == nullptr || capacity == 0U)
      return;
    destination[0] = '\0';
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr || index >= count_)
      return;
    fileSystem->getFileName(indices_[index], destination,
                            static_cast<int>(capacity));
    destination[capacity - 1U] = '\0';
  }

  Ui2SampleBrowserCommand MakeSelected(Ui2SampleBrowserCommandType type) const {
    Ui2SampleBrowserCommand command{
        .type = type,
        .projectSample = mode_ == Ui2SampleBrowserMode::ProjectPool};
    ReadName(selected_, command.filename.data(), command.filename.size());
    if (command.filename[0] == '\0' ||
        (command.projectSample &&
         !Ui2IsFlatProjectSampleLeaf(command.filename.data())))
      command.type = Ui2SampleBrowserCommandType::None;
    return command;
  }

  Ui2SampleBrowserCommand ActivateSelection() {
    if (!HasSelection())
      return {};
    ClearError();
    if (IsSelectedDirectory()) {
      // RefreshCurrentDirectory filters these in ProjectPool. Keep this guard
      // for adapters whose cached file type changes between list and input.
      if (mode_ == Ui2SampleBrowserMode::ProjectPool) {
        SetError("INVALID SAMPLE");
        return {};
      }
      char name[PFILENAME_SIZE]{};
      ReadName(selected_, name, sizeof(name));
      if (std::strcmp(name, "..") == 0)
        NavigateParent();
      else
        NavigateInto(name);
      return {};
    }
    if (mode_ == Ui2SampleBrowserMode::ProjectPool) {
      if (selectedAction_ == 0U)
        return MakeSelected(Ui2SampleBrowserCommandType::Edit);
      if (selectedAction_ == 1U) {
        mode_ = Ui2SampleBrowserMode::Library;
        if (!JumpToModeRoot()) {
          mode_ = Ui2SampleBrowserMode::ProjectPool;
          JumpToModeRoot();
          SetError("SAMPLE LIB UNAVAILABLE");
        }
        return {.type = Ui2SampleBrowserCommandType::ModeChanged};
      }
      return MakeSelected(Ui2SampleBrowserCommandType::RequestDelete);
    }
    return MakeSelected(selectedAction_ == 0U
                            ? Ui2SampleBrowserCommandType::Import
                            : Ui2SampleBrowserCommandType::Edit);
  }

  void MoveAction(int delta) {
    if (!HasFileSelection()) {
      selectedAction_ = 0U;
      return;
    }
    const int count =
        mode_ == Ui2SampleBrowserMode::ProjectPool ? 3 : 2;
    selectedAction_ = static_cast<std::uint8_t>(
        (count + static_cast<int>(selectedAction_) + delta) % count);
    ClearError();
  }

  void SelectionChanged() {
    top_ = Ui2BrowserSnapshot::ResolveWindowTop(count_, selected_, top_);
    selectedAction_ = 0U;
    ClearError();
  }

  bool JumpToModeRoot() {
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr)
      return false;
    bool changed = false;
    if (mode_ == Ui2SampleBrowserMode::Library) {
      changed = fileSystem->chdir(SAMPLES_LIB_DIR);
    } else {
      changed = fileSystem->chdir(PROJECTS_DIR) &&
                fileSystem->chdir(projectName_.data()) &&
                fileSystem->chdir(PROJECT_SAMPLES_DIR);
    }
    if (!changed)
      return false;
    depth_ = 0U;
    selectedStack_.fill(0U);
    return RefreshCurrentDirectory();
  }

  void NavigateInto(const char *name) {
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (name == nullptr || name[0] == '\0' || fileSystem == nullptr ||
        depth_ >= selectedStack_.size()) {
      SetError("MAX DIRECTORY DEPTH");
      return;
    }
    const std::uint16_t priorSelection = selected_;
    if (!fileSystem->chdir(name)) {
      SetError("CANNOT OPEN DIRECTORY");
      return;
    }
    selectedStack_[depth_++] = priorSelection;
    RefreshCurrentDirectory();
  }

  void NavigateParent() {
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr || fileSystem->isCurrentRoot() ||
        !fileSystem->chdir(".."))
      return;
    const std::uint16_t prior =
        depth_ == 0U ? 0U : selectedStack_[--depth_];
    RefreshCurrentDirectory();
    if (count_ != 0U) {
      selected_ = std::min<std::uint16_t>(prior, count_ - 1U);
      top_ = Ui2BrowserSnapshot::ResolveWindowTop(count_, selected_, 0U);
    }
  }

  std::array<int, MAX_FILE_INDEX_SIZE> indices_{};
  std::array<std::uint16_t, DirectoryDepthCapacity> selectedStack_{};
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> projectName_{};
  std::array<char, PFILENAME_SIZE> pendingDelete_{};
  std::array<char, 32U> error_{};
  Ui2ControllerInputState dialogInput_{};
  std::uint32_t dialogInstanceId_ = 0U;
  std::uint16_t count_ = 0U;
  std::uint16_t selected_ = 0U;
  std::uint16_t top_ = 0U;
  std::uint16_t heldMask_ = 0U;
  std::uint8_t instrument_ = 0U;
  std::uint8_t depth_ = 0U;
  std::uint8_t selectedAction_ = 0U;
  std::uint8_t dialogSelectedAction_ = 1U;
  Ui2SampleBrowserMode mode_ = Ui2SampleBrowserMode::ProjectPool;
  bool active_ = false;
  bool previewHeld_ = false;
  bool toggleChordLatched_ = false;
  bool dialogActive_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2SampleBrowserCommand>);
static_assert(std::is_trivially_copyable_v<Ui2SampleBrowserController>);
static_assert(sizeof(Ui2SampleBrowserController) <= 2'000U);

} // namespace ui2
