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

#include <cstdint>
#include <string_view>

namespace ui2 {

struct UiRecordViewData {
  std::string_view source = "LINE IN";
  std::string_view lineGain = "0 DB";
  std::string_view micGain = "0 DB";
  std::string_view elapsed = "00:00";
  std::uint16_t safeWidth = 154;
  std::uint16_t warningWidth = 42;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiRecordView {
public:
  [[nodiscard]] static UiBuildStatus Build(const UiRecordViewData &data,
                                           UiPalette &palette,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiRecordViewData &previous,
                          const UiRecordViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface,
                          const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 42, 226, 9};
  }
};

} // namespace ui2
