/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Views/Browser/UiBrowserView.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

// Fixed-capacity application-thread state for file/project browsers. The
// renderer-facing string_views returned by ViewData() borrow this snapshot,
// never a FileSystem buffer or an ETL temporary. Keep the snapshot alive until
// UiBrowserView has finished building/rendering the frame.
struct Ui2BrowserSnapshot {
  static constexpr std::size_t VisibleRowCapacity =
      ui2::kUiBrowserVisibleRowCapacity;
  static constexpr std::size_t ItemTextCapacity = 36;
  static constexpr std::size_t ActionCapacity = 3;

  std::array<char, 24> title{};
  std::array<char, 8> meta{};
  std::array<std::array<char, ItemTextCapacity>, VisibleRowCapacity> items{};
  std::array<char, 32> footer{};
  std::array<std::array<char, 12>, ActionCapacity> actions{};
  std::uint16_t topIndex = 0;
  std::uint16_t totalItemCount = 0;
  std::uint8_t visibleItemCount = 0;
  std::uint8_t selectedRow = 0;
  std::uint8_t actionCount = 0;
  std::uint8_t activeAction = 0;
  bool hasSelection = false;

  template <std::size_t Size>
  static void CopyText(std::array<char, Size> &destination,
                       std::string_view source) {
    destination.fill('\0');
    if constexpr (Size > 1U) {
      const std::size_t count = std::min(source.size(), Size - 1U);
      if (count != 0U)
        std::memcpy(destination.data(), source.data(), count);
    }
  }

  // Reconcile the legacy browser's larger text-grid page with UI2's thirteen
  // 11-pixel rows while preserving the real selected item and scroll origin
  // whenever it still fits.
  [[nodiscard]] static constexpr std::uint16_t
  ResolveWindowTop(std::uint16_t total, std::uint16_t selected,
                   std::uint16_t legacyTop) {
    if (total == 0U)
      return 0U;
    selected = std::min<std::uint16_t>(selected, total - 1U);
    const std::uint16_t maximumTop =
        total > VisibleRowCapacity
            ? static_cast<std::uint16_t>(total - VisibleRowCapacity)
            : 0U;
    std::uint16_t top = std::min(legacyTop, maximumTop);
    if (selected < top) {
      top = selected;
    } else if (static_cast<std::uint32_t>(selected) >=
               static_cast<std::uint32_t>(top) + VisibleRowCapacity) {
      top = static_cast<std::uint16_t>(
          selected - static_cast<std::uint16_t>(VisibleRowCapacity - 1U));
    }
    return std::min(top, maximumTop);
  }

  void ConfigureWindow(std::size_t total, std::size_t selected,
                       std::size_t legacyTop) {
    const std::size_t clampedTotal =
        std::min<std::size_t>(total, std::numeric_limits<std::uint16_t>::max());
    totalItemCount = static_cast<std::uint16_t>(clampedTotal);
    if (totalItemCount == 0U) {
      topIndex = 0U;
      visibleItemCount = 0U;
      selectedRow = 0U;
      hasSelection = false;
      return;
    }
    const std::uint16_t selectedIndex = static_cast<std::uint16_t>(
        std::min<std::size_t>(selected, totalItemCount - 1U));
    const std::uint16_t suppliedTop = static_cast<std::uint16_t>(
        std::min<std::size_t>(legacyTop,
                              std::numeric_limits<std::uint16_t>::max()));
    topIndex = ResolveWindowTop(totalItemCount, selectedIndex, suppliedTop);
    visibleItemCount = static_cast<std::uint8_t>(std::min<std::uint16_t>(
        static_cast<std::uint16_t>(VisibleRowCapacity),
        static_cast<std::uint16_t>(totalItemCount - topIndex)));
    selectedRow = static_cast<std::uint8_t>(selectedIndex - topIndex);
    hasSelection = selectedRow < visibleItemCount;
  }

  [[nodiscard]] ui2::UiBrowserViewData
  ViewData(ui2::UiPowerState power =
               ui2::UiPowerState::BatteryNormal) const {
    ui2::UiBrowserViewData data;
    data.title = title.data();
    data.meta = meta.data();
    for (std::size_t row = 0; row < items.size(); ++row)
      data.items[row] = items[row].data();
    data.visibleItemCount = std::min<std::uint8_t>(
        visibleItemCount, static_cast<std::uint8_t>(items.size()));
    data.selectedRow = selectedRow;
    data.topIndex = topIndex;
    data.totalItemCount = totalItemCount;
    data.footer = footer.data();
    for (std::size_t action = 0; action < actions.size(); ++action)
      data.actions[action] = actions[action].data();
    data.actionCount = std::min<std::uint8_t>(
        actionCount, static_cast<std::uint8_t>(actions.size()));
    data.activeAction = data.actionCount == 0U
                            ? 0U
                            : std::min<std::uint8_t>(
                                  activeAction, data.actionCount - 1U);
    data.cursorInkVisible = hasSelection;
    data.power = power;
    return data;
  }

  bool operator==(const Ui2BrowserSnapshot &) const = default;
};

static_assert(std::is_trivially_copyable_v<Ui2BrowserSnapshot>);
static_assert(std::is_trivially_destructible_v<Ui2BrowserSnapshot>);
static_assert(sizeof(Ui2BrowserSnapshot) <= 640U);
