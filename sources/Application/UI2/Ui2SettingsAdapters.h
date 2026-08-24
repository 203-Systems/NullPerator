/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2SettingsSnapshots.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/Views/Font/UiFontView.h"
#include "UI2/Views/Theme/UiThemeView.h"

#include <cstddef>
#include <cstdint>

namespace ui2 {

static_assert(ThemeViewUi2Snapshot::ColorCount ==
              UiPalette::kUserColorCount);

// These conversions copy all retained text. The projected string_views from
// ToViewData are only used while building a scene on the application thread.
inline UiThemeViewState
MakeUiThemeViewState(const ThemeViewUi2Snapshot &snapshot,
                     UiPowerState power = UiPowerState::BatteryNormal) {
  UiThemeViewState state;
  state.name = snapshot.name;
  state.nameAction = snapshot.nameAction;
  state.selectedColor = snapshot.focus == ThemeViewUi2Focus::Color
                            ? snapshot.selectedColor
                            : -1;
  state.power = power;
  return state;
}

inline UiFontViewState
MakeUiFontViewState(const FontViewUi2Snapshot &snapshot,
                    UiPowerState power = UiPowerState::BatteryNormal) {
  UiFontViewState state;
  state.font = snapshot.font;
  state.power = power;
  return state;
}

// Palette synchronization is deliberately separate from frame-state capture.
// Call ApplyThemeSnapshotToPalette only after an explicit theme load or user
// color commit; calling MakeUiThemeViewState never changes renderer colors.
inline void ApplyThemeSnapshotToPalette(const ThemeViewUi2Snapshot &snapshot,
                                        UiPalette &palette) {
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    const std::uint32_t color = snapshot.colors[index];
    palette.Set(static_cast<PaletteIndex>(index),
                {static_cast<std::uint8_t>((color >> 16U) & 0xFFU),
                 static_cast<std::uint8_t>((color >> 8U) & 0xFFU),
                 static_cast<std::uint8_t>(color & 0xFFU)});
  }
}

// Before editing an already-loaded UI2 theme, copy the nineteen independent
// palette values back to the application snapshot. This avoids re-expanding
// the twelve-color legacy compatibility map on every frame.
inline void CopyPaletteToThemeSnapshot(const UiPalette &palette,
                                       ThemeViewUi2Snapshot &snapshot) {
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    const Rgb888 color = palette.Get(static_cast<PaletteIndex>(index));
    snapshot.colors[index] =
        (static_cast<std::uint32_t>(color.red) << 16U) |
        (static_cast<std::uint32_t>(color.green) << 8U) |
        static_cast<std::uint32_t>(color.blue);
  }
}

} // namespace ui2
