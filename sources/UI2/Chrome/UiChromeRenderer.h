/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeModel.h"
#include "UI2/Scene/UiSceneBuilder.h"

#include <optional>

namespace ui2 {

enum class UiBuildStatus : std::uint8_t {
  Built,
  CommandOverflow,
  DesignRequired,
};

class UiChromeRenderer {
public:
  [[nodiscard]] static UiBuildStatus
  BuildTop(const UiTopBarModel &model, UiBarScene &scene,
           std::optional<RectI16> navHighlight = std::nullopt);
  [[nodiscard]] static UiBuildStatus BuildBottom(const UiBottomBarModel &model,
                                                 UiBarScene &scene);
  [[nodiscard]] static RectI16 NavTargetRect(UiNavTarget target);
  [[nodiscard]] static UiNavMapModel NavigationMap(UiNavTarget target);
  [[nodiscard]] static RectI16 MetaTargetRect(const UiTopBarModel &model);
  [[nodiscard]] static RectI16 BottomTrackTargetRect(std::int8_t track);
  [[nodiscard]] static RectI16 BottomRgbTargetRect(std::uint8_t component,
                                                   std::uint8_t value);
  [[nodiscard]] static std::int16_t
  BatteryFillWidth(const UiTopBarModel &model);

private:
  static void DrawPower(const UiTopBarModel &model,
                        UiSceneBuilder<64, 256> &builder);
  static void DrawSaving(const UiTopBarModel &model,
                         UiSceneBuilder<64, 256> &builder);
};

} // namespace ui2
