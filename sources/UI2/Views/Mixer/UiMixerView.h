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

struct UiMixerViewData {
  std::array<std::array<std::uint8_t, 2>, 9> vuLevelTop{};
  std::array<std::string_view, 9> volumes{};
  std::int8_t selectedChannel = 0;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiMixerView {
public:
  static constexpr std::uint8_t kChannelCount = 9;
  static constexpr std::int16_t kMeterTop = 46;
  static constexpr std::int16_t kMeterHeight = 153;

  [[nodiscard]] static UiBuildStatus
  Build(const UiMixerViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiMixerViewData &previous,
                          const UiMixerViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16 MeterDamageRect(std::uint8_t channel,
                                               std::uint8_t side);
  [[nodiscard]] static RectI16
  MeterLevelDamageRect(std::uint8_t channel, std::uint8_t side,
                       std::uint8_t previousLevelTop,
                       std::uint8_t currentLevelTop);
  [[nodiscard]] static RectI16 ValueDamageRect(std::uint8_t channel);
  [[nodiscard]] static RectI16 LabelDamageRect(std::uint8_t channel);
};

} // namespace ui2
