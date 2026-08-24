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
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ui2 {

inline constexpr std::size_t kUiBrowserVisibleRowCapacity = 13;

struct UiBrowserViewData {
  std::string_view title;
  std::string_view meta;
  // The firmware supplies only the current visible window. This keeps browser
  // rendering fixed-capacity even when the backing directory contains many
  // entries.
  std::array<std::string_view, kUiBrowserVisibleRowCapacity> items{};
  std::uint8_t visibleItemCount = 0;
  std::uint8_t selectedRow = 0;
  std::uint16_t topIndex = 0;
  std::uint16_t totalItemCount = 0;
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
  [[nodiscard]] static constexpr RectI16
  CursorTargetRect(std::uint8_t row = 0) {
    return row < kUiBrowserVisibleRowCapacity
               ? RectI16{7, static_cast<std::int16_t>(43 + row * 11), 226, 11}
               : RectI16{};
  }
  [[nodiscard]] static RectI16
  ScrollThumbRect(const UiBrowserViewData &data);
};

} // namespace ui2
