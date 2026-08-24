/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace ui2 {

struct UiFontViewData {
  std::string_view font = "REGULAR 5X7";
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

// Fixed-capacity counterpart to UiFontViewData for firmware/WASM retained
// frames. ToViewData only borrows this object's internal font buffer.
struct UiFontViewState {
  static constexpr std::size_t FontCapacity = 41;

  std::array<char, FontCapacity> font{
      'R', 'E', 'G', 'U', 'L', 'A', 'R', ' ', '5', 'X', '7', '\0'};
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;

  [[nodiscard]] UiFontViewData ToViewData() const;
  bool operator==(const UiFontViewState &) const = default;
};

static_assert(std::is_trivially_copyable_v<UiFontViewState>);
static_assert(std::is_trivially_destructible_v<UiFontViewState>);

class UiFontView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiFontViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiFontViewData &previous,
                          const UiFontViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 53, 226, 9};
  }
};

} // namespace ui2
