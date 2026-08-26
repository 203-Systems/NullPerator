/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/ITrackerInputSink.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Views/Ui2BrowserSnapshot.h"
#include "System/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ui2 {

enum class Ui2ProjectBrowserCommandType : std::uint8_t {
  None,
  Load,
  Delete,
  Back,
};

struct Ui2ProjectBrowserCommand {
  Ui2ProjectBrowserCommandType type = Ui2ProjectBrowserCommandType::None;
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> project{};
};

[[nodiscard]] constexpr Ui2ProjectBrowserCommandType
Ui2ProjectBrowserProjectAction(bool optionHeld, std::uint8_t activeAction) {
  return optionHeld || activeAction != 0U
             ? Ui2ProjectBrowserCommandType::Delete
             : Ui2ProjectBrowserCommandType::Load;
}

// Native fixed-capacity project browser. File-system indices are kept instead
// of names so the controller remains small and allocation-free on firmware.
class Ui2ProjectBrowserController {
public:
  bool Refresh(const char *currentProject = nullptr) {
    heldMask_ = 0U;
    SetCurrentProject(currentProject);
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr || !fileSystem->chdir(PROJECTS_DIR))
      return false;

    depth_ = 1U;
    for (auto &part : path_)
      part.fill('\0');
    CopyDirectoryName(path_[0], ProjectDirectoryName());
    return RefreshCurrentDirectory();
  }

  bool RefreshCurrentDirectory() {
    count_ = 0U;
    selected_ = 0U;
    top_ = 0U;
    activeAction_ = 0U;
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr)
      return false;

    etl::vector<int, MAX_FILE_INDEX_SIZE> listed;
    // Historical firmware allowed leading-dot project names. Real adapters
    // suppress hidden entries by default, so request them only at /projects
    // and then apply the persistence-owned internal-name filter explicitly.
    if (!fileSystem->listChecked(&listed, "", true, InProjectDirectory()))
      return false;
    for (const int fileIndex : listed) {
      if (count_ >= indices_.size() ||
          fileSystem->getFileType(fileIndex) != PFT_DIR)
        continue;
      char name[Ui2BrowserSnapshot::ItemTextCapacity]{};
      fileSystem->getFileName(fileIndex, name, sizeof(name));
      name[sizeof(name) - 1U] = '\0';
      if (name[0] == '\0' || std::strcmp(name, ".") == 0 ||
          std::strcmp(name, "..") == 0 ||
          (InProjectDirectory() &&
           PersistencyService::IsInternalProjectName(name)))
        continue;
      indices_[count_++] = fileIndex;
    }
    return true;
  }

  Ui2ProjectBrowserCommand Handle(TrackerAction action, bool pressed) {
    const std::uint16_t bit = TrackerActionBit(action);
    if (pressed) {
      if ((heldMask_ & bit) != 0U)
        return {};
      heldMask_ |= bit;
    } else {
      heldMask_ &= static_cast<std::uint16_t>(~bit);
      return {};
    }

    if (action == TrackerAction::Up) {
      if (selected_ > 0U)
        --selected_;
      activeAction_ = 0U;
      KeepSelectionVisible();
    } else if (action == TrackerAction::Down) {
      if (selected_ + 1U < RowCount())
        ++selected_;
      activeAction_ = 0U;
      KeepSelectionVisible();
    } else if (action == TrackerAction::Left) {
      MoveAction(-1);
    } else if (action == TrackerAction::Right) {
      MoveAction(1);
    } else if (action == TrackerAction::Edit) {
      const std::uint16_t parentRows = HasParent() ? 1U : 0U;
      if (parentRows != 0U && selected_ == 0U) {
        NavigateParent();
        return {};
      }
      const std::uint16_t directoryIndex =
          static_cast<std::uint16_t>(selected_ - parentRows);
      if (!InProjectDirectory()) {
        NavigateInto(directoryIndex);
        return {};
      }
      Ui2ProjectBrowserCommand command{
          .type = Ui2ProjectBrowserProjectAction(
              (heldMask_ & TrackerActionBit(TrackerAction::Option)) != 0U,
              activeAction_)};
      ReadName(directoryIndex, command.project.data(), command.project.size());
      // DELETE is deliberately unavailable for the active project both in the
      // footer and at command emission. Application re-checks the name before
      // persistence as a second data-safety boundary.
      if (command.project[0] == '\0' ||
          (command.type == Ui2ProjectBrowserCommandType::Delete &&
           IsCurrentProject(command.project.data())))
        command.type = Ui2ProjectBrowserCommandType::None;
      return command;
    }
    return {};
  }

  [[nodiscard]] Ui2BrowserSnapshot
  Snapshot(const char *currentProject = nullptr) const {
    Ui2BrowserSnapshot snapshot;
    Ui2BrowserSnapshot::CopyText(snapshot.title, "BROWSE");
    Ui2BrowserSnapshot::CopyText(
        snapshot.meta, depth_ == 0U ? "/" : path_[depth_ - 1U].data());
    snapshot.ConfigureWindow(RowCount(), selected_, top_);
    const std::uint16_t parentRows = HasParent() ? 1U : 0U;
    for (std::uint8_t row = 0U; row < snapshot.visibleItemCount; ++row) {
      const std::uint16_t listIndex =
          static_cast<std::uint16_t>(snapshot.topIndex + row);
      if (parentRows != 0U && listIndex == 0U) {
        Ui2BrowserSnapshot::CopyText(snapshot.items[row], "..");
        continue;
      }
      char name[Ui2BrowserSnapshot::ItemTextCapacity]{};
      ReadName(static_cast<std::uint16_t>(listIndex - parentRows), name,
               sizeof(name));
      char display[Ui2BrowserSnapshot::ItemTextCapacity]{};
      const char *activeProject =
          currentProject == nullptr ? currentProject_.data() : currentProject;
      std::snprintf(display, sizeof(display), "%s%s",
                    InProjectDirectory() && activeProject != nullptr &&
                            std::strcmp(name, activeProject) == 0
                        ? "*"
                        : "",
                    name);
      Ui2BrowserSnapshot::CopyText(snapshot.items[row], display);
    }
    std::snprintf(snapshot.footer.data(), snapshot.footer.size(), "%u ITEM%s",
                  static_cast<unsigned>(count_), count_ == 1U ? "" : "S");
    const bool parentSelected = parentRows != 0U && selected_ == 0U;
    Ui2BrowserSnapshot::CopyText(snapshot.actions[0], parentSelected ? "UP"
                                                      : InProjectDirectory()
                                                          ? "LOAD"
                                                          : "OPEN");
    snapshot.actionCount = snapshot.hasSelection ? 1U : 0U;
    if (snapshot.hasSelection && !parentSelected && InProjectDirectory()) {
      char selectedName[Ui2BrowserSnapshot::ItemTextCapacity]{};
      ReadName(static_cast<std::uint16_t>(selected_ - parentRows), selectedName,
               sizeof(selectedName));
      if (!IsCurrentProject(selectedName)) {
        Ui2BrowserSnapshot::CopyText(snapshot.actions[1], "DELETE");
        snapshot.actionCount = 2U;
      }
    }
    snapshot.activeAction =
        snapshot.actionCount == 0U
            ? 0U
            : std::min<std::uint8_t>(activeAction_, snapshot.actionCount - 1U);
    return snapshot;
  }

private:
  void KeepSelectionVisible() {
    top_ = Ui2BrowserSnapshot::ResolveWindowTop(RowCount(), selected_, top_);
  }

  [[nodiscard]] std::uint16_t RowCount() const {
    return static_cast<std::uint16_t>(count_ + (HasParent() ? 1U : 0U));
  }

  [[nodiscard]] bool HasParent() const {
    FileSystem *fileSystem = FileSystem::GetInstance();
    return fileSystem != nullptr && !fileSystem->isCurrentRoot();
  }

  [[nodiscard]] bool InProjectDirectory() const {
    return depth_ == 1U &&
           std::strcmp(path_[0].data(), ProjectDirectoryName()) == 0;
  }

  void SetCurrentProject(const char *currentProject) {
    currentProject_.fill('\0');
    if (currentProject != nullptr)
      std::snprintf(currentProject_.data(), currentProject_.size(), "%s",
                    currentProject);
  }

  [[nodiscard]] bool IsCurrentProject(const char *project) const {
    return project != nullptr && project[0] != '\0' &&
           currentProject_[0] != '\0' &&
           std::strcmp(project, currentProject_.data()) == 0;
  }

  void MoveAction(int delta) {
    if (!InProjectDirectory() || !HasProjectSelection()) {
      activeAction_ = 0U;
      return;
    }
    char project[Ui2BrowserSnapshot::ItemTextCapacity]{};
    const std::uint16_t parentRows = HasParent() ? 1U : 0U;
    ReadName(static_cast<std::uint16_t>(selected_ - parentRows), project,
             sizeof(project));
    if (IsCurrentProject(project)) {
      activeAction_ = 0U;
      return;
    }
    constexpr int actionCount = 2;
    activeAction_ = static_cast<std::uint8_t>(
        (actionCount + static_cast<int>(activeAction_) + delta) % actionCount);
  }

  [[nodiscard]] bool HasProjectSelection() const {
    const std::uint16_t parentRows = HasParent() ? 1U : 0U;
    return selected_ >= parentRows && selected_ - parentRows < count_;
  }

  static constexpr const char *ProjectDirectoryName() {
    const char *name = PROJECTS_DIR;
    while (*name == '/')
      ++name;
    return name;
  }

  template <std::size_t Size>
  static void CopyDirectoryName(std::array<char, Size> &destination,
                                const char *source) {
    destination.fill('\0');
    if (source != nullptr)
      std::snprintf(destination.data(), destination.size(), "%s", source);
  }

  void NavigateParent() {
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr || fileSystem->isCurrentRoot() ||
        !fileSystem->chdir(".."))
      return;
    if (depth_ > 0U) {
      --depth_;
      path_[depth_].fill('\0');
    }
    RefreshCurrentDirectory();
  }

  void NavigateInto(std::uint16_t directoryIndex) {
    if (directoryIndex >= count_ || depth_ >= path_.size())
      return;
    char name[Ui2BrowserSnapshot::ItemTextCapacity]{};
    ReadName(directoryIndex, name, sizeof(name));
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (name[0] == '\0' || fileSystem == nullptr || !fileSystem->chdir(name))
      return;
    CopyDirectoryName(path_[depth_], name);
    ++depth_;
    RefreshCurrentDirectory();
  }

  void ReadName(std::uint16_t listIndex, char *destination,
                std::size_t capacity) const {
    if (destination == nullptr || capacity == 0U)
      return;
    destination[0] = '\0';
    if (listIndex >= count_)
      return;
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr)
      return;
    fileSystem->getFileName(indices_[listIndex], destination,
                            static_cast<int>(capacity));
    destination[capacity - 1U] = '\0';
  }

  std::array<int, MAX_FILE_INDEX_SIZE> indices_{};
  std::uint16_t count_ = 0U;
  std::uint16_t selected_ = 0U;
  std::uint16_t top_ = 0U;
  std::uint16_t heldMask_ = 0U;
  std::array<std::array<char, Ui2BrowserSnapshot::ItemTextCapacity>, 8U>
      path_{};
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> currentProject_{};
  std::uint8_t depth_ = 0U;
  std::uint8_t activeAction_ = 0U;
};

} // namespace ui2
