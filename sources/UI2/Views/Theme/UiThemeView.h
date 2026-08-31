/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Interaction/UiVerticalList.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ui2 {

struct UiThemeViewData {
  std::string_view name = "DEFAULT";
  // -1 selects NAME; 0..19 select the twenty configurable palette entries.
  std::int8_t selectedColor = -1;
  std::array<std::uint8_t, 3> selectedRgb{};
  std::uint8_t colorComponent = 0;
  std::uint8_t nameAction = 0;
  std::int16_t scrollOffset = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

// Retained frame state owns every byte referenced by UiThemeViewData. The
// string_view returned by ToViewData is a transient build-time projection and
// must not outlive this object.
struct UiThemeViewState {
  static constexpr std::size_t NameCapacity = 17;

  std::array<char, NameCapacity> name{
      'D', 'E', 'F', 'A', 'U', 'L', 'T', '\0'};
  std::int8_t selectedColor = -1;
  std::array<std::uint8_t, 3> selectedRgb{};
  std::uint8_t colorComponent = 0;
  std::uint8_t nameAction = 0;
  std::int16_t scrollOffset = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;

  [[nodiscard]] UiThemeViewData ToViewData() const;
  bool operator==(const UiThemeViewState &) const = default;
};

static_assert(std::is_trivially_copyable_v<UiThemeViewState>);
static_assert(std::is_trivially_destructible_v<UiThemeViewState>);

class UiThemeView {
public:
  static constexpr std::int16_t kColorFirstTop = 58;
  static constexpr std::int16_t kColorRowPitch = 14;
  static constexpr std::int16_t kColorCursorHeight = 11;

  [[nodiscard]] static UiBuildStatus
  Build(const UiThemeViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiThemeViewData &previous,
                          const UiThemeViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 41, 226, 9};
  }
  [[nodiscard]] static constexpr RectI16 ColorCursorTargetRect(
      std::uint8_t color) {
    return color < UiPalette::kUserColorCount
               ? RectI16{7,
                         static_cast<std::int16_t>(kColorFirstTop +
                                                   color * kColorRowPitch),
                         226, kColorCursorHeight}
               : RectI16{};
  }
  [[nodiscard]] static RectI16 CursorTargetRect(const UiThemeViewData &data);
  [[nodiscard]] static std::int16_t
  RevealCursor(std::int16_t currentOffset, const UiThemeViewData &data);

  static constexpr std::int16_t kContentBottom = static_cast<std::int16_t>(
      kColorFirstTop + (UiPalette::kUserColorCount - 1U) * kColorRowPitch +
      kColorCursorHeight);
  static constexpr std::int16_t kRevealBottom = 198;

  static_assert(kContentBottom == 335,
                "twenty theme colors must end at the approved boundary");
};

} // namespace ui2
