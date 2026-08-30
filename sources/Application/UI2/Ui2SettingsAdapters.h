/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2SettingsSnapshots.h"
#include "Application/UI2/Workflows/Ui2ThemeWorkflow.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/Views/Font/UiFontView.h"
#include "UI2/Views/Theme/UiThemeView.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

static_assert(ThemeViewUi2Snapshot::ColorCount ==
              UiPalette::kUserColorCount);

template <std::size_t DestinationSize, std::size_t SourceSize>
inline void CopySettingsText(
    std::array<char, DestinationSize> &destination,
    const std::array<char, SourceSize> &source) {
  static_assert(DestinationSize > 0U);
  destination.fill('\0');
  const std::size_t count =
      DestinationSize - 1U < SourceSize ? DestinationSize - 1U : SourceSize;
  for (std::size_t index = 0; index < count && source[index] != '\0'; ++index)
    destination[index] = source[index];
}

// These conversions copy all retained text. The projected string_views from
// ToViewData are only used while building a scene on the application thread.
inline UiThemeViewState
MakeUiThemeViewState(const ThemeViewUi2Snapshot &snapshot,
                     UiPowerState power = UiPowerState::BatteryNormal) {
  UiThemeViewState state;
  CopySettingsText(state.name, snapshot.name);
  state.nameAction = snapshot.nameAction < 4U ? snapshot.nameAction : 0U;
  state.selectedColor =
      snapshot.focus == ThemeViewUi2Focus::Color &&
              snapshot.selectedColor >= 0 &&
              static_cast<std::size_t>(snapshot.selectedColor) <
                  ThemeViewUi2Snapshot::ColorCount
          ? snapshot.selectedColor
          : -1;
  if (state.selectedColor >= 0 && snapshot.colorsValid) {
    state.selectedRgb = Ui2ThemeWorkflow::Components(
        snapshot.colors[static_cast<std::size_t>(state.selectedColor)]);
  }
  state.power = power;
  return state;
}

inline UiFontViewState
MakeUiFontViewState(const FontViewUi2Snapshot &snapshot,
                    UiPowerState power = UiPowerState::BatteryNormal) {
  UiFontViewState state;
  CopySettingsText(state.font, snapshot.font);
  state.power = power;
  return state;
}

// Palette synchronization is deliberately separate from frame-state capture.
// Call ApplyThemeSnapshotToPalette only after an explicit theme load or user
// color commit; calling MakeUiThemeViewState never changes renderer colors.
[[nodiscard]] inline bool
ApplyThemeSnapshotToPalette(const ThemeViewUi2Snapshot &snapshot,
                            UiPalette &palette) {
  if (!snapshot.colorsValid)
    return false;
  std::array<Rgb888, UiPalette::kUserColorCount> colors{};
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    const std::uint32_t color = snapshot.colors[index];
    colors[index] =
        {static_cast<std::uint8_t>((color >> 16U) & 0xFFU),
         static_cast<std::uint8_t>((color >> 8U) & 0xFFU),
         static_cast<std::uint8_t>(color & 0xFFU)};
  }
  palette.SetUserColors(colors);
  return true;
}

// A native UI2 theme controller may copy all independent palette
// values into its owned snapshot. This does not make the twelve-color legacy
// Config independently editable; its capability masks remain authoritative.
inline void CopyPaletteToThemeSnapshot(const UiPalette &palette,
                                       ThemeViewUi2Snapshot &snapshot) {
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    const Rgb888 color = palette.Get(static_cast<PaletteIndex>(index));
    snapshot.colors[index] =
        (static_cast<std::uint32_t>(color.red) << 16U) |
        (static_cast<std::uint32_t>(color.green) << 8U) |
        static_cast<std::uint32_t>(color.blue);
  }
  snapshot.colorsValid = true;
}

} // namespace ui2
