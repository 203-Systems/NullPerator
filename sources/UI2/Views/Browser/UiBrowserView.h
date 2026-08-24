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

struct UiBrowserViewData {
  std::string_view title;
  std::string_view meta;
  std::string_view item;
  std::string_view footer;
  std::array<std::string_view, 3> actions{};
  std::uint8_t actionCount = 0;
  std::uint8_t activeAction = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiBrowserView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiBrowserViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiBrowserViewData &previous,
                          const UiBrowserViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 43, 226, 11};
  }
};

} // namespace ui2
