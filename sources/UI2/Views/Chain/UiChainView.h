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
#include <cstdint>
#include <string_view>

namespace ui2 {

struct UiChainViewData {
  std::string_view number = "00";
  std::array<std::uint8_t, 16> phrases{};
  std::array<std::uint8_t, 16> transposes{};
  std::array<std::string_view, 8> trackNotes{};
  std::array<std::uint8_t, 2> vuLevelTop{148, 148};
  std::uint8_t editRow = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiChainView {
public:
  static constexpr std::int16_t kMeterTop = 50;
  static constexpr std::int16_t kMeterHeight = 148;

  [[nodiscard]] static UiBuildStatus
  Build(const UiChainViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiChainViewData &previous,
                          const UiChainViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16 CursorTargetRect(std::uint8_t row);
  [[nodiscard]] static RectI16 RowDamageRect(std::uint8_t row);
  [[nodiscard]] static RectI16 VuDamageRect(std::uint8_t side);
};

} // namespace ui2
