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

#include <string_view>

namespace ui2 {

struct UiFontViewData {
  std::string_view font = "REGULAR 5X7";
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

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
