/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Views/Dialog/UiDialogView.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

// Application-thread capture for a legacy modal. The packet owns all text
// consumed by UI2. ToViewData() only creates a short-lived read-only projection
// into this packet; no Variable, ETL string, or legacy View storage escapes.
struct Ui2DialogSnapshot {
  static constexpr std::size_t TextCapacity = 33;
  static constexpr std::size_t ValueCapacity = 17;
  static constexpr std::size_t ElapsedCapacity = 8;
  static constexpr std::uint8_t ProgressPixelWidth = 144;

  ui2::UiDialogKind kind = ui2::UiDialogKind::Message;
  std::array<char, TextCapacity> title{};
  std::array<char, TextCapacity> label{};
  std::array<char, ValueCapacity> value{};
  std::array<char, ElapsedCapacity> elapsed{};
  std::array<ui2::UiDialogAction, ui2::kUiDialogActionCapacity> actions{};
  std::uint8_t progressWidth = 0;
  std::uint8_t actionCount = 0;
  std::uint8_t selectedAction = 0;
  bool actionsFocused = true;

  bool operator==(const Ui2DialogSnapshot &) const = default;

  void SetTitle(std::string_view text) { Copy(title, text); }
  void SetLabel(std::string_view text) { Copy(label, text); }
  void SetValue(std::string_view text) { Copy(value, text); }
  void SetElapsed(std::string_view text) { Copy(elapsed, text); }

  void PushAction(ui2::UiDialogAction action) {
    if (actionCount >= actions.size())
      return;
    actions[actionCount++] = action;
  }

  void SetSelectedAction(int selected, bool focused) {
    actionsFocused = focused;
    if (actionCount == 0U) {
      selectedAction = 0U;
      return;
    }
    selectedAction = static_cast<std::uint8_t>(
        std::clamp(selected, 0, static_cast<int>(actionCount) - 1));
  }

  void SetProgressPercent(int percent) {
    const int bounded = std::clamp(percent, 0, 100);
    progressWidth = static_cast<std::uint8_t>(
        (bounded * static_cast<int>(ProgressPixelWidth)) / 100);
  }

  [[nodiscard]] ui2::UiDialogViewData ToViewData() const {
    return {
        .kind = kind,
        .title = CStringView(title),
        .label = CStringView(label),
        .value = CStringView(value),
        .elapsed = CStringView(elapsed),
        .progressWidth = progressWidth,
        .actions = actions,
        .actionCount = actionCount,
        .selectedAction = selectedAction,
        .actionsFocused = actionsFocused,
    };
  }

private:
  template <std::size_t Size>
  static void Copy(std::array<char, Size> &destination,
                   std::string_view source) {
    static_assert(Size > 0U);
    destination.fill('\0');
    const std::size_t count = std::min(source.size(), Size - 1U);
    std::copy_n(source.begin(), count, destination.begin());
  }

  template <std::size_t Size>
  [[nodiscard]] static std::string_view
  CStringView(const std::array<char, Size> &text) {
    const auto end = std::find(text.begin(), text.end(), '\0');
    return {text.data(), static_cast<std::size_t>(end - text.begin())};
  }
};

static_assert(std::is_trivially_copyable_v<Ui2DialogSnapshot>);
static_assert(sizeof(Ui2DialogSnapshot) <= 104U);
