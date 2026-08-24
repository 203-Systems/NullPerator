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

struct UiGrooveViewData {
  std::string_view number = "00";
  std::array<std::uint8_t, 16> steps{};
  std::uint8_t editRow = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiGrooveView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiGrooveViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiGrooveViewData &previous,
                          const UiGrooveViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16 CursorTargetRect(std::uint8_t row);
  [[nodiscard]] static RectI16 RowDamageRect(std::uint8_t row);
};

} // namespace ui2
