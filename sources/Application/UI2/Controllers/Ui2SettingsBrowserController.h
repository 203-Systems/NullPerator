/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/ITrackerInputSink.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Views/Ui2BrowserSnapshot.h"
#include "System/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace ui2 {

enum class Ui2SettingsBrowserMode : std::uint8_t { None, Theme, Font };

enum class Ui2SettingsBrowserCommandType : std::uint8_t {
  None,
  Back,
  ImportTheme,
  SelectFont,
};

struct Ui2SettingsBrowserCommand {
  Ui2SettingsBrowserCommandType type = Ui2SettingsBrowserCommandType::None;
  std::array<char, MAX_THEME_NAME_LENGTH + 5U> theme{};
  std::uint8_t font = 0U;

  [[nodiscard]] bool HasValue() const {
    return type != Ui2SettingsBrowserCommandType::None;
  }
};

// Theme and Font share only the approved Browser renderer primitive. Their
// roots, entries and commands deliberately remain separate from Project
// lifecycle semantics. Storage is fixed-capacity and Handle/Snapshot allocate
// no heap memory.
class Ui2SettingsBrowserController {
public:
  static constexpr std::uint8_t FontCount = 3U;
  static constexpr std::uint8_t SupportedFontCount = 1U;

  bool OpenTheme(const char *currentTheme) {
    Reset(Ui2SettingsBrowserMode::Theme);
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr || !fileSystem->chdir(THEMES_DIR)) {
      SetError("THEME DIRECTORY UNAVAILABLE");
      return false;
    }
    etl::vector<int, MAX_FILE_INDEX_SIZE> listed;
    if (!fileSystem->listChecked(&listed, THEME_FILE_EXTENSION, false)) {
      SetError("TOO MANY THEMES");
      return false;
    }
    for (const int fileIndex : listed) {
      if (themeCount_ >= themeIndices_.size() ||
          fileSystem->getFileType(fileIndex) == PFT_DIR)
        continue;
      themeIndices_[themeCount_++] = fileIndex;
    }
    if (currentTheme != nullptr && currentTheme[0] != '\0') {
      char candidate[Ui2BrowserSnapshot::ItemTextCapacity]{};
      for (std::uint16_t index = 0U; index < themeCount_; ++index) {
        ReadThemeName(index, candidate, sizeof(candidate));
        const std::size_t length = std::strlen(currentTheme);
        if (std::strncmp(candidate, currentTheme, length) == 0 &&
            (candidate[length] == '\0' ||
             std::strcmp(candidate + length, THEME_FILE_EXTENSION) == 0)) {
          selected_ = index;
          break;
        }
      }
    }
    KeepSelectionVisible();
    activeAction_ = themeCount_ == 0U ? 0U : 1U;
    return true;
  }

  void OpenFont(std::uint8_t configuredFont) {
    Reset(Ui2SettingsBrowserMode::Font);
    selected_ = std::min<std::uint16_t>(configuredFont, FontCount - 1U);
    activeAction_ = 1U;
  }

  void Close() { Reset(Ui2SettingsBrowserMode::None); }

  [[nodiscard]] Ui2SettingsBrowserMode Mode() const { return mode_; }
  [[nodiscard]] bool Active() const {
    return mode_ != Ui2SettingsBrowserMode::None;
  }

  void SetError(const char *message) {
    error_.fill('\0');
    if (message != nullptr)
      std::snprintf(error_.data(), error_.size(), "%s", message);
  }

  Ui2SettingsBrowserCommand Handle(TrackerAction action, bool pressed) {
    const std::uint16_t bit = TrackerActionBit(action);
    if (pressed) {
      if ((heldMask_ & bit) != 0U)
        return {};
      heldMask_ |= bit;
    } else {
      heldMask_ &= static_cast<std::uint16_t>(~bit);
      return {};
    }
    if (!Active())
      return {};
    if (action == TrackerAction::Up) {
      if (selected_ > 0U)
        --selected_;
      SelectionChanged();
    } else if (action == TrackerAction::Down) {
      const std::uint16_t count = ItemCount();
      if (selected_ + 1U < count)
        ++selected_;
      SelectionChanged();
    } else if (action == TrackerAction::Left) {
      activeAction_ = 0U;
    } else if (action == TrackerAction::Right && ItemCount() != 0U) {
      activeAction_ = 1U;
    } else if (action == TrackerAction::Edit) {
      if (activeAction_ == 0U || ItemCount() == 0U)
        return {.type = Ui2SettingsBrowserCommandType::Back};
      if (mode_ == Ui2SettingsBrowserMode::Theme) {
        Ui2SettingsBrowserCommand command{
            .type = Ui2SettingsBrowserCommandType::ImportTheme};
        ReadThemeName(selected_, command.theme.data(), command.theme.size());
        if (command.theme[0] == '\0')
          command.type = Ui2SettingsBrowserCommandType::None;
        return command;
      }
      // Only Regular has a real glyph asset today. Bold and Wide stay visible
      // in the approved built-in list, but never emit a selection command that
      // would make Config claim a renderer capability it does not have.
      if (selected_ < SupportedFontCount) {
        return {.type = Ui2SettingsBrowserCommandType::SelectFont,
                .font = static_cast<std::uint8_t>(selected_)};
      }
    }
    return {};
  }

  [[nodiscard]] Ui2BrowserSnapshot Snapshot() const {
    Ui2BrowserSnapshot snapshot;
    Ui2BrowserSnapshot::CopyText(
        snapshot.title,
        mode_ == Ui2SettingsBrowserMode::Font ? "FONTS" : "THEMES");
    Ui2BrowserSnapshot::CopyText(
        snapshot.meta,
        mode_ == Ui2SettingsBrowserMode::Font ? "BUILT-IN" : "NPT");
    snapshot.ConfigureWindow(ItemCount(), selected_, top_);
    for (std::uint8_t row = 0U; row < snapshot.visibleItemCount; ++row) {
      const std::uint16_t item =
          static_cast<std::uint16_t>(snapshot.topIndex + row);
      if (mode_ == Ui2SettingsBrowserMode::Font) {
        Ui2BrowserSnapshot::CopyText(snapshot.items[row], FontName(item));
      } else {
        char name[Ui2BrowserSnapshot::ItemTextCapacity]{};
        ReadThemeName(item, name, sizeof(name));
        Ui2BrowserSnapshot::CopyText(snapshot.items[row], name);
      }
    }

    if (error_[0] != '\0') {
      Ui2BrowserSnapshot::CopyText(snapshot.footer, error_.data());
    } else if (mode_ == Ui2SettingsBrowserMode::Font &&
               selected_ >= SupportedFontCount) {
      Ui2BrowserSnapshot::CopyText(snapshot.footer, "FONT ASSET UNAVAILABLE");
    } else {
      std::snprintf(snapshot.footer.data(), snapshot.footer.size(), "%u ITEM%s",
                    static_cast<unsigned>(ItemCount()),
                    ItemCount() == 1U ? "" : "S");
    }

    Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "CANCEL");
    snapshot.actionCount = 1U;
    if (snapshot.hasSelection) {
      Ui2BrowserSnapshot::CopyText(
          snapshot.actions[1], mode_ == Ui2SettingsBrowserMode::Theme ? "IMPORT"
                               : selected_ < SupportedFontCount
                                   ? "APPLY"
                                   : "UNAVAILABLE");
      snapshot.actionCount = 2U;
    }
    snapshot.activeAction =
        std::min<std::uint8_t>(activeAction_, snapshot.actionCount - 1U);
    return snapshot;
  }

private:
  void Reset(Ui2SettingsBrowserMode mode) {
    mode_ = mode;
    themeCount_ = 0U;
    selected_ = 0U;
    top_ = 0U;
    heldMask_ = 0U;
    activeAction_ = 0U;
    error_.fill('\0');
  }

  void SelectionChanged() {
    error_.fill('\0');
    activeAction_ = ItemCount() == 0U ? 0U : 1U;
    KeepSelectionVisible();
  }

  void KeepSelectionVisible() {
    top_ = Ui2BrowserSnapshot::ResolveWindowTop(ItemCount(), selected_, top_);
  }

  [[nodiscard]] std::uint16_t ItemCount() const {
    return mode_ == Ui2SettingsBrowserMode::Font ? FontCount : themeCount_;
  }

  [[nodiscard]] static const char *FontName(std::uint16_t index) {
    static constexpr std::array<const char *, FontCount> names{"Regular",
                                                               "Bold", "Wide"};
    return index < names.size() ? names[index] : "";
  }

  void ReadThemeName(std::uint16_t index, char *destination,
                     std::size_t capacity) const {
    if (destination == nullptr || capacity == 0U)
      return;
    destination[0] = '\0';
    if (index >= themeCount_)
      return;
    FileSystem *fileSystem = FileSystem::GetInstance();
    if (fileSystem == nullptr)
      return;
    fileSystem->getFileName(themeIndices_[index], destination,
                            static_cast<int>(capacity));
    destination[capacity - 1U] = '\0';
  }

  std::array<int, MAX_FILE_INDEX_SIZE> themeIndices_{};
  std::array<char, 32> error_{};
  std::uint16_t themeCount_ = 0U;
  std::uint16_t selected_ = 0U;
  std::uint16_t top_ = 0U;
  std::uint16_t heldMask_ = 0U;
  Ui2SettingsBrowserMode mode_ = Ui2SettingsBrowserMode::None;
  std::uint8_t activeAction_ = 0U;
};

static_assert(std::is_trivially_copyable_v<Ui2SettingsBrowserCommand>);
static_assert(std::is_trivially_copyable_v<Ui2SettingsBrowserController>);
static_assert(sizeof(Ui2SettingsBrowserController) <= 1'100U);

} // namespace ui2
