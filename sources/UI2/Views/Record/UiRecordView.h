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

enum class UiRecordFocus : std::uint8_t {
  Source,
  None,
};

enum class UiRecordState : std::uint8_t {
  Unavailable,
  Armed,
  Recording,
  Saving,
};

struct UiRecordViewData {
  std::string_view source = "LINE IN";
  std::string_view elapsed = "00:00";
  std::uint16_t safeWidth = 154;
  std::uint16_t warningWidth = 42;
  std::uint8_t savingPercent = 0;
  RectI16 cursorVisualRect{};
  UiRecordFocus focus = UiRecordFocus::Source;
  UiRecordState state = UiRecordState::Armed;
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool meterAvailable = true;
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
  [[nodiscard]] static constexpr RectI16
  CursorTargetRect(UiRecordFocus focus = UiRecordFocus::Source) {
    switch (focus) {
    case UiRecordFocus::Source:
      return {7, 42, 226, 9};
    case UiRecordFocus::None:
      return {};
    }
    return {};
  }
};

} // namespace ui2
