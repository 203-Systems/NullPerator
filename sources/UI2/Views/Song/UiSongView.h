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

struct UiSongViewData {
  std::string_view name;
  std::string_view elapsed = "00:08";
  std::array<std::array<std::uint8_t, 8>, 16> rows{};
  std::array<std::string_view, 8> notes{};
  std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1,
                                          -1, -1, -1, -1};
  std::array<std::uint8_t, 2> vuLevelTop{14, 34};
  std::uint8_t rowOffset = 0;
  std::uint8_t editRow = 8;
  std::uint8_t editTrack = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool playing = true;
  UiPowerState power = UiPowerState::Playing;
};

class UiSongView {
public:
  [[nodiscard]] static UiBuildStatus Build(const UiSongViewData &data,
                                           UiPalette &palette,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiSongViewData &previous,
                          const UiSongViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface,
                          const UiPalette &palette);
  [[nodiscard]] static RectI16 CellDamageRect(std::uint8_t track,
                                              std::uint8_t row);
  [[nodiscard]] static RectI16 CursorTargetRect(std::uint8_t track,
                                                std::uint8_t row);
  [[nodiscard]] static RectI16 PlaybackTickRect(std::uint8_t track,
                                                std::uint8_t row);
  [[nodiscard]] static RectI16 RowDamageRect(std::uint8_t row);
  [[nodiscard]] static RectI16 TrackHeaderDamageRect(std::uint8_t track);
  [[nodiscard]] static RectI16 BottomTrackDamageRect(std::uint8_t track);
  [[nodiscard]] static RectI16 VuDamageRect(std::uint8_t channel);

private:
  [[nodiscard]] static bool RequiresFullInvalidation(
      const UiSongViewData &previous, const UiSongViewData &current);
};

} // namespace ui2
