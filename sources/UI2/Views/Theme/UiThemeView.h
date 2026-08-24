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

struct UiThemeViewData {
  std::string_view name = "DEFAULT";
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiThemeView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiThemeViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiThemeViewData &previous,
                          const UiThemeViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 41, 226, 9};
  }
};

} // namespace ui2
