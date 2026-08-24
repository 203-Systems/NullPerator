/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Theme/UiPalette.h"

#include <array>
#include <string_view>

namespace ui2 {

struct UiThemeColorElement {
  UiColorToken token;
  std::string_view key;
  std::string_view label;
};

inline constexpr std::array<UiThemeColorElement, UiPalette::kUserColorCount>
    kUiThemeColors{{
        {UiColorToken::SurfaceBackground, "surface.bg", "BACKGROUND"},
        {UiColorToken::SurfaceTopBar, "surface.top_bar", "TOP BAR"},
        {UiColorToken::SurfaceBottomBar, "surface.bottom_bar", "BOTTOM BAR"},
        {UiColorToken::TextNormal, "text.normal", "TEXT NORMAL"},
        {UiColorToken::TextDim, "text.dim", "TEXT DIM"},
        {UiColorToken::TextHighlighted, "text.highlighted",
         "TEXT HIGHLIGHTED"},
        {UiColorToken::TextColored, "text.colored", "TEXT COLORED"},
        {UiColorToken::CursorPrimary, "cursor.primary", "CURSOR"},
        {UiColorToken::CursorRow, "cursor.row", "CURSOR ROW"},
        {UiColorToken::PlaybackActive, "playback.active", "PLAYBACK"},
        {UiColorToken::SystemInfo, "system.info", "SYSTEM INFO"},
        {UiColorToken::SystemWarning, "system.warning", "SYSTEM WARNING"},
        {UiColorToken::SystemError, "system.error", "SYSTEM ERROR"},
        {UiColorToken::BatteryNormal, "battery.normal", "BATTERY NORMAL"},
        {UiColorToken::BatteryCharging, "battery.charging",
         "BATTERY CHARGING"},
        {UiColorToken::BatteryLow, "battery.low", "BATTERY LOW"},
        {UiColorToken::VuSafe, "vu.safe", "VU SAFE"},
        {UiColorToken::VuWarning, "vu.warning", "VU WARNING"},
        {UiColorToken::VuPeak, "vu.peak", "VU PEAK"},
    }};

} // namespace ui2
