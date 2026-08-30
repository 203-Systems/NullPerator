/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/ITrackerInputSink.h"
#include "Application/Persistency/InstrumentExportRecovery.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/Views/Ui2BrowserSnapshot.h"
#include "System/FileSystem/FileSystem.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ui2 {

enum class Ui2InstrumentBrowserCommandType : std::uint8_t {
  None,
  Import,
  Back,
};

struct Ui2InstrumentBrowserCommand {
  Ui2InstrumentBrowserCommandType type = Ui2InstrumentBrowserCommandType::None;
  std::array<char, MAX_INSTRUMENT_FILENAME_LENGTH + 1U> filename{};
};

// Dedicated allocation-free browser for persisted .pti instruments. It owns
// stable file indices only; filenames are copied into commands/snapshots.
class Ui2InstrumentBrowserController {
public:
  bool Refresh() {
    input_ = {};
    count_ = selected_ = top_ = 0U;
    error_.fill('\0');
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr || !RecoverInstrumentExportJournals() ||
        !fileSystem->chdir(INSTRUMENTS_DIR))
      return false;
    etl::vector<int, MAX_FILE_INDEX_SIZE> listed;
    if (!fileSystem->listChecked(&listed, INSTRUMENT_FILE_EXTENSION, false))
      return false;
    for (const int fileIndex : listed) {
      if (count_ >= indices_.size() ||
          fileSystem->getFileType(fileIndex) == PFT_DIR)
        continue;
      indices_[count_++] = fileIndex;
    }
    return true;
  }

  Ui2InstrumentBrowserCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};
    if (action == TrackerAction::Up && selected_ > 0U) {
      --selected_;
      error_.fill('\0');
    } else if (action == TrackerAction::Down && selected_ + 1U < count_) {
      ++selected_;
      error_.fill('\0');
    } else if (action == TrackerAction::Edit && count_ != 0U) {
      Ui2InstrumentBrowserCommand command{
          .type = Ui2InstrumentBrowserCommandType::Import};
      ReadName(selected_, command.filename.data(), command.filename.size());
      return command;
    }
    top_ = Ui2BrowserSnapshot::ResolveWindowTop(count_, selected_, top_);
    return {};
  }

  void SetError(const char *message) {
    error_.fill('\0');
    if (message == nullptr)
      return;
    std::strncpy(error_.data(), message, error_.size() - 1U);
    error_.back() = '\0';
  }

  [[nodiscard]] Ui2BrowserSnapshot Snapshot() const {
    Ui2BrowserSnapshot snapshot;
    Ui2BrowserSnapshot::CopyText(snapshot.title, "IMPORT");
    snapshot.ConfigureWindow(count_, selected_, top_);
    for (std::uint8_t row = 0U; row < snapshot.visibleItemCount; ++row)
      ReadName(static_cast<std::uint16_t>(snapshot.topIndex + row),
               snapshot.items[row].data(), snapshot.items[row].size());
    if (error_[0] != '\0') {
      Ui2BrowserSnapshot::CopyText(snapshot.footer, error_.data());
    } else {
      std::snprintf(snapshot.footer.data(), snapshot.footer.size(),
                    "%u ITEM%s", static_cast<unsigned>(count_),
                    count_ == 1U ? "" : "S");
    }
    if (snapshot.hasSelection) {
      Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "LOAD");
      snapshot.actionCount = 1U;
    }
    return snapshot;
  }

private:
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

  std::array<int, MAX_FILE_INDEX_SIZE> indices_{};
  std::uint16_t count_ = 0U;
  std::uint16_t selected_ = 0U;
  std::uint16_t top_ = 0U;
  Ui2ControllerInputState input_{};
  std::array<char, 32> error_{};
};

} // namespace ui2
