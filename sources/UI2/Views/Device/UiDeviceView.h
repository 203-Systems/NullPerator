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

#include <cstdint>
#include <string_view>

namespace ui2 {

struct UiDeviceViewData {
  std::string_view midiDevice = "OFF";
  std::string_view midiSync = "OFF";
  std::string_view remoteUi = "ON";
  std::string_view resampler = "NONE";
  std::string_view volume = "40";
  std::string_view brightness = "FF";
  std::string_view theme = "DEFAULT";
  std::string_view font = "REGULAR";
  std::string_view version = "VERSION 2.3 BETA";
  std::uint8_t batteryPercent = 60;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  std::int16_t scrollOffset = 0;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiDeviceView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiDeviceViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiDeviceViewData &previous,
                          const UiDeviceViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 53, 226, 9};
  }
  [[nodiscard]] static constexpr std::int16_t
  RevealCursor(std::int16_t currentOffset) {
    return UiVerticalList::Reveal(currentOffset, CursorTargetRect(), 34, 208,
                                  203);
  }
  [[nodiscard]] static RectI16 FieldDamageRect(std::int16_t y);
};

} // namespace ui2
